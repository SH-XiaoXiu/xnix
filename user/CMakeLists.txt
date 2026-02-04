# 用户态程序构建
#
# 职责：
#   - 编译所有用户态程序（drivers, apps, demos, init）
#   - 发现服务配置（service.conf）
#   - 生成 init 所需的头文件（module_index.h, core_services.h）
#   - 导出 CORE_SERVICES / USER_SERVICES 供 iso 模块使用
#
# 规范：
#   - 每个驱动/应用目录可包含 service.conf
#   - core=true 表示核心服务（作为 Multiboot 模块启动）
#   - core=false/默认 表示用户服务（从文件系统加载）
#

# 通用编译选项
set(USER_C_FLAGS
        -m32 -march=i386
        -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector
        -Wall -Wextra
        -g
)

# 确保 include 目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

# ============================================
# 第三方库
# ============================================
add_subdirectory(tplibs)

# ============================================
# 用户态库
# ============================================
add_subdirectory(libs)

# ============================================
# Driver 模块 (UDM 驱动)
# ============================================
add_subdirectory(drivers)

# ============================================
# Server 模块 (系统服务)
# ============================================
add_subdirectory(servers)

# ============================================
# 应用程序
# ============================================
add_subdirectory(apps)

# ============================================
# Demo 程序
# ============================================
add_subdirectory(demos)

