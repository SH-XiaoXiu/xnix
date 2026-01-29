project(libpthread C)

add_library(pthread STATIC
        src/pthread.c
        src/pthread_attr.c
        src/pthread_mutex.c
)

target_compile_options(pthread PRIVATE ${USER_C_FLAGS})

target_include_directories(pthread PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(pthread PUBLIC c)

