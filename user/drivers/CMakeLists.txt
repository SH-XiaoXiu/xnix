# UDM 驱动构建
#
# 所有驱动使用自己的 CMakeLists.txt 管理构建和元数据

# 自动发现所有驱动子目录
file(GLOB DRIVER_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach (DRIVER_DIR ${DRIVER_DIRS})
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR})
        continue()
    endif ()

    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DRIVER_DIR}/CMakeLists.txt)
        add_subdirectory(${DRIVER_DIR})
    endif ()
endforeach ()
