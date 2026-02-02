# Demo 程序自动构建
#
# 使用方法:
#   1. 在 demos/ 下创建子目录，如 demos/mytest/
#   2. 在子目录中放入 main.c
#   3. 重新 cmake，自动编译并加入 ISO
#
# CMake 选项:
#   -DXNIX_BUILD_DEMOS=ON         启用 demo 构建
#   -DXNIX_DEMO_<NAME>=OFF        禁用指定 demo (如 -DXNIX_DEMO_HELLO=OFF)
#

option(XNIX_BUILD_DEMOS "Build demo programs" ON)

if (NOT XNIX_BUILD_DEMOS)
    message(STATUS "Demo programs disabled")
    set(DEMO_TARGETS "" CACHE INTERNAL "List of demo targets" FORCE)
    set(DEMO_NAMES "" CACHE INTERNAL "List of demo names" FORCE)
    # 生成空的 demo_modules.h
    file(WRITE ${CMAKE_BINARY_DIR}/include/demo_modules.h
            "/* No demos */\n#ifndef _DEMO_MODULES_H\n#define _DEMO_MODULES_H\n#define DEMO_COUNT 0\nstatic const char *demo_names[] = {};\n#endif\n")
    return()
endif ()

# 自动发现所有 demo 子目录
file(GLOB DEMO_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_DEMO_TARGETS "")
set(_DEMO_NAMES "")

foreach (DEMO_DIR ${DEMO_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DEMO_DIR})
        continue()
    endif ()

    # 检查是否有 main.c
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DEMO_DIR}/main.c)
        message(STATUS "Skipping ${DEMO_DIR}: no main.c")
        continue()
    endif ()

    set(DEMO_NAME ${DEMO_DIR})
    set(DEMO_ELF ${DEMO_NAME}.elf)

    message(STATUS "Found demo: ${DEMO_NAME}")

    # 收集该 demo 目录下所有源文件
    file(GLOB DEMO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${DEMO_DIR}/*.c)

    # 模块名（大写用于选项）
    string(TOUPPER ${DEMO_DIR} DEMO_UPPER)

    # 创建选项，默认 ON
    option(XNIX_DEMO_${DEMO_UPPER} "Build demo: ${DEMO_DIR}" ON)

    if (NOT XNIX_DEMO_${DEMO_UPPER})
        message(STATUS "Skipping demo ${DEMO_DIR}: disabled by option")
        continue()
    endif ()

    # 创建可执行文件
    add_executable(${DEMO_ELF}
            ${CMAKE_SOURCE_DIR}/user/libs/libc/src/crt0.s
            ${DEMO_SOURCES}
    )

    target_compile_options(${DEMO_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(${DEMO_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
            ${CMAKE_SOURCE_DIR}/user/libs/libudm/include
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(${DEMO_ELF} PRIVATE c)
    if (TARGET pthread)
        target_link_libraries(${DEMO_ELF} PRIVATE pthread)
    endif ()
    target_link_options(${DEMO_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
    )

    # 复制到 build 目录
    add_custom_command(TARGET ${DEMO_ELF} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${DEMO_ELF}> ${CMAKE_BINARY_DIR}/${DEMO_ELF}
    )

    # 记录 demo 信息
    list(APPEND _DEMO_TARGETS ${DEMO_ELF})
    list(APPEND _DEMO_NAMES ${DEMO_NAME})
endforeach ()

# 更新 CACHE 变量（全局可见）
set(DEMO_TARGETS "${_DEMO_TARGETS}" CACHE INTERNAL "List of demo targets" FORCE)
set(DEMO_NAMES "${_DEMO_NAMES}" CACHE INTERNAL "List of demo names" FORCE)

# 同时设置 PARENT_SCOPE（给 user/CMakeLists.txt）
set(DEMO_TARGETS "${_DEMO_TARGETS}" PARENT_SCOPE)
set(DEMO_NAMES "${_DEMO_NAMES}" PARENT_SCOPE)

message(STATUS "Demo targets: ${_DEMO_TARGETS}")

# 生成 demo 模块头文件供 init 使用
set(DEMO_HEADER_CONTENT "/* Auto-generated demo module list */\n")
string(APPEND DEMO_HEADER_CONTENT "#ifndef _DEMO_MODULES_H\n")
string(APPEND DEMO_HEADER_CONTENT "#define _DEMO_MODULES_H\n\n")
string(APPEND DEMO_HEADER_CONTENT "/* Demo module indices (MODULE_DEMO_BASE is defined in module_index.h) */\n\n")

list(LENGTH _DEMO_NAMES DEMO_COUNT)
set(DEMO_INDEX 0)
foreach (NAME ${_DEMO_NAMES})
    string(TOUPPER ${NAME} NAME_UPPER)
    string(APPEND DEMO_HEADER_CONTENT "#define MODULE_DEMO_${NAME_UPPER} (MODULE_DEMO_BASE + ${DEMO_INDEX})\n")
    math(EXPR DEMO_INDEX "${DEMO_INDEX} + 1")
endforeach ()

string(APPEND DEMO_HEADER_CONTENT "\n#define DEMO_COUNT ${DEMO_COUNT}\n")
string(APPEND DEMO_HEADER_CONTENT "\nstatic const char *demo_names[] = {\n")
foreach (NAME ${_DEMO_NAMES})
    string(APPEND DEMO_HEADER_CONTENT "    \"${NAME}\",\n")
endforeach ()
string(APPEND DEMO_HEADER_CONTENT "};\n")
string(APPEND DEMO_HEADER_CONTENT "\n#endif /* _DEMO_MODULES_H */\n")

file(WRITE ${CMAKE_BINARY_DIR}/include/demo_modules.h "${DEMO_HEADER_CONTENT}")
message(STATUS "Generated demo_modules.h with ${DEMO_COUNT} demos")
