# ps2d - PS/2 keyboard + mouse unified driver

set(APP_NAME "ps2d")
set(APP_SOURCES
        main.c
        ../kbd/scancode.c
        ../moused/ps2.c
)
set(APP_LIBS c driver pthread)
set(APP_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../kbd
        ${CMAKE_CURRENT_SOURCE_DIR}/../moused
)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
