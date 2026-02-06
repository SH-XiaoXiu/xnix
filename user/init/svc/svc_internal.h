#ifndef INIT_SVC_INTERNAL_H
#define INIT_SVC_INTERNAL_H

#include "svc_manager.h"

struct svc_handle_def *handle_def_get_or_add(struct svc_manager *mgr, const char *name);
void                   svc_resolve_handles(struct svc_manager *mgr);
bool                   svc_can_start_advanced(struct svc_manager *mgr, int idx);
int                    svc_try_mount_service(struct svc_manager *mgr, int idx);

#endif /* INIT_SVC_INTERNAL_H */
