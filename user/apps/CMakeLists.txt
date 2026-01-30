# 应用程序自动构建
#
# 使用方法:
#   1. 在 apps/ 下创建子目录，如 apps/myapp/
#   2. 在子目录中放入 main.c
#   3. 重新 cmake，自动编译并加入 ISO
#
# CMake 选项:
#   -DXNIX_BUILD_APPS=OFF        禁用所有应用
#   -DXNIX_APP_<NAME>=OFF        禁用指定应用 (如 -DXNIX_APP_SHELL=OFF)
#

option(XNIX_BUILD_APPS "Build user applications" ON)

if (NOT XNIX_BUILD_APPS)
    message(STATUS "User applications disabled")
    set(APP_TARGETS "" CACHE INTERNAL "List of app targets" FORCE)
    set(APP_NAMES "" CACHE INTERNAL "List of app names" FORCE)
    return()
endif ()

# 自动发现所有应用子目录
file(GLOB APP_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_APP_TARGETS "")
set(_APP_NAMES "")

foreach (APP_DIR ${APP_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${APP_DIR})
        continue()
    endif ()

    # 检查是否有 main.c
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${APP_DIR}/main.c)
        message(STATUS "Skipping app ${APP_DIR}: no main.c")
        continue()
    endif ()

    # 模块名（大写用于选项）
    string(TOUPPER ${APP_DIR} APP_UPPER)

    # 创建选项，默认 ON
    option(XNIX_APP_${APP_UPPER} "Build app: ${APP_DIR}" ON)

    if (NOT XNIX_APP_${APP_UPPER})
        message(STATUS "Skipping app ${APP_DIR}: disabled by option")
        continue()
    endif ()

    set(APP_NAME ${APP_DIR})
    set(APP_ELF ${APP_NAME}.elf)

    message(STATUS "Found app: ${APP_NAME}")

    # 收集该应用目录下所有源文件
    file(GLOB APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${APP_DIR}/*.c)

    # 创建可执行文件
    add_executable(${APP_ELF}
            ${CMAKE_SOURCE_DIR}/user/libs/libc/src/crt0.s
            ${APP_SOURCES}
    )

    target_compile_options(${APP_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(${APP_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
            ${CMAKE_SOURCE_DIR}/user/libs/libudm/include
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(${APP_ELF} PRIVATE c)
    if (TARGET pthread)
        target_link_libraries(${APP_ELF} PRIVATE pthread)
    endif ()
    target_link_options(${APP_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/user.ld -nostdlib
    )

    # 复制到 build 目录
    add_custom_command(TARGET ${APP_ELF} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${APP_ELF}> ${CMAKE_BINARY_DIR}/${APP_ELF}
    )

    list(APPEND _APP_TARGETS ${APP_ELF})
    list(APPEND _APP_NAMES ${APP_NAME})
endforeach ()

# 导出变量
set(APP_TARGETS "${_APP_TARGETS}" CACHE INTERNAL "List of app targets" FORCE)
set(APP_NAMES "${_APP_NAMES}" CACHE INTERNAL "List of app names" FORCE)
set(APP_TARGETS "${_APP_TARGETS}" PARENT_SCOPE)
set(APP_NAMES "${_APP_NAMES}" PARENT_SCOPE)

message(STATUS "App targets: ${_APP_TARGETS}")
