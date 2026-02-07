# ============================================
# Xnix 应用程序构建模板
# ============================================
#
# 用法：在服务/应用目录下创建 CMakeLists.txt
#
# set(APP_NAME "myapp")
# set(APP_SOURCES main.c foo.c)
# set(APP_LIBS c d vfs pthread)  # 可选，默认 c d vfs pthread
# set(APP_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)  # 可选
# set(APP_INSTALL_TO_SYSTEM ON)  # 可选，默认 ON（打包到 system.img）
# include(${CMAKE_SOURCE_DIR}/user/app.cmake)
#
# ============================================

# 检查必需参数
if (NOT DEFINED APP_NAME)
    message(FATAL_ERROR "APP_NAME not defined")
endif ()

if (NOT DEFINED APP_SOURCES)
    message(FATAL_ERROR "APP_SOURCES not defined")
endif ()

# 默认值
if (NOT DEFINED APP_LIBS)
    set(APP_LIBS c d vfs pthread)
endif ()

if (NOT DEFINED APP_INSTALL_TO_SYSTEM)
    set(APP_INSTALL_TO_SYSTEM ON)
endif ()

# 创建可执行文件
add_executable(${APP_NAME}.elf
        $<TARGET_OBJECTS:crt0>
        ${APP_SOURCES}
)

# 编译选项
target_compile_options(${APP_NAME}.elf PRIVATE ${USER_C_FLAGS})

# 包含目录
set(APP_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 如果有额外的包含目录
if (DEFINED APP_INCLUDES)
    list(APPEND APP_INCLUDE_DIRS ${APP_INCLUDES})
endif ()

target_include_directories(${APP_NAME}.elf PRIVATE ${APP_INCLUDE_DIRS})

# 链接库
target_link_libraries(${APP_NAME}.elf PRIVATE ${APP_LIBS})

# 链接选项
target_link_options(${APP_NAME}.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

# 复制到 build 目录
add_custom_command(TARGET ${APP_NAME}.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${APP_NAME}.elf> ${CMAKE_BINARY_DIR}/${APP_NAME}.elf
)

# 根据 APP_INSTALL_TO_SYSTEM 决定打包策略
if (APP_INSTALL_TO_SYSTEM)
    # 判断安装目录（根据源码位置）
    get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
    get_filename_component(CATEGORY ${PARENT_DIR} NAME)

    if (CATEGORY STREQUAL "drivers" OR CATEGORY STREQUAL "servers")
        set(INSTALL_SUBDIR "sbin")
    elseif (CATEGORY STREQUAL "apps")
        set(INSTALL_SUBDIR "bin")
    else ()
        set(INSTALL_SUBDIR "bin")
    endif ()

    # 添加到全局列表，供 iso 模块使用
    set(SYSTEM_IMG_FILES ${SYSTEM_IMG_FILES}
            "${CMAKE_BINARY_DIR}/${APP_NAME}.elf:/${INSTALL_SUBDIR}/${APP_NAME}.elf"
            CACHE INTERNAL "Files to package into system.img"
    )
else ()
    # 输出到 optional 目录
    add_custom_command(TARGET ${APP_NAME}.elf POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/optional
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${APP_NAME}.elf> ${CMAKE_BINARY_DIR}/optional/${APP_NAME}.elf
    )
endif ()

# 清理变量，避免影响其他应用
unset(APP_NAME)
unset(APP_SOURCES)
unset(APP_LIBS)
unset(APP_INCLUDES)
unset(APP_INCLUDE_DIRS)
unset(APP_INSTALL_TO_SYSTEM)
unset(INSTALL_SUBDIR)
unset(CATEGORY)
unset(PARENT_DIR)
