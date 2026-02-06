# ISO 打包模块
#
# 职责：
#   - 生成可启动 ISO 镜像
#   - 生成 rootfs.img（包含非核心服务和配置）
#   - 将非核心服务放到 optional/ 目录
#
# 规范：
#   - core=true  -> Multiboot 模块，打包到 ISO
#   - core=false -> 放到 optional/，用户自行部署
#
# 依赖：
#   - KERNEL_ELF    内核路径（来自根 CMake）
#   - INIT_ELF      init 路径（来自 user/）
#   - CORE_SERVICES 核心服务列表（来自 user/）
#   - USER_SERVICES 用户服务列表（来自 user/）
#

message(STATUS "[iso] Core services: ${CORE_SERVICES}")
message(STATUS "[iso] User services: ${USER_SERVICES}")

# ============================================
# 目录设置
# ============================================
set(ISO_DIR ${CMAKE_BINARY_DIR}/iso)
set(OPTIONAL_DIR ${CMAKE_BINARY_DIR}/optional)

# ============================================
# rootfs.img 内容收集
# ============================================
set(ROOTFS_FILES "")

# 非核心驱动/应用 -> rootfs
foreach (SVC ${USER_SERVICES})
    if (SVC_${SVC}_TYPE STREQUAL "driver" OR SVC_${SVC}_TYPE STREQUAL "server")
        list(APPEND ROOTFS_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:drivers/${SVC}.elf")
    elseif (SVC_${SVC}_TYPE STREQUAL "app")
        list(APPEND ROOTFS_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:bin/${SVC}.elf")
    endif ()
endforeach ()

# core bin 工具 -> rootfs /sys/bin/
foreach (BIN ${CORE_BIN_TOOLS})
    list(APPEND ROOTFS_FILES "${BIN_TOOL_OUTPUT_DIR}/${BIN}.elf:bin/${BIN}.elf")
endforeach ()

# 配置文件
list(APPEND ROOTFS_FILES "${CMAKE_BINARY_DIR}/generated/services.conf:etc/services.conf")

# 写入文件列表
set(ROOTFS_FILELIST ${CMAKE_BINARY_DIR}/rootfs_files.txt)
string(REPLACE ";" "\n" ROOTFS_FILES_CONTENT "${ROOTFS_FILES}")
file(WRITE ${ROOTFS_FILELIST} "${ROOTFS_FILES_CONTENT}")

# ============================================
# rootfs.img 生成
# ============================================
set(ROOTFS_IMG ${CMAKE_BINARY_DIR}/rootfs.img)
set(ROOTFS_SIZE 4096)  # 4MB

# 收集依赖
set(ROOTFS_DEPENDS "")
foreach (SVC ${USER_SERVICES})
    list(APPEND ROOTFS_DEPENDS ${SVC}.elf)
endforeach ()
foreach (BIN ${CORE_BIN_TOOLS})
    list(APPEND ROOTFS_DEPENDS bin_${BIN}.elf)
endforeach ()

add_custom_command(
        OUTPUT ${ROOTFS_IMG}
        COMMAND ${CMAKE_COMMAND}
        -DROOTFS_IMG=${ROOTFS_IMG}
        -DROOTFS_SIZE=${ROOTFS_SIZE}
        -DROOTFS_FILELIST=${ROOTFS_FILELIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_rootfs.cmake
        DEPENDS ${ROOTFS_DEPENDS}
        COMMENT "生成 rootfs.img..."
)

add_custom_target(rootfs DEPENDS ${ROOTFS_IMG})

# ============================================
# grub.cfg 生成
# ============================================
set(GRUB_CFG_CONTENT "set timeout=0\nset default=0\nmenuentry \"Xnix\" {\n")
string(APPEND GRUB_CFG_CONTENT "  multiboot /boot/xnix.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /sbin/init.elf name=init\n")

foreach (SVC ${CORE_SERVICES})
    string(APPEND GRUB_CFG_CONTENT "  module /drivers/${SVC}.elf name=${SVC}\n")
endforeach ()

string(APPEND GRUB_CFG_CONTENT "  module /rootfs.img name=rootfs\n")
string(APPEND GRUB_CFG_CONTENT "}\n")

file(MAKE_DIRECTORY ${ISO_DIR}/boot/grub)
file(WRITE ${ISO_DIR}/boot/grub/grub.cfg "${GRUB_CFG_CONTENT}")

# ============================================
# ISO 构建目标
# ============================================
set(ISO_DEPENDS xnix.elf init.elf rootfs)

# 核心服务
foreach (SVC ${CORE_SERVICES})
    list(APPEND ISO_DEPENDS ${SVC}.elf)
endforeach ()

# 复制命令
set(ISO_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/sbin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/drivers
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/xnix.elf ${ISO_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_DIR}/sbin/init.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${ROOTFS_IMG} ${ISO_DIR}/rootfs.img
)

foreach (SVC ${CORE_SERVICES})
    list(APPEND ISO_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/${SVC}.elf ${ISO_DIR}/drivers/${SVC}.elf
    )
endforeach ()

add_custom_target(iso
        ${ISO_COMMANDS}
        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_DIR} 2>/dev/null
        DEPENDS ${ISO_DEPENDS}
        COMMENT "创建可启动 ISO..."
)

# ============================================
# optional 目录（非核心服务，用户自行部署）
# ============================================
set(OPT_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OPTIONAL_DIR}/drivers
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OPTIONAL_DIR}/apps
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OPTIONAL_DIR}/bin
)

