# 用户态库自动构建
#
# 使用方法:
#   1. 在 libs/ 下创建子目录，如 libs/libfoo/
#   2. 在子目录中创建 lib.cmake 和 src/ 目录
#   3. lib.cmake 中定义 LIB_NAME 和 LIB_DEPS
#   4. 重新 cmake，自动编译
#
# lib.cmake 格式:
#   set(LIB_NAME "foo")       # 库名（不带 lib 前缀）
#   set(LIB_DEPS "c")         # 依赖的库列表（空格分隔）
#   set(LIB_KERNEL ON)        # 可选：需要内核头文件（<xnix/*.h>）
#
# CMake 选项:
#   -DXNIX_BUILD_LIBS=OFF        禁用所有用户态库
#   -DXNIX_LIB_<NAME>=OFF        禁用指定库 (如 -DXNIX_LIB_PTHREAD=OFF)
#

option(XNIX_BUILD_LIBS "Build user libraries" ON)

if (NOT XNIX_BUILD_LIBS)
    message(STATUS "User libraries disabled")
    return()
endif ()

# 自动发现所有库子目录
file(GLOB LIB_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

# 第一遍：收集所有库信息
set(_ALL_LIBS "")
foreach (LIB_DIR ${LIB_DIRS})
    # 跳过非目录
    if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR})
        continue()
    endif ()

    # 检查是否有 lib.cmake
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/lib.cmake)
        message(STATUS "Skipping lib ${LIB_DIR}: no lib.cmake")
        continue()
    endif ()

    # 加载配置
    set(LIB_NAME "")
    set(LIB_DEPS "")
    set(LIB_KERNEL OFF)
    set(LIB_EXTRA_INCLUDES "")
    include(${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/lib.cmake)

    if (NOT LIB_NAME)
        message(STATUS "Skipping lib ${LIB_DIR}: LIB_NAME not defined")
        continue()
    endif ()

    # 模块名（大写用于选项）
    string(TOUPPER ${LIB_NAME} LIB_UPPER)

    # 创建选项，默认 ON
    option(XNIX_LIB_${LIB_UPPER} "Build library: ${LIB_NAME}" ON)

    if (NOT XNIX_LIB_${LIB_UPPER})
        message(STATUS "Skipping lib ${LIB_NAME}: disabled by option")
        continue()
    endif ()

    # 保存库信息
    set(_LIB_${LIB_NAME}_DIR ${LIB_DIR})
    set(_LIB_${LIB_NAME}_DEPS ${LIB_DEPS})
    set(_LIB_${LIB_NAME}_KERNEL ${LIB_KERNEL})
    set(_LIB_${LIB_NAME}_EXTRA_INCLUDES ${LIB_EXTRA_INCLUDES})
    list(APPEND _ALL_LIBS ${LIB_NAME})
endforeach ()

# 拓扑排序（简单版本：需要依赖已正确声明）
# 按依赖深度排序
set(_SORTED_LIBS "")
set(_REMAINING ${_ALL_LIBS})

while (_REMAINING)
    set(_ADDED_THIS_ROUND "")
    foreach (LIB ${_REMAINING})
        set(_DEPS_MET TRUE)
        foreach (DEP ${_LIB_${LIB}_DEPS})
            if (NOT DEP IN_LIST _SORTED_LIBS)
                set(_DEPS_MET FALSE)
                break()
            endif ()
        endforeach ()
        if (_DEPS_MET)
            list(APPEND _ADDED_THIS_ROUND ${LIB})
        endif ()
    endforeach ()

    if (NOT _ADDED_THIS_ROUND)
        message(FATAL_ERROR "Circular dependency detected in libs: ${_REMAINING}")
    endif ()

    list(APPEND _SORTED_LIBS ${_ADDED_THIS_ROUND})
    list(REMOVE_ITEM _REMAINING ${_ADDED_THIS_ROUND})
endwhile ()

# 第二遍：按顺序构建库
foreach (LIB_NAME ${_SORTED_LIBS})
    set(LIB_DIR ${_LIB_${LIB_NAME}_DIR})
    set(LIB_DEPS ${_LIB_${LIB_NAME}_DEPS})
    set(LIB_KERNEL ${_LIB_${LIB_NAME}_KERNEL})
    set(LIB_EXTRA_INCLUDES ${_LIB_${LIB_NAME}_EXTRA_INCLUDES})

    message(STATUS "Found lib: ${LIB_NAME} (deps: ${LIB_DEPS})")

    # 收集源文件
    file(GLOB_RECURSE LIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/src/*.c)

    # 创建静态库
    add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

    target_compile_options(${LIB_NAME} PRIVATE ${USER_C_FLAGS})

    # 添加 include 目录（顺序：SDK -> 自己的 -> 依赖的 -> 内核的）
    # SDK include (always available for ABI headers)
    target_include_directories(${LIB_NAME} PUBLIC
            ${CMAKE_SOURCE_DIR}/sdk/include
    )

    # 自己的 include
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/include)
        target_include_directories(${LIB_NAME} PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/include
        )
    endif ()

    # 显式添加依赖库的 include 目录（确保顺序正确）
    foreach (DEP ${LIB_DEPS})
        set(DEP_DIR ${_LIB_${DEP}_DIR})
        if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DEP_DIR}/include)
            target_include_directories(${LIB_NAME} PUBLIC
                    ${CMAKE_CURRENT_SOURCE_DIR}/${DEP_DIR}/include
            )
        endif ()
    endforeach ()

    # 添加额外的 include 目录（仅头文件依赖，不建立链接依赖）
    foreach (EXTRA_INC ${LIB_EXTRA_INCLUDES})
        set(EXTRA_DIR ${_LIB_${EXTRA_INC}_DIR})
        if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXTRA_DIR}/include)
            target_include_directories(${LIB_NAME} PUBLIC
                    ${CMAKE_CURRENT_SOURCE_DIR}/${EXTRA_DIR}/include
            )
        endif ()
    endforeach ()

    # 如果需要内核头文件，添加到最后
    if (LIB_KERNEL)
        target_include_directories(${LIB_NAME} PUBLIC
                ${CMAKE_SOURCE_DIR}/main/include
        )
    endif ()

    # 链接依赖
    if (LIB_DEPS)
        target_link_libraries(${LIB_NAME} PUBLIC ${LIB_DEPS})
    endif ()
endforeach ()

message(STATUS "Lib targets: ${_SORTED_LIBS}")
