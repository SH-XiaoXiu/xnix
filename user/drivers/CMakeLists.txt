# UDM 驱动自动构建
#
# 使用方法:
#   1. 在 drivers/ 下创建子目录，如 drivers/mydriver/
#   2. 在子目录中放入 main.c
#   3. 重新 cmake，自动编译并加入 ISO/rootfs
#

option(XNIX_BUILD_DRIVERS "Build Driver modules" ON)

if (NOT XNIX_BUILD_DRIVERS)
    message(STATUS "Driver modules disabled")
    set(DRIVER_TARGETS "" CACHE INTERNAL "List of driver targets" FORCE)
    set(DRIVER_NAMES "" CACHE INTERNAL "List of driver names" FORCE)
    return()
endif ()

# 自动发现所有驱动子目录
file(GLOB DRIVER_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_DRIVER_TARGETS "")
set(_DRIVER_NAMES "")

foreach (DRIVER_DIR ${DRIVER_DIRS})
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR})
        continue()
    endif ()

    # If driver has its own CMakeLists.txt, use add_subdirectory
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/CMakeLists.txt)
        message(STATUS "Found driver with CMakeLists.txt: ${DRIVER_DIR}")
        add_subdirectory(${DRIVER_DIR})
        list(APPEND _DRIVER_TARGETS ${DRIVER_DIR}.elf)
        list(APPEND _DRIVER_NAMES ${DRIVER_DIR})
        continue()
    endif ()

    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/main.c)
        message(STATUS "Skipping driver ${DRIVER_DIR}: no main.c")
        continue()
    endif ()

    string(TOUPPER ${DRIVER_DIR} DRIVER_UPPER)
    option(XNIX_DRIVER_${DRIVER_UPPER} "Build driver: ${DRIVER_DIR}" ON)

    if (NOT XNIX_DRIVER_${DRIVER_UPPER})
        message(STATUS "Skipping driver ${DRIVER_DIR}: disabled by option")
        continue()
    endif ()

    set(DRIVER_NAME ${DRIVER_DIR})
    set(DRIVER_ELF ${DRIVER_NAME}.elf)

    message(STATUS "Found driver: ${DRIVER_NAME}")

    file(GLOB DRIVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/*.c)

    set(SERVER_EXTRA_LIBS "")
    set(SERVER_EXTRA_SOURCES "")
    set(SERVER_EXTRA_INCLUDES "")

    if (DRIVER_NAME STREQUAL "fatfsd")
        set(SERVER_EXTRA_LIBS fatfs)
    elseif (DRIVER_NAME STREQUAL "rootfsd")
        set(SERVER_EXTRA_SOURCES
                ${CMAKE_SOURCE_DIR}/user/drivers/fatfsd/fatfs_vfs.c
        )
        set(SERVER_EXTRA_INCLUDES
                ${CMAKE_SOURCE_DIR}/user/drivers/fatfsd
        )
        set(SERVER_EXTRA_LIBS fatfs)
    endif ()

    add_executable(${DRIVER_ELF}
            ${CMAKE_SOURCE_DIR}/user/libs/libc/src/crt0.s
            ${DRIVER_SOURCES}
            ${SERVER_EXTRA_SOURCES}
    )

    target_compile_options(${DRIVER_ELF} PRIVATE ${USER_C_FLAGS})
    target_include_directories(${DRIVER_ELF} PRIVATE
            ${CMAKE_SOURCE_DIR}/user/libs/libc/include
            ${CMAKE_SOURCE_DIR}/user/libs/libd/include
            ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
            ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
            ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}
            ${CMAKE_SOURCE_DIR}/main/include
            ${CMAKE_BINARY_DIR}/include
            ${SERVER_EXTRA_INCLUDES}
    )
    target_link_libraries(${DRIVER_ELF} PRIVATE c d vfs pthread ${SERVER_EXTRA_LIBS})

    target_link_options(${DRIVER_ELF} PRIVATE
            -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
    )

    add_custom_command(TARGET ${DRIVER_ELF} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${DRIVER_ELF}> ${CMAKE_BINARY_DIR}/${DRIVER_ELF}
    )

    list(APPEND _DRIVER_TARGETS ${DRIVER_ELF})
    list(APPEND _DRIVER_NAMES ${DRIVER_NAME})
endforeach ()

list(SORT _DRIVER_NAMES)
list(SORT _DRIVER_TARGETS)

set(DRIVER_TARGETS "${_DRIVER_TARGETS}" CACHE INTERNAL "List of driver targets" FORCE)
set(DRIVER_NAMES "${_DRIVER_NAMES}" CACHE INTERNAL "List of driver names" FORCE)
set(DRIVER_TARGETS "${_DRIVER_TARGETS}" PARENT_SCOPE)
set(DRIVER_NAMES "${_DRIVER_NAMES}" PARENT_SCOPE)

message(STATUS "Driver targets: ${_DRIVER_TARGETS}")
