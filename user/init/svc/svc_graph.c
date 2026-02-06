#include "svc_internal.h"

#include <stdio.h>
#include <string.h>

static bool svc_dfs_cycle_check(struct svc_manager *mgr, int idx, int *path, int path_len) {
    struct svc_graph_node *node = &mgr->graph[idx];

    if (node->in_path) {
        printf("ERROR: Circular dependency detected:\n  ");
        for (int i = 0; i < path_len; i++) {
            printf("%s -> ", mgr->configs[path[i]].name);
        }
        printf("%s\n", mgr->configs[idx].name);
        return true;
    }

    if (node->visited) {
        return false;
    }

    node->in_path  = true;
    path[path_len] = idx;

    for (int i = 0; i < node->dep_count; i++) {
        int target = node->deps[i].target_idx;
        if (svc_dfs_cycle_check(mgr, target, path, path_len + 1)) {
            return true;
        }
    }

    node->in_path = false;
    node->visited = true;
    return false;
}

static int svc_detect_cycles(struct svc_manager *mgr) {
    int path[SVC_MAX_SERVICES];

    for (int i = 0; i < mgr->count; i++) {
        mgr->graph[i].visited = false;
        mgr->graph[i].in_path = false;
    }

    for (int i = 0; i < mgr->count; i++) {
        if (!mgr->graph[i].visited) {
            if (svc_dfs_cycle_check(mgr, i, path, 0)) {
                return -1;
            }
        }
    }

    return 0;
}

static int svc_topological_sort(struct svc_manager *mgr) {
    int in_degree[SVC_MAX_SERVICES] = {0};
    int queue[SVC_MAX_SERVICES];
    int queue_front = 0, queue_back = 0;
    int level = 0;

    for (int i = 0; i < mgr->count; i++) {
        struct svc_graph_node *node = &mgr->graph[i];
        for (int j = 0; j < node->dep_count; j++) {
            in_degree[i]++;
        }
    }

    for (int i = 0; i < mgr->count; i++) {
        if (in_degree[i] == 0) {
            queue[queue_back++]      = i;
            mgr->graph[i].topo_level = 0;
        }
    }

    int processed = 0;
    while (queue_front < queue_back) {
        int level_size = queue_back - queue_front;

        for (int i = 0; i < level_size; i++) {
            int idx                      = queue[queue_front++];
            mgr->topo_order[processed++] = idx;

            for (int j = 0; j < mgr->count; j++) {
                struct svc_graph_node *node = &mgr->graph[j];
                for (int k = 0; k < node->dep_count; k++) {
                    if (node->deps[k].target_idx == idx) {
                        in_degree[j]--;
                        if (in_degree[j] == 0) {
                            queue[queue_back++]      = j;
                            mgr->graph[j].topo_level = mgr->graph[idx].topo_level + 1;
                        }
                    }
                }
            }
        }
        level++;
    }

    if (processed != mgr->count) {
        printf("ERROR: Topological sort failed (cyclic dependency?)\n");
        return -1;
    }

    mgr->max_topo_level = level - 1;
    printf("Dependency graph validated: %d services, %d levels\n", mgr->count, level);
    return 0;
}

int svc_build_dependency_graph(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        mgr->graph[i].dep_count    = 0;
        mgr->graph[i].topo_level   = 0;
        mgr->graph[i].pending_deps = 0;
        mgr->graph[i].visited      = false;
        mgr->graph[i].in_path      = false;
    }

    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        for (int j = 0; j < cfg->after_count; j++) {
            int dep_idx = svc_find_by_name(mgr, cfg->after[j]);
            if (dep_idx < 0) {
                printf("ERROR: Service '%s' depends on unknown service '%s' (after)\n", cfg->name,
                       cfg->after[j]);
                return -1;
            }

            if (node->dep_count >= SVC_DEPS_MAX * 3) {
                printf("ERROR: Service '%s' has too many dependencies\n", cfg->name);
                return -1;
            }

            node->deps[node->dep_count].target_idx = dep_idx;
            node->deps[node->dep_count].type       = DEP_AFTER;
            strncpy(node->deps[node->dep_count].name, cfg->after[j], SVC_NAME_MAX);
            node->dep_count++;
        }

        for (int j = 0; j < cfg->ready_count; j++) {
            int dep_idx = svc_find_by_name(mgr, cfg->ready[j]);
            if (dep_idx < 0) {
                printf("ERROR: Service '%s' requires unknown service '%s' (ready)\n", cfg->name,
                       cfg->ready[j]);
                return -1;
            }

            if (node->dep_count >= SVC_DEPS_MAX * 3) {
                printf("ERROR: Service '%s' has too many dependencies\n", cfg->name);
                return -1;
            }

            node->deps[node->dep_count].target_idx = dep_idx;
            node->deps[node->dep_count].type       = DEP_REQUIRES;
            strncpy(node->deps[node->dep_count].name, cfg->ready[j], SVC_NAME_MAX);
            node->dep_count++;
        }
    }

    if (svc_detect_cycles(mgr) < 0) {
        return -1;
    }

    if (svc_topological_sort(mgr) < 0) {
        return -1;
    }

    mgr->graph_valid = true;
    return 0;
}

bool svc_can_start_advanced(struct svc_manager *mgr, int idx) {
    struct svc_config     *cfg  = &mgr->configs[idx];
    struct svc_graph_node *node = &mgr->graph[idx];

    for (int i = 0; i < node->dep_count; i++) {
        struct svc_dependency *dep       = &node->deps[i];
        int                    target    = dep->target_idx;
        struct svc_runtime    *target_rt = &mgr->runtime[target];

        switch (dep->type) {
        case DEP_REQUIRES:
            if (target_rt->state < SVC_STATE_RUNNING || !target_rt->ready) {
                return false;
            }
            break;
        case DEP_WANTS:
            if (target_rt->state == SVC_STATE_RUNNING && !target_rt->ready) {
                return false;
            }
            break;
        case DEP_AFTER:
            if (target_rt->state < SVC_STATE_STARTING) {
                return false;
            }
            break;
        }
    }

    if (cfg->wait_path[0]) {
        return false;
    }

    return true;
}
