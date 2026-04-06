# ps2 - PS/2 keyboard + mouse unified driver

set(APP_NAME "ps2")
set(APP_SOURCES
        main.c
        ../ps2/scancode.c
        ../ps2/ps2_mouse.c
)
set(APP_LIBS c driver pthread)
set(APP_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../ps2
)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
