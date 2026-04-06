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

# 每次重新配置时重建 system.img 文件清单，避免重命名后残留旧产物路径
set(SYSTEM_IMG_FILES "" CACHE INTERNAL "Files to package into system.img" FORCE)

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
# 核心服务列表直接硬编码 核心服务是固定的
# ============================================
set(CORE_SERVICES
        serial
        ps2
        # ramfsd - 已内置到 init，不再作为独立服务
        # rootfsd - 已被 system.img 替代，不再需要
        vfs
        fatfs
        display
        fb
        term
        console
        klog
        devfs
        syslog
        ws
        # shell - 移到 /bin/，不是系统服务
)
set(CORE_DRIVERS
        serial
        ps2
        fatfs
        display
        fb
)
set(CORE_SYSTEM_SERVERS
        vfs
        term
        console
        klog
        devfs
        syslog
        ws
)

set(USER_SERVICES "")

# 所有 bin 工具全部打包到 /bin
set(CORE_BIN_TOOLS ${BIN_TOOL_NAMES})
set(USER_BIN_TOOLS "")

set(CORE_BIN_TOOLS "${CORE_BIN_TOOLS}" CACHE INTERNAL "Core bin tools (in /bin)")
set(USER_BIN_TOOLS "" CACHE INTERNAL "User bin tools (in optional)")

message(STATUS "[user] Core services: ${CORE_SERVICES}")
message(STATUS "[user] Core bin tools: ${CORE_BIN_TOOLS}")
message(STATUS "[user] User bin tools: ${USER_BIN_TOOLS}")
message(STATUS "[user] User services: ${USER_SERVICES}")

# ============================================
# 无需生成头文件
# ============================================
# 系统服务配置：user/sys.conf -> initramfs:/etc/sys.conf
# init 从 ramfs 读取统一配置文件
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
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libd/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libvfs/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libpthread/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libproc/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/init
)
target_link_libraries(init.elf PRIVATE c d vfs pthread serial proc)
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
set(CORE_DRIVERS "${CORE_DRIVERS}" CACHE INTERNAL "Core driver list")
set(CORE_SYSTEM_SERVERS "${CORE_SYSTEM_SERVERS}" CACHE INTERNAL "Core server list")
set(USER_SERVICES "${USER_SERVICES}" CACHE INTERNAL "User services list")
set(INIT_ELF "${CMAKE_BINARY_DIR}/init.elf" CACHE INTERNAL "Init ELF path")
