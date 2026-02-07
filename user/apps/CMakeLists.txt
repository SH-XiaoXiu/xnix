# 应用程序构建
#
# 所有应用使用自己的 CMakeLists.txt 管理构建和元数据

# 自动发现所有应用子目录
file(GLOB APP_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach (APP_DIR ${APP_DIRS})
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${APP_DIR})
        continue()
    endif ()

    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${APP_DIR}/CMakeLists.txt)
        add_subdirectory(${APP_DIR})
    endif ()
endforeach ()
