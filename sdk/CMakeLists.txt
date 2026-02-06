# Xnix SDK - 公共 ABI 和运行时库
#
# 该 SDK 提供了内核与用户空间之间的稳定接口。
# 组件包括：
#   - 公共 ABI 头文件（系统调用、句柄、IPC 等）
#   - 运行时启动代码 (crt0.s)
#   - 系统调用包装库 (libxnix.a)

cmake_minimum_required(VERSION 3.16)

# SDK 头文件（无需编译，仅头文件）
set(SDK_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/errno.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/framebuffer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/handle.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/ipc.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/perm.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/process.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/stdint.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/syscall.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/abi/types.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/driver/ioport.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix/driver/irq.h
)

# 创建 SDK 库目标（接口库 - 仅头文件）
add_library(xnix_sdk INTERFACE)

target_include_directories(xnix_sdk INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 安装 SDK 头文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/xnix
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")

# 安装启动代码
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/lib/crt0.S
        DESTINATION lib)

# 导出目标供其他项目使用
export(TARGETS xnix_sdk
        FILE ${CMAKE_CURRENT_BINARY_DIR}/xnix-sdk-targets.cmake)

message(STATUS "[SDK] 公共 ABI 头文件: ${SDK_HEADERS}")
message(STATUS "[SDK] 包含目录: ${CMAKE_CURRENT_SOURCE_DIR}/include")
