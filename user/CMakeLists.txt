# 用户态程序构建
#
# 目录结构:
#   libs/       - 用户态库 (libc, libpthread, libudm)
#   init/       - init 进程
#   drivers/    - UDM 驱动 (动态扫描)
#   apps/       - 应用程序 (动态扫描)
#   demos/      - 演示程序 (动态扫描)
#
# 全局开关:
#   -DXNIX_BUILD_DRIVERS=OFF    禁用所有 UDM 驱动
#   -DXNIX_BUILD_APPS=OFF       禁用所有应用
#   -DXNIX_BUILD_DEMOS=OFF      禁用所有 demo
#
# 单独模块开关 (默认 ON):
#   -DXNIX_DRIVER_<NAME>=OFF    禁用指定驱动
#   -DXNIX_APP_<NAME>=OFF       禁用指定应用
#   -DXNIX_DEMO_<NAME>=OFF      禁用指定 demo
#

# 通用编译选项
set(USER_C_FLAGS
        -m32 -march=i386
        -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
        -Wall -Wextra
        -g
)

# 确保 include 目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)

# ============================================
# 第三方库
# ============================================
add_subdirectory(tplibs)

# ============================================
# 用户态库
# ============================================
add_subdirectory(libs)

# ============================================
# UDM 驱动 (动态扫描，生成 module_index.h)
# 必须在 init 之前处理，因为 init 依赖 module_index.h
# ============================================
add_subdirectory(drivers)

# ============================================
# init.elf
# ============================================
add_executable(init.elf
        libs/libc/src/crt0.s
        init/main.c
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libc/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(init.elf PRIVATE c)
target_link_options(init.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/libs/user.ld -nostdlib
)

add_custom_command(TARGET init.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_BINARY_DIR}/init.elf
)

# ============================================
# 应用程序 (动态扫描)
# ============================================
add_subdirectory(apps)

# ============================================
# Demo 程序 (动态扫描)
# ============================================
add_subdirectory(demos)

# 传递变量给父级
set(DRIVER_TARGETS "${DRIVER_TARGETS}" PARENT_SCOPE)
set(DRIVER_NAMES "${DRIVER_NAMES}" PARENT_SCOPE)
set(APP_TARGETS "${APP_TARGETS}" PARENT_SCOPE)
set(APP_NAMES "${APP_NAMES}" PARENT_SCOPE)
set(DEMO_TARGETS "${DEMO_TARGETS}" PARENT_SCOPE)
set(DEMO_NAMES "${DEMO_NAMES}" PARENT_SCOPE)