# ============================================
# 服务发现与配置生成
# ============================================
# 解析 service.conf 的函数
function(parse_service_conf FILE_PATH OUT_PREFIX)
    if (NOT EXISTS ${FILE_PATH})
        return()
    endif ()

    file(STRINGS ${FILE_PATH} LINES)

    set(${OUT_PREFIX}_CORE FALSE PARENT_SCOPE)
    set(${OUT_PREFIX}_AFTER "" PARENT_SCOPE)
    set(${OUT_PREFIX}_CAPS "" PARENT_SCOPE)
    set(${OUT_PREFIX}_MOUNT "" PARENT_SCOPE)
    set(${OUT_PREFIX}_RESPAWN FALSE PARENT_SCOPE)
    set(${OUT_PREFIX}_PATH "" PARENT_SCOPE)
    set(${OUT_PREFIX}_NO_READY_FILE FALSE PARENT_SCOPE)
    set(${OUT_PREFIX}_PROVIDES "" PARENT_SCOPE)
    set(${OUT_PREFIX}_REQUIRES "" PARENT_SCOPE)
    set(${OUT_PREFIX}_WANTS "" PARENT_SCOPE)

    foreach (LINE ${LINES})
        string(REGEX MATCH "^[ \t]*#" IS_COMMENT "${LINE}")
        if (IS_COMMENT)
            continue()
        endif ()
        string(STRIP "${LINE}" LINE)
        if ("${LINE}" STREQUAL "")
            continue()
        endif ()

        string(REGEX MATCH "^([a-z_]+)[ \t]*=[ \t]*(.*)$" MATCH "${LINE}")
        if (MATCH)
            set(KEY "${CMAKE_MATCH_1}")
            set(VALUE "${CMAKE_MATCH_2}")
            string(STRIP "${VALUE}" VALUE)

            if (KEY STREQUAL "core" AND VALUE STREQUAL "true")
                set(${OUT_PREFIX}_CORE TRUE PARENT_SCOPE)
            elseif (KEY STREQUAL "after")
                set(${OUT_PREFIX}_AFTER "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "caps")
                set(${OUT_PREFIX}_CAPS "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "mount")
                set(${OUT_PREFIX}_MOUNT "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "respawn" AND VALUE STREQUAL "true")
                set(${OUT_PREFIX}_RESPAWN TRUE PARENT_SCOPE)
            elseif (KEY STREQUAL "no_ready_file" AND VALUE STREQUAL "true")
                set(${OUT_PREFIX}_NO_READY_FILE TRUE PARENT_SCOPE)
            elseif (KEY STREQUAL "path")
                set(${OUT_PREFIX}_PATH "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "profile")
                set(${OUT_PREFIX}_PROFILE "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "handles")
                set(${OUT_PREFIX}_HANDLES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "provides")
                set(${OUT_PREFIX}_PROVIDES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "requires")
                set(${OUT_PREFIX}_REQUIRES "${VALUE}" PARENT_SCOPE)
            elseif (KEY STREQUAL "wants")
                set(${OUT_PREFIX}_WANTS "${VALUE}" PARENT_SCOPE)
            endif ()
        endif ()
    endforeach ()
endfunction()

# 收集服务
set(CORE_SERVICES "")
set(USER_SERVICES "")

# 扫描 Drivers
foreach (DRIVER ${DRIVER_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/drivers/${DRIVER}/service.conf")
    parse_service_conf("${SVC_CONF}" "SVC")

    if (SVC_CORE)
        list(APPEND CORE_SERVICES "${DRIVER}")
        set(SVC_${DRIVER}_CORE TRUE CACHE INTERNAL "")
    else ()
        list(APPEND USER_SERVICES "${DRIVER}")
        set(SVC_${DRIVER}_CORE FALSE CACHE INTERNAL "")
    endif ()

    set(SVC_${DRIVER}_TYPE "driver" CACHE INTERNAL "")
    set(SVC_${DRIVER}_AFTER "${SVC_AFTER}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_MOUNT "${SVC_MOUNT}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_RESPAWN "${SVC_RESPAWN}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_NO_READY_FILE "${SVC_NO_READY_FILE}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_PROFILE "${SVC_PROFILE}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_PROVIDES "${SVC_PROVIDES}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_REQUIRES "${SVC_REQUIRES}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_WANTS "${SVC_WANTS}" CACHE INTERNAL "")
    set(SVC_${DRIVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    if (SVC_PATH)
        set(SVC_${DRIVER}_PATH "${SVC_PATH}" CACHE INTERNAL "")
    else ()
        set(SVC_${DRIVER}_PATH "/sys/drivers/${DRIVER}.elf" CACHE INTERNAL "")
    endif ()
endforeach ()

# 扫描 Servers
foreach (SERVER ${SERVER_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/servers/${SERVER}/service.conf")
    parse_service_conf("${SVC_CONF}" "SVC")

    if (SVC_CORE)
        list(APPEND CORE_SERVICES "${SERVER}")
        set(SVC_${SERVER}_CORE TRUE CACHE INTERNAL "")
    else ()
        list(APPEND USER_SERVICES "${SERVER}")
        set(SVC_${SERVER}_CORE FALSE CACHE INTERNAL "")
    endif ()

    set(SVC_${SERVER}_TYPE "server" CACHE INTERNAL "")
    set(SVC_${SERVER}_AFTER "${SVC_AFTER}" CACHE INTERNAL "")
    set(SVC_${SERVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_MOUNT "${SVC_MOUNT}" CACHE INTERNAL "")
    set(SVC_${SERVER}_RESPAWN "${SVC_RESPAWN}" CACHE INTERNAL "")
    set(SVC_${SERVER}_NO_READY_FILE "${SVC_NO_READY_FILE}" CACHE INTERNAL "")
    set(SVC_${SERVER}_PROFILE "${SVC_PROFILE}" CACHE INTERNAL "")
    set(SVC_${SERVER}_PROVIDES "${SVC_PROVIDES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_REQUIRES "${SVC_REQUIRES}" CACHE INTERNAL "")
    set(SVC_${SERVER}_WANTS "${SVC_WANTS}" CACHE INTERNAL "")
    set(SVC_${SERVER}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
    # 路径：优先用 service.conf 指定的，否则默认
    if (SVC_PATH)
        set(SVC_${SERVER}_PATH "${SVC_PATH}" CACHE INTERNAL "")
    else ()
        set(SVC_${SERVER}_PATH "/sys/drivers/${SERVER}.elf" CACHE INTERNAL "")
    endif ()
endforeach ()

# 扫描应用
foreach (APP ${APP_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/apps/${APP}/service.conf")
    if (EXISTS ${SVC_CONF})
        parse_service_conf("${SVC_CONF}" "SVC")

        # apps 也可以是 core
        if (SVC_CORE)
            list(APPEND CORE_SERVICES "${APP}")
            set(SVC_${APP}_CORE TRUE CACHE INTERNAL "")
        else ()
            list(APPEND USER_SERVICES "${APP}")
            set(SVC_${APP}_CORE FALSE CACHE INTERNAL "")
        endif ()

        set(SVC_${APP}_TYPE "app" CACHE INTERNAL "")
        set(SVC_${APP}_AFTER "${SVC_AFTER}" CACHE INTERNAL "")
        set(SVC_${APP}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
        set(SVC_${APP}_MOUNT "${SVC_MOUNT}" CACHE INTERNAL "")
        set(SVC_${APP}_RESPAWN "${SVC_RESPAWN}" CACHE INTERNAL "")
        set(SVC_${APP}_NO_READY_FILE "${SVC_NO_READY_FILE}" CACHE INTERNAL "")
        set(SVC_${APP}_PROFILE "${SVC_PROFILE}" CACHE INTERNAL "")
        set(SVC_${APP}_PROVIDES "${SVC_PROVIDES}" CACHE INTERNAL "")
        set(SVC_${APP}_REQUIRES "${SVC_REQUIRES}" CACHE INTERNAL "")
        set(SVC_${APP}_WANTS "${SVC_WANTS}" CACHE INTERNAL "")
        set(SVC_${APP}_HANDLES "${SVC_HANDLES}" CACHE INTERNAL "")
        # 路径：优先用 service.conf 指定的，否则默认
        if (SVC_PATH)
            set(SVC_${APP}_PATH "${SVC_PATH}" CACHE INTERNAL "")
        else ()
            set(SVC_${APP}_PATH "/sys/bin/${APP}.elf" CACHE INTERNAL "")
        endif ()
    endif ()
endforeach ()

# 扫描 bin 工具（确定哪些是 core）
# bin 工具不是服务，不需要 services.conf 条目
# core=true 的打包到 rootfs.img，core=false 的放到 optional/
set(CORE_BIN_TOOLS "")
set(USER_BIN_TOOLS "")

foreach (BIN ${BIN_TOOL_NAMES})
    set(SVC_CONF "${CMAKE_CURRENT_SOURCE_DIR}/apps/bin/${BIN}/service.conf")
    set(IS_CORE ${PACK_BIN_TO_ROOTFS})  # 使用全局配置作为默认值

    if (EXISTS ${SVC_CONF})
        parse_service_conf("${SVC_CONF}" "SVC")
        if (DEFINED SVC_CORE)
            set(IS_CORE ${SVC_CORE})  # service.conf 覆盖默认值
        endif ()
    endif ()

    if (IS_CORE)
        list(APPEND CORE_BIN_TOOLS "${BIN}")
    else ()
        list(APPEND USER_BIN_TOOLS "${BIN}")
    endif ()
endforeach ()

set(CORE_BIN_TOOLS "${CORE_BIN_TOOLS}" CACHE INTERNAL "Core bin tools (in rootfs)")
set(USER_BIN_TOOLS "${USER_BIN_TOOLS}" CACHE INTERNAL "User bin tools (in optional)")

message(STATUS "[user] Core services: ${CORE_SERVICES}")
message(STATUS "[user] Core bin tools: ${CORE_BIN_TOOLS}")
message(STATUS "[user] User bin tools: ${USER_BIN_TOOLS}")
message(STATUS "[user] User services: ${USER_SERVICES}")

# 拓扑排序核心服务
set(SORTED_CORE "")
set(REMAINING ${CORE_SERVICES})

while (REMAINING)
    set(FOUND FALSE)
    foreach (SVC ${REMAINING})
        set(DEPS_MET TRUE)
        if (SVC_${SVC}_AFTER)
            string(REPLACE " " ";" AFTER_LIST "${SVC_${SVC}_AFTER}")
            foreach (DEP ${AFTER_LIST})
                list(FIND SORTED_CORE ${DEP} DEP_IDX)
                if (DEP_IDX EQUAL -1)
                    list(FIND CORE_SERVICES ${DEP} IS_CORE)
                    if (NOT IS_CORE EQUAL -1)
                        set(DEPS_MET FALSE)
                        break()
                    endif ()
                endif ()
            endforeach ()
        endif ()

        if (DEPS_MET)
            list(APPEND SORTED_CORE ${SVC})
            list(REMOVE_ITEM REMAINING ${SVC})
            set(FOUND TRUE)
            break()
        endif ()
    endforeach ()

    if (NOT FOUND AND REMAINING)
        list(GET REMAINING 0 FIRST)
        list(APPEND SORTED_CORE ${FIRST})
        list(REMOVE_ITEM REMAINING ${FIRST})
    endif ()
endwhile ()

set(CORE_SERVICES ${SORTED_CORE})
message(STATUS "[user] Sorted core: ${CORE_SERVICES}")

# ============================================
# 生成 module_index.h
# ============================================
set(MODULE_H "/* 自动生成 - user/CMakeLists.txt */\n")
string(APPEND MODULE_H "#ifndef _MODULE_INDEX_H\n")
string(APPEND MODULE_H "#define _MODULE_INDEX_H\n\n")
string(APPEND MODULE_H "#define MODULE_INIT 0\n")

set(MOD_IDX 1)
foreach (SVC ${CORE_SERVICES})
    string(TOUPPER ${SVC} SVC_UPPER)
    string(APPEND MODULE_H "#define MODULE_${SVC_UPPER} ${MOD_IDX}\n")
    set(SVC_${SVC}_MODULE ${MOD_IDX})
    math(EXPR MOD_IDX "${MOD_IDX} + 1")
endforeach ()

string(APPEND MODULE_H "#define MODULE_ROOTFS ${MOD_IDX}\n\n")
string(APPEND MODULE_H "#define USER_CONFIG_DEFAULT \"/sys/etc/services.conf\"\n\n")
string(APPEND MODULE_H "#endif\n")

file(WRITE ${CMAKE_BINARY_DIR}/include/module_index.h "${MODULE_H}")

# ============================================
# 生成 core_services.h（嵌入式配置）
# ============================================
# 直接使用 core_services.conf 文件（新服务管理系统）
set(CORE_SERVICES_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/init/core_services.conf")

if (EXISTS ${CORE_SERVICES_CONF_PATH})
    file(READ ${CORE_SERVICES_CONF_PATH} CORE_CONF)
    message(STATUS "Using core_services.conf from ${CORE_SERVICES_CONF_PATH}")
else ()
    message(FATAL_ERROR "core_services.conf not found at ${CORE_SERVICES_CONF_PATH}")
endif ()

# 转换为 C 字符串
string(REPLACE "\\" "\\\\" CORE_CONF_ESC "${CORE_CONF}")
string(REPLACE "\"" "\\\"" CORE_CONF_ESC "${CORE_CONF_ESC}")
string(REPLACE "\n" "\\n\"\n    \"" CORE_CONF_ESC "${CORE_CONF_ESC}")

set(CORE_H "/* 自动生成 - user/CMakeLists.txt */\n")
string(APPEND CORE_H "#ifndef _CORE_SERVICES_H\n")
string(APPEND CORE_H "#define _CORE_SERVICES_H\n\n")
string(APPEND CORE_H "static const char CORE_SERVICES_CONF[] =\n")
string(APPEND CORE_H "    \"${CORE_CONF_ESC}\";\n\n")
string(APPEND CORE_H "#endif\n")

file(WRITE ${CMAKE_BINARY_DIR}/include/core_services.h "${CORE_H}")

# ============================================
# 生成 services.conf（用户服务配置）
# ============================================
set(USER_CONF "# 用户服务配置\n\n")

foreach (SVC ${USER_SERVICES})
    string(APPEND USER_CONF "[service.${SVC}]\n")
    string(APPEND USER_CONF "type = path\n")
    string(APPEND USER_CONF "path = ${SVC_${SVC}_PATH}\n")
    if (SVC_${SVC}_AFTER)
        string(APPEND USER_CONF "after = ${SVC_${SVC}_AFTER}\n")
    endif ()
    if (SVC_${SVC}_HANDLES)
        string(APPEND USER_CONF "handles = ${SVC_${SVC}_HANDLES}\n")
    endif ()
    if (SVC_${SVC}_PROVIDES)
        string(APPEND USER_CONF "provides = ${SVC_${SVC}_PROVIDES}\n")
    endif ()
    if (SVC_${SVC}_REQUIRES)
        string(APPEND USER_CONF "requires = ${SVC_${SVC}_REQUIRES}\n")
    endif ()
    if (SVC_${SVC}_WANTS)
        string(APPEND USER_CONF "wants = ${SVC_${SVC}_WANTS}\n")
    endif ()
    if (SVC_${SVC}_MOUNT)
        string(APPEND USER_CONF "mount = ${SVC_${SVC}_MOUNT}\n")
    endif ()
    if (SVC_${SVC}_RESPAWN)
        string(APPEND USER_CONF "respawn = true\n")
    endif ()
    if (SVC_${SVC}_NO_READY_FILE)
        string(APPEND USER_CONF "no_ready_file = true\n")
    endif ()
    if (SVC_${SVC}_PROFILE)
        string(APPEND USER_CONF "profile = ${SVC_${SVC}_PROFILE}\n")
    endif ()
    string(APPEND USER_CONF "\n")
endforeach ()

file(WRITE ${CMAKE_BINARY_DIR}/generated/services.conf "${USER_CONF}")

if (EXISTS ${INIT_HANDLES_CONF_PATH})
    if (NOT DEFINED INIT_HANDLES_CONF_CONTENT)
        file(READ ${INIT_HANDLES_CONF_PATH} INIT_HANDLES_CONF_CONTENT)
    endif ()
    file(APPEND ${CMAKE_BINARY_DIR}/generated/services.conf "\n${INIT_HANDLES_CONF_CONTENT}\n")
endif ()

# ============================================
# init.elf（依赖生成的头文件）
# ============================================
add_executable(init.elf
        libs/libc/src/crt0.s
        init/main.c
        init/ini_parser.c
        init/svc_manager.c
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libc/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(init.elf PRIVATE c)
target_link_options(init.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/libs/user.ld -nostdlib
)

add_custom_command(TARGET init.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_BINARY_DIR}/init.elf
)

# ============================================
# 导出给 iso 模块
# ============================================
set(CORE_SERVICES "${CORE_SERVICES}" CACHE INTERNAL "Core services list")
set(USER_SERVICES "${USER_SERVICES}" CACHE INTERNAL "User services list")
set(INIT_ELF "${CMAKE_BINARY_DIR}/init.elf" CACHE INTERNAL "Init ELF path")
