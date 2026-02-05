# 独立工具程序构建
#
# 此目录下的程序不会被打包进 ISO，编译输出到 ${CMAKE_BINARY_DIR}/bin/
#
# 使用方法:
#   1. 在 bin/ 下创建子目录，如 bin/mytool/
#   2. 在子目录中放入 main.c
#   3. 重新 cmake，自动编译到 build/bin/
#
# 构建命令:
#   make bin_tools    # 只构建 bin 工具
#   make              # 默认构建包含 bin 工具
#

option(XNIX_BUILD_BIN "Build bin tools" ON)

if (NOT XNIX_BUILD_BIN)
    message(STATUS "Bin tools disabled")
    return()
endif ()

# 输出目录
set(BIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${BIN_OUTPUT_DIR})

# 收集所有 bin 目标
set(_BIN_TARGETS "")
set(_BIN_NAMES "")

# 自动发现所有子目录
file(GLOB BIN_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach (BIN_DIR ${BIN_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_DIR})
        continue()
    endif ()

    # 检查是否有 main.c
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_DIR}/main.c)
        message(STATUS "Skipping bin tool ${BIN_DIR}: no main.c")
        continue()
    endif ()

    # 模块名（大写用于选项）
    string(TOUPPER ${BIN_DIR} BIN_UPPER)

    # 创建选项，默认 ON
    option(XNIX_BIN_${BIN_UPPER} "Build bin tool: ${BIN_DIR}" ON)

    if (NOT XNIX_BIN_${BIN_UPPER})
        message(STATUS "Skipping bin tool ${BIN_DIR}: disabled by option")
        continue()
    endif ()

    set(BIN_NAME ${BIN_DIR})
    set(BIN_ELF ${BIN_NAME}.elf)

    message(STATUS "Found bin tool: ${BIN_NAME}")

    # 收集该目录下所有源文件
    file(GLOB BIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_DIR}/*.c)

    # 创建可执行文件
    add_executable(bin_${BIN_ELF}
            ${CMAKE_SOURCE_DIR}/user/libs/libc/src/crt0.s
            ${BIN_SOURCES}
    )

    target_compile_options(bin_${BIN_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(bin_${BIN_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
            ${CMAKE_SOURCE_DIR}/user/libs/libd/include
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(bin_${BIN_ELF} PRIVATE c)
    if (TARGET pthread)
        target_link_libraries(bin_${BIN_ELF} PRIVATE pthread)
    endif ()
    target_link_options(bin_${BIN_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
    )

    # 输出到 build/bin/ 目录
    set_target_properties(bin_${BIN_ELF} PROPERTIES
            OUTPUT_NAME ${BIN_NAME}
            RUNTIME_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR}
            SUFFIX ".elf"
    )

    list(APPEND _BIN_TARGETS bin_${BIN_ELF})
    list(APPEND _BIN_NAMES ${BIN_NAME})
endforeach ()

# 创建聚合目标
if (_BIN_TARGETS)
    add_custom_target(bin_tools ALL DEPENDS ${_BIN_TARGETS})
    message(STATUS "Bin tools target: bin_tools (${_BIN_TARGETS})")
endif ()

# 导出 bin 工具列表（供 iso 模块使用）
set(BIN_TOOL_TARGETS "${_BIN_TARGETS}" CACHE INTERNAL "Bin tool targets")
set(BIN_TOOL_NAMES "${_BIN_NAMES}" CACHE INTERNAL "Bin tool names")
set(BIN_TOOL_OUTPUT_DIR "${BIN_OUTPUT_DIR}" CACHE INTERNAL "Bin tool output directory")

# 为每个 bin 工具生成服务配置（遵守 ISO 规范）
# 默认 core=false，可通过 bin/xxx/service.conf 覆盖
foreach (BIN_NAME ${_BIN_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/${BIN_NAME}/service.conf")
    if (EXISTS ${SVC_CONF})
        # 有 service.conf，由 user/CMakeLists.txt 处理
        continue()
    endif ()
    # 自动生成默认配置
    set(SVC_bin_${BIN_NAME}_TYPE "bin" CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_CORE FALSE CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_PATH "/sys/bin/${BIN_NAME}.elf" CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_AFTER "" CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_HANDLES "" CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_MOUNT "" CACHE INTERNAL "")
    set(SVC_bin_${BIN_NAME}_RESPAWN FALSE CACHE INTERNAL "")
endforeach ()
