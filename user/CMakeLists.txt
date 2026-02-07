# 用户态程序构建
#
# 职责：
#   - 编译所有用户态程序（drivers, apps, demos, init）
#   - 导出 CORE_SERVICES 供 iso 模块使用
#
# 规范：
#   - 每个程序使用 app.cmake 模板构建
#   - APP_INSTALL_TO_SYSTEM=ON  -> 打包到 system.img
#   - APP_INSTALL_TO_SYSTEM=OFF -> 输出到 optional/
#

# 通用编译选项
set(USER_C_FLAGS
        -m32 -march=i386
        -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector
        -Wall -Wextra
        -g
)

# 确保 include 目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

# ============================================
# 第三方库
# ============================================
add_subdirectory(tplibs)

# ============================================
# 用户态库
# ============================================
add_subdirectory(libs)

# ============================================
# Driver 模块 (UDM 驱动)
# ============================================
add_subdirectory(drivers)

# ============================================
# Server 模块 (系统服务)
# ============================================
add_subdirectory(servers)

# ============================================
# 应用程序
# ============================================
add_subdirectory(apps)

# ============================================
# 服务发现与配置生成
# ============================================
# 解析 service.conf 的函数
function(parse_service_conf FILE_PATH OUT_PREFIX)
    if (NOT EXISTS ${FILE_PATH})
        return()
    endif ()

    file(STRINGS ${FILE_PATH} LINES)

    set(${OUT_PREFIX}_CORE FALSE PARENT_SCOPE)
    set(${OUT_PREFIX}_AFTER "" PARENT_SCOPE)
    set(${OUT_PREFIX}_CAPS "" PARENT_SCOPE)
    set(${OUT_PREFIX}_MOUNT "" PARENT_SCOPE)
    set(${OUT_PREFIX}_RESPAWN FALSE PARENT_SCOPE)
    set(${OUT_PREFIX}_PATH "" PARENT_SCOPE)
    set(${OUT_PREFIX}_PROVIDES "" PARENT_SCOPE)
    set(${OUT_PREFIX}_REQUIRES "" PARENT_SCOPE)
    set(${OUT_PREFIX}_WANTS "" PARENT_SCOPE)

    foreach (LINE ${LINES})
        string(REGEX MATCH "^[ \t]*#" IS_COMMENT "${LINE}")
        if (IS_COMMENT)
            continue()
        endif ()
        string(STRIP "${LINE}" LINE)
        if ("${LINE}" STREQUAL "")
            continue()
        endif ()

        string(REGEX MATCH "^([a-z_]+)[ \t]*=[ \t]*(.*)$" MATCH "${LINE}")
        if (MATCH)
            set(KEY "${CMAKE_MATCH_1}")
            set(VALUE "${CMAKE_MATCH_2}")
            string(STRIP "${VALUE}" VALUE)

            if (KEY STREQUAL "core" AND VALUE STREQUAL "true")
                set(${OUT_PREFIX}_CORE TRUE PARENT_SCOPE)
            elseif (KEY STREQUAL "after")
                set(${OUT_PREFIX}_AFTER "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "caps")
                set(${OUT_PREFIX}_CAPS "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "mount")
                set(${OUT_PREFIX}_MOUNT "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "respawn" AND VALUE STREQUAL "true")
                set(${OUT_PREFIX}_RESPAWN TRUE PARENT_SCOPE)
            elseif (KEY STREQUAL "path")
                set(${OUT_PREFIX}_PATH "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "profile")
                set(${OUT_PREFIX}_PROFILE "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "handles")
                set(${OUT_PREFIX}_HANDLES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "provides")
                set(${OUT_PREFIX}_PROVIDES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "requires")
                set(${OUT_PREFIX}_REQUIRES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "wants")
                set(${OUT_PREFIX}_WANTS "${VALUE}" PARENT_SCOPE)
            endif ()
        endif ()
    endforeach ()
endfunction()

# ============================================
# 核心服务列表直接硬编码 核心服务是固定的
# ============================================
set(CORE_SERVICES
        seriald
        kbd
        # ramfsd - 已内置到 init，不再作为独立服务
        # rootfsd - 已被 system.img 替代，不再需要
        vfsserver
        fatfsd
        vgad
        fbd
        fbcond
        ttyd
        klogd
        # shell - 移到 /bin/，不是系统服务
)

set(USER_SERVICES "")

# 扫描 Servers(保留占位)
foreach (SERVER ${SERVER_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/servers/${SERVER}/service.conf")
    parse_service_conf("${SVC_CONF}" "SVC")

    if (SVC_CORE)
        list(APPEND CORE_SERVICES "${SERVER}")
        set(SVC_${SERVER}_CORE TRUE CACHE INTERNAL "")
    else ()
        list(APPEND USER_SERVICES "${SERVER}")
        set(SVC_${SERVER}_CORE FALSE CACHE INTERNAL "")
    endif ()

    set(SVC_${SERVER}_TYPE "server" CACHE INTERNAL "")
    set(SVC_${SERVER}_AFTER "${SVC_AFTER}" CACHE INTERNAL "")
    set(SVC_${SERVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_MOUNT "${SVC_MOUNT}" CACHE INTERNAL "")
    set(SVC_${SERVER}_RESPAWN "${SVC_RESPAWN}" CACHE INTERNAL "")
    set(SVC_${SERVER}_PROFILE "${SVC_PROFILE}" CACHE INTERNAL "")
    set(SVC_${SERVER}_PROVIDES "${SVC_PROVIDES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_REQUIRES "${SVC_REQUIRES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_WANTS "${SVC_WANTS}" CACHE INTERNAL "")
    set(SVC_${SERVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    # 路径：优先用 service.conf 指定的，否则默认
    if (SVC_PATH)
        set(SVC_${SERVER}_PATH "${SVC_PATH}" CACHE INTERNAL "")
    else ()
        set(SVC_${SERVER}_PATH "/sys/drivers/${SERVER}.elf" CACHE INTERNAL "")
    endif ()
endforeach ()

# 扫描应用
foreach (APP ${APP_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/apps/${APP}/service.conf")
    if (EXISTS ${SVC_CONF})
        parse_service_conf("${SVC_CONF}" "SVC")

        # apps 也可以是 core
        if (SVC_CORE)
            list(APPEND CORE_SERVICES "${APP}")
            set(SVC_${APP}_CORE TRUE CACHE INTERNAL "")
        else ()
            list(APPEND USER_SERVICES "${APP}")
            set(SVC_${APP}_CORE FALSE CACHE INTERNAL "")
        endif ()

        set(SVC_${APP}_TYPE "app" CACHE INTERNAL "")
        set(SVC_${APP}_AFTER "${SVC_AFTER}" CACHE INTERNAL "")
        set(SVC_${APP}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
        set(SVC_${APP}_MOUNT "${SVC_MOUNT}" CACHE INTERNAL "")
        set(SVC_${APP}_RESPAWN "${SVC_RESPAWN}" CACHE INTERNAL "")
        set(SVC_${APP}_PROFILE "${SVC_PROFILE}" CACHE INTERNAL "")
        set(SVC_${APP}_PROVIDES "${SVC_PROVIDES}" CACHE INTERNAL "")
        set(SVC_${APP}_REQUIRES "${SVC_REQUIRES}" CACHE INTERNAL "")
        set(SVC_${APP}_WANTS "${SVC_WANTS}" CACHE INTERNAL "")
        set(SVC_${APP}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
        # 路径：优先用 service.conf 指定的，否则默认
        if (SVC_PATH)
            set(SVC_${APP}_PATH "${SVC_PATH}" CACHE INTERNAL "")
        else ()
            set(SVC_${APP}_PATH "/sys/bin/${APP}.elf" CACHE INTERNAL "")
        endif ()
    endif ()
endforeach ()

# 扫描 bin 工具（确定哪些是 core）
# bin 工具不是服务，不需要 services.conf 条目
# core=true 的打包到 rootfs.img，core=false 的放到 optional/
set(CORE_BIN_TOOLS "")
set(USER_BIN_TOOLS "")

foreach (BIN ${BIN_TOOL_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/apps/bin/${BIN}/service.conf")
    set(IS_CORE ${PACK_BIN_TO_ROOTFS})  # 使用全局配置作为默认值

    if (EXISTS ${SVC_CONF})
        parse_service_conf("${SVC_CONF}" "SVC")
        if (DEFINED SVC_CORE)
            set(IS_CORE ${SVC_CORE})  # service.conf 覆盖默认值
        endif ()
    endif ()

    if (IS_CORE)
        list(APPEND CORE_BIN_TOOLS "${BIN}")
    else ()
        list(APPEND USER_BIN_TOOLS "${BIN}")
    endif ()
endforeach ()

set(CORE_BIN_TOOLS "${CORE_BIN_TOOLS}" CACHE INTERNAL "Core bin tools (in rootfs)")
set(USER_BIN_TOOLS "${USER_BIN_TOOLS}" CACHE INTERNAL "User bin tools (in optional)")

message(STATUS "[user] Core services: ${CORE_SERVICES}")
message(STATUS "[user] Core bin tools: ${CORE_BIN_TOOLS}")
message(STATUS "[user] User bin tools: ${USER_BIN_TOOLS}")
message(STATUS "[user] User services: ${USER_SERVICES}")

# 拓扑排序核心服务
set(SORTED_CORE "")
set(REMAINING ${CORE_SERVICES})

while (REMAINING)
    set(FOUND FALSE)
    foreach (SVC ${REMAINING})
        set(DEPS_MET TRUE)
        if (SVC_${SVC}_AFTER)
            string(REPLACE " " ";" AFTER_LIST "${SVC_${SVC}_AFTER}")
            foreach (DEP ${AFTER_LIST})
                list(FIND SORTED_CORE ${DEP} DEP_IDX)
                if (DEP_IDX EQUAL -1)
                    list(FIND CORE_SERVICES ${DEP} IS_CORE)
                    if (NOT IS_CORE EQUAL -1)
                        set(DEPS_MET FALSE)
                        break()
                    endif ()
                endif ()
            endforeach ()
        endif ()

        if (DEPS_MET)
            list(APPEND SORTED_CORE ${SVC})
            list(REMOVE_ITEM REMAINING ${SVC})
            set(FOUND TRUE)
            break()
        endif ()
    endforeach ()

    if (NOT FOUND AND REMAINING)
        list(GET REMAINING 0 FIRST)
        list(APPEND SORTED_CORE ${FIRST})
        list(REMOVE_ITEM REMAINING ${FIRST})
    endif ()
endwhile ()

set(CORE_SERVICES ${SORTED_CORE})
message(STATUS "[user] Sorted core: ${CORE_SERVICES}")

# ============================================
# 无需生成头文件
# ============================================
# 核心服务配置：user/core_services.conf -> initramfs:/etc/core_services.conf
# 用户服务配置：user/user_services.conf -> system.img:/etc/user_services.conf
# init 通过 ramfs/VFS 直接读取配置文件，不再使用嵌入式头文件

# ============================================
# 用户服务配置
# ============================================
# 用户服务配置已改为手写配置文件：user/user_services.conf
# 不再从 service.conf 自动生成（简化构建流程）
#
# 注意：保留 generated 目录，以防其他地方依赖
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

# ============================================
# init.elf（依赖生成的头文件）
# ============================================
add_executable(init.elf
        libs/libc/src/crt0.S
        init/main.c
        init/early_console.c
        init/ini_parser.c
        init/svc_manager.c
        init/svc/svc_config.c
        init/svc/svc_handles.c
        init/svc/svc_graph.c
        init/svc/svc_runtime.c
        init/svc/svc_scheduler.c
        init/svc/svc_notify.c
        init/ramfs.c
        init/initramfs_tar.c
        init/ramfsd_service.c
        init/bootstrap/exec.c
        init/bootstrap/fat32_reader.c
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libd/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libvfs/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libpthread/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/init
)
target_link_libraries(init.elf PRIVATE c d vfs pthread serial)
target_link_options(init.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/libs/user.ld -nostdlib
)

add_custom_command(TARGET init.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_BINARY_DIR}/init.elf
)

# ============================================
# 导出给 iso 模块
# ============================================
set(CORE_SERVICES "${CORE_SERVICES}" CACHE INTERNAL "Core services list")
set(USER_SERVICES "${USER_SERVICES}" CACHE INTERNAL "User services list")
set(INIT_ELF "${CMAKE_BINARY_DIR}/init.elf" CACHE INTERNAL "Init ELF path")
