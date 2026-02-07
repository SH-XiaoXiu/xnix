# installer 应用程序

set(INSTALLER_SOURCES
    main.c
)

add_executable(installer.elf
    $<TARGET_OBJECTS:crt0>
    ${INSTALLER_SOURCES}
)

target_compile_options(installer.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(installer.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk/include
    ${CMAKE_SOURCE_DIR}/user/libs/libc/include
    ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
    ${CMAKE_SOURCE_DIR}/user/libs/libd/include
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(installer.elf PRIVATE
    c
    vfs
    -Wl,--whole-archive serial -Wl,--no-whole-archive
)

target_link_options(installer.elf PRIVATE
    -m32
    -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld
    -nostdlib
)

set_target_properties(installer.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