foreach (SVC ${USER_SERVICES})
    if (SVC_${SVC}_TYPE STREQUAL "driver" OR SVC_${SVC}_TYPE STREQUAL "server")
        list(APPEND OPT_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/${SVC}.elf ${OPTIONAL_DIR}/drivers/${SVC}.elf
        )
    elseif (SVC_${SVC}_TYPE STREQUAL "app")
        list(APPEND OPT_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/${SVC}.elf ${OPTIONAL_DIR}/apps/${SVC}.elf
        )
    endif ()
endforeach ()

# 非核心 bin 工具 -> optional/bin/
foreach (BIN ${USER_BIN_TOOLS})
    list(APPEND OPT_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${BIN_TOOL_OUTPUT_DIR}/${BIN}.elf ${OPTIONAL_DIR}/bin/${BIN}.elf
    )
endforeach ()

# 复制配置
list(APPEND OPT_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/generated/services.conf ${OPTIONAL_DIR}/services.conf
)

# optional 依赖对应的 ELF 目标
set(OPT_DEPENDS "")
foreach (SVC ${USER_SERVICES})
    list(APPEND OPT_DEPENDS ${SVC}.elf)
endforeach ()
foreach (BIN ${USER_BIN_TOOLS})
    list(APPEND OPT_DEPENDS bin_${BIN}.elf)
endforeach ()

add_custom_target(optional
        ${OPT_COMMANDS}
        DEPENDS ${OPT_DEPENDS}
        COMMENT "准备 optional 目录（非核心服务）..."
)

# ============================================
# 运行目标
# ============================================
add_custom_target(run
        COMMAND qemu-system-i386
        -cdrom ${CMAKE_BINARY_DIR}/xnix.iso
        -m 128
        -serial stdio
        -no-reboot
        -smp 8
        DEPENDS iso
        COMMENT "在 QEMU 中运行 Xnix..."
)

add_custom_target(debug
        COMMAND qemu-system-i386
        -cdrom ${CMAKE_BINARY_DIR}/xnix.iso
        -m 128
        -serial stdio
        -no-reboot
        -smp 8
        -s -S
        DEPENDS iso
        COMMENT "在 QEMU 调试模式下运行 (GDB 端口 1234)..."
)
