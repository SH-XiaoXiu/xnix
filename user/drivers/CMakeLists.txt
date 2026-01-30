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

option(XNIX_BUILD_DRIVERS "Build UDM drivers" ON)

if (NOT XNIX_BUILD_DRIVERS)
    message(STATUS "UDM drivers disabled")
    set(DRIVER_TARGETS "" CACHE INTERNAL "List of driver targets" FORCE)
    set(DRIVER_NAMES "" CACHE INTERNAL "List of driver names" FORCE)
    return()
endif ()

# 自动发现所有驱动子目录
file(GLOB DRIVER_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_DRIVER_TARGETS "")
set(_DRIVER_NAMES "")

foreach (DRIVER_DIR ${DRIVER_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR})
        continue()
    endif ()

    # 检查是否有 main.c
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/main.c)
        message(STATUS "Skipping driver ${DRIVER_DIR}: no main.c")
        continue()
    endif ()

    # 模块名（大写用于选项）
    string(TOUPPER ${DRIVER_DIR} DRIVER_UPPER)

    # 创建选项，默认 ON
    option(XNIX_DRIVER_${DRIVER_UPPER} "Build driver: ${DRIVER_DIR}" ON)

    if (NOT XNIX_DRIVER_${DRIVER_UPPER})
        message(STATUS "Skipping driver ${DRIVER_DIR}: disabled by option")
        continue()
    endif ()

    set(DRIVER_NAME ${DRIVER_DIR})
    set(DRIVER_ELF ${DRIVER_NAME}.elf)

    message(STATUS "Found driver: ${DRIVER_NAME}")

    # 收集该驱动目录下所有源文件
    file(GLOB DRIVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/*.c)

    # 创建可执行文件
    add_executable(${DRIVER_ELF}
            ${CMAKE_SOURCE_DIR}/user/libs/libc/src/crt0.s
            ${DRIVER_SOURCES}
    )

    target_compile_options(${DRIVER_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(${DRIVER_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libudm/include
            ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(${DRIVER_ELF} PRIVATE c udm)
    target_link_options(${DRIVER_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/user.ld -nostdlib
    )

    # 复制到 build 目录
    add_custom_command(TARGET ${DRIVER_ELF} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${DRIVER_ELF}> ${CMAKE_BINARY_DIR}/${DRIVER_ELF}
    )

    list(APPEND _DRIVER_TARGETS ${DRIVER_ELF})
    list(APPEND _DRIVER_NAMES ${DRIVER_NAME})
endforeach ()

# 导出变量
set(DRIVER_TARGETS "${_DRIVER_TARGETS}" CACHE INTERNAL "List of driver targets" FORCE)
set(DRIVER_NAMES "${_DRIVER_NAMES}" CACHE INTERNAL "List of driver names" FORCE)
set(DRIVER_TARGETS "${_DRIVER_TARGETS}" PARENT_SCOPE)
set(DRIVER_NAMES "${_DRIVER_NAMES}" PARENT_SCOPE)

message(STATUS "Driver targets: ${_DRIVER_TARGETS}")
