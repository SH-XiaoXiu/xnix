# test_gui - GUI 演示应用

set(APP_NAME "test_gui")
set(APP_SOURCES main.c)
set(APP_LIBS c ws font)
set(APP_INCLUDES
        ${CMAKE_SOURCE_DIR}/user/libs/libws/include
        ${CMAKE_SOURCE_DIR}/user/libs/libfont/include
)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
