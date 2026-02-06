# UDM 驱动自动构建
#
# 使用方法:
#   1. 在 drivers/ 下创建子目录，如 drivers/mydriver/
#   2. 在子目录中放入 main.c
#   3. 重新 cmake，自动编译并加入 ISO
#
# CMake 选项:
#   -DXNIX_BUILD_DRIVERS=OFF        禁用所有驱动
#   -DXNIX_DRIVER_<NAME>=OFF        禁用指定驱动 (如 -DXNIX_DRIVER_SERIALD=OFF)
#

option(XNIX_BUILD_SERVERS "Build Server modules" ON)

if (NOT XNIX_BUILD_SERVERS)
    message(STATUS "Server modules disabled")
    set(SERVER_TARGETS "" CACHE INTERNAL "List of server targets" FORCE)
    set(SERVER_NAMES "" CACHE INTERNAL "List of server names" FORCE)
    return()
endif ()

# 自动发现所有服务器子目录
file(GLOB SERVER_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_SERVER_TARGETS "")
set(_SERVER_NAMES "")

foreach (SERVER_DIR ${SERVER_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR})
        continue()
    endif ()

    # 检查是否有 main.c
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}/main.c)
        message(STATUS "Skipping server ${SERVER_DIR}: no main.c")
        continue()
    endif ()

    # 模块名（大写用于选项）
    string(TOUPPER ${SERVER_DIR} SERVER_UPPER)

    # 创建选项，默认 ON
    option(XNIX_SERVER_${SERVER_UPPER} "Build server: ${SERVER_DIR}" ON)

    if (NOT XNIX_SERVER_${SERVER_UPPER})
        message(STATUS "Skipping server ${SERVER_DIR}: disabled by option")
        continue()
    endif ()

    set(SERVER_NAME ${SERVER_DIR})
    set(SERVER_ELF ${SERVER_NAME}.elf)

    message(STATUS "Found server: ${SERVER_NAME}")

    # 收集该服务器目录下所有源文件
    file(GLOB SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}/*.c)

    # 加载服务器配置（如果存在）
    set(SERVER_EXTRA_LIBS "")
    set(SERVER_EXTRA_SOURCES "")
    set(SERVER_EXTRA_INCLUDES "")
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}/CMakeLists.txt)
        include(${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}/CMakeLists.txt)
    endif ()

    # 创建可执行文件
    add_executable(${SERVER_ELF}
            $<TARGET_OBJECTS:crt0>
            ${SERVER_SOURCES}
            ${SERVER_EXTRA_SOURCES}
    )

    target_compile_options(${SERVER_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(${SERVER_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libd/include
            ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
            ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
            ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
            ${SERVER_EXTRA_INCLUDES}
    )
    # 链接库
    target_link_libraries(${SERVER_ELF} PRIVATE c d vfs pthread ${SERVER_EXTRA_LIBS})

    target_link_options(${SERVER_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
    )

    # 复制到 build 目录
    add_custom_command(TARGET ${SERVER_ELF} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${SERVER_ELF}> ${CMAKE_BINARY_DIR}/${SERVER_ELF}
    )

    list(APPEND _SERVER_TARGETS ${SERVER_ELF})
    list(APPEND _SERVER_NAMES ${SERVER_NAME})
endforeach ()

# 按字母序排序（与 GLOB 结果一致）
list(SORT _SERVER_NAMES)
list(SORT _SERVER_TARGETS)

# 导出变量
set(SERVER_TARGETS "${_SERVER_TARGETS}" CACHE INTERNAL "List of server targets" FORCE)
set(SERVER_NAMES "${_SERVER_NAMES}" CACHE INTERNAL "List of server names" FORCE)
set(SERVER_TARGETS "${_SERVER_TARGETS}" PARENT_SCOPE)
set(SERVER_NAMES "${_SERVER_NAMES}" PARENT_SCOPE)

message(STATUS "Server targets: ${_SERVER_TARGETS}")

# module_index.h 现在由 cmake/generate_services.cmake 自动生成
