# 服务器程序构建
#
# 所有服务使用自己的 CMakeLists.txt 管理构建和元数据

# 自动发现所有服务子目录
file(GLOB SERVER_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach (SERVER_DIR ${SERVER_DIRS})
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR})
        continue()
    endif ()

    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SERVER_DIR}/CMakeLists.txt)
        add_subdirectory(${SERVER_DIR})
    endif ()
endforeach ()
