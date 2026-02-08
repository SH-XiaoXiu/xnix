# ISO 打包模块
#
# 职责：
#   - 生成 initramfs.img (TAR, 核心服务 + 配置)
#   - 生成 system.img (FAT32, 完整系统目录)
#   - 生成 system_installer.img (FAT32, 最小安装环境)
#   - 生成 disk_template.img (MBR+FAT32+GRUB 可引导磁盘)
#   - 生成 ISO (El Torito)
#   - 生成 optional/ 目录
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
# initramfs.img 构建 (核心服务 + 配置)
# ============================================
set(INITRAMFS_FILES "")

# 核心服务 -> initramfs /sbin/
foreach (SVC ${CORE_SERVICES})
    list(APPEND INITRAMFS_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:sbin/${SVC}.elf")
endforeach ()

# 核心服务配置
list(APPEND INITRAMFS_FILES "${CMAKE_SOURCE_DIR}/user/core_services.conf:etc/core_services.conf")

# 写入文件列表
set(INITRAMFS_FILELIST ${CMAKE_BINARY_DIR}/initramfs_files.txt)
string(REPLACE ";" "\n" INITRAMFS_FILES_CONTENT "${INITRAMFS_FILES}")
file(WRITE ${INITRAMFS_FILELIST} "${INITRAMFS_FILES_CONTENT}")

# 生成 initramfs.img
set(INITRAMFS_IMG ${CMAKE_BINARY_DIR}/initramfs.img)

# 收集依赖
set(INITRAMFS_DEPENDS "")
foreach (SVC ${CORE_SERVICES})
    list(APPEND INITRAMFS_DEPENDS ${SVC}.elf)
endforeach ()

add_custom_command(
        OUTPUT ${INITRAMFS_IMG}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/initramfs_tmp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/initramfs_tmp
        COMMAND ${CMAKE_COMMAND}
        -DFILELIST=${INITRAMFS_FILELIST}
        -DTMPDIR=${CMAKE_BINARY_DIR}/initramfs_tmp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_initramfs_files.cmake
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/initramfs_tmp
        tar -cf ${INITRAMFS_IMG} --format=ustar .
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/initramfs_tmp
        DEPENDS ${INITRAMFS_DEPENDS}
        COMMENT "生成 initramfs.img (TAR)..."
)

add_custom_target(initramfs DEPENDS ${INITRAMFS_IMG})

# ============================================
# system.img 内容收集 (FAT32, 完整系统)
# ============================================
# SYSTEM_IMG_FILES 由 app.cmake 自动注册
set(SYSTEM_FILES ${SYSTEM_IMG_FILES})

# 核心服务 -> /sbin/
foreach (SVC ${CORE_SERVICES})
    list(APPEND SYSTEM_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:/sbin/${SVC}.elf")
endforeach ()

# 配置文件
list(APPEND SYSTEM_FILES "${CMAKE_SOURCE_DIR}/user/core_services.conf:/etc/core_services.conf")
list(APPEND SYSTEM_FILES "${CMAKE_SOURCE_DIR}/user/user_services.conf:/etc/user_services.conf")

# boot 文件 (磁盘启动使用)
list(APPEND SYSTEM_FILES "${CMAKE_BINARY_DIR}/xnix.elf:/boot/xnix.elf")
list(APPEND SYSTEM_FILES "${CMAKE_BINARY_DIR}/init.elf:/boot/init.elf")
# initramfs.img 会在生成后复制

# sys 文件 (恢复/信息副本)
list(APPEND SYSTEM_FILES "${CMAKE_BINARY_DIR}/xnix.elf:/sys/xnix.elf")
list(APPEND SYSTEM_FILES "${CMAKE_BINARY_DIR}/init.elf:/sys/init.elf")

# 写入文件列表
set(SYSTEM_FILELIST ${CMAKE_BINARY_DIR}/system_files.txt)
string(REPLACE ";" "\n" SYSTEM_FILES_CONTENT "${SYSTEM_FILES}")
file(WRITE ${SYSTEM_FILELIST} "${SYSTEM_FILES_CONTENT}")

# ============================================
# system.img 生成 (FAT32, 16MB)
# ============================================
set(SYSTEM_IMG ${CMAKE_BINARY_DIR}/system.img)

# 收集依赖
set(SYSTEM_DEPENDS initramfs xnix.elf init.elf)
foreach (ENTRY ${SYSTEM_FILES})
    string(REGEX MATCH "^([^:]+)" SRC_FILE "${ENTRY}")
    if (SRC_FILE MATCHES "\\.elf$")
        get_filename_component(TARGET_NAME ${SRC_FILE} NAME_WE)
        if (SRC_FILE MATCHES "^${CMAKE_BINARY_DIR}/")
            list(APPEND SYSTEM_DEPENDS ${TARGET_NAME}.elf)
        endif ()
    endif ()
endforeach ()

add_custom_command(
        OUTPUT ${SYSTEM_IMG}
        # 创建空镜像
        COMMAND ${CMAKE_COMMAND} -E env dd if=/dev/zero of=${SYSTEM_IMG} bs=1M count=16 status=none
        # 格式化为 FAT32
        COMMAND mkfs.fat -F 16 ${SYSTEM_IMG}
        # 创建目录结构
        COMMAND mmd -i ${SYSTEM_IMG} ::/sbin ::/bin ::/etc ::/sys ::/boot ::/boot/grub ::/drivers ::/mnt || true
        # 复制文件列表
        COMMAND ${CMAKE_COMMAND}
        -DIMG_FILE=${SYSTEM_IMG}
        -DFILELIST=${SYSTEM_FILELIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_system_files.cmake
        # 复制 initramfs.img 到 /boot/
        COMMAND mcopy -i ${SYSTEM_IMG} -o ${INITRAMFS_IMG} ::/boot/initramfs.img
        DEPENDS ${SYSTEM_DEPENDS}
        COMMENT "生成 system.img (FAT32, 16MB)..."
)

add_custom_target(system DEPENDS ${SYSTEM_IMG})

# ============================================
# system_installer.img (FAT32, 最小安装环境)
# ============================================
set(SYSTEM_INSTALLER_IMG ${CMAKE_BINARY_DIR}/system_installer.img)

# installer 需要: shell + installer + 配置
set(INSTALLER_FILELIST ${CMAKE_BINARY_DIR}/installer_files.txt)
set(INSTALLER_FILES "")
list(APPEND INSTALLER_FILES "${CMAKE_BINARY_DIR}/shell.elf:/bin/shell.elf")
list(APPEND INSTALLER_FILES "${CMAKE_BINARY_DIR}/installer.elf:/bin/installer.elf")
list(APPEND INSTALLER_FILES "${CMAKE_SOURCE_DIR}/user/user_services.conf:/etc/user_services.conf")
# 核心服务也放到 /sbin/ (与 initramfs 内容相同, 用于 respawn)
foreach (SVC ${CORE_SERVICES})
    list(APPEND INSTALLER_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:/sbin/${SVC}.elf")
endforeach ()
list(APPEND INSTALLER_FILES "${CMAKE_SOURCE_DIR}/user/core_services.conf:/etc/core_services.conf")
string(REPLACE ";" "\n" INSTALLER_FILES_CONTENT "${INSTALLER_FILES}")
file(WRITE ${INSTALLER_FILELIST} "${INSTALLER_FILES_CONTENT}")

add_custom_command(
        OUTPUT ${SYSTEM_INSTALLER_IMG}
        COMMAND ${CMAKE_COMMAND} -E env dd if=/dev/zero of=${SYSTEM_INSTALLER_IMG} bs=1M count=16 status=none
        COMMAND mkfs.fat -F 16 ${SYSTEM_INSTALLER_IMG}
        COMMAND mmd -i ${SYSTEM_INSTALLER_IMG} ::/sbin ::/bin ::/etc || true
        COMMAND ${CMAKE_COMMAND}
        -DIMG_FILE=${SYSTEM_INSTALLER_IMG}
        -DFILELIST=${INSTALLER_FILELIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_system_files.cmake
        DEPENDS shell.elf installer.elf ${INITRAMFS_DEPENDS}
        COMMENT "生成 system_installer.img (FAT16, 16MB)..."
)

add_custom_target(system_installer DEPENDS ${SYSTEM_INSTALLER_IMG})

# ============================================
# disk_template.img (MBR+FAT32+GRUB 可引导磁盘)
# ============================================
set(DISK_TEMPLATE_IMG ${CMAKE_BINARY_DIR}/disk_template.img)
set(DISK_TEMPLATE_SIZE 128) # 128MB

add_custom_command(
        OUTPUT ${DISK_TEMPLATE_IMG}
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/make_disk_template.sh
        ${DISK_TEMPLATE_SIZE} ${DISK_TEMPLATE_IMG} ${CMAKE_BINARY_DIR}
        DEPENDS xnix.elf init.elf initramfs system
        COMMENT "生成可引导磁盘模板 (${DISK_TEMPLATE_SIZE}MB)..."
)

add_custom_target(disk_template DEPENDS ${DISK_TEMPLATE_IMG})

# ============================================
# grub.cfg 生成 (常规 ISO: init + initramfs + system)
# ============================================
set(GRUB_CFG_CONTENT "set timeout=0\nset default=0\nmenuentry \"Xnix\" {\n")
string(APPEND GRUB_CFG_CONTENT "  multiboot /boot/xnix.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/init.elf name=init\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/initramfs.img name=initramfs\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/system.img name=system\n")
string(APPEND GRUB_CFG_CONTENT "}\n")

file(MAKE_DIRECTORY ${ISO_DIR}/boot/grub)
file(WRITE ${ISO_DIR}/boot/grub/grub.cfg "${GRUB_CFG_CONTENT}")

# installer ISO grub.cfg
set(INSTALLER_GRUB_CFG "set timeout=0\nset default=0\nmenuentry \"Xnix Installer\" {\n")
string(APPEND INSTALLER_GRUB_CFG "  multiboot /boot/xnix.elf\n")
string(APPEND INSTALLER_GRUB_CFG "  module /boot/init.elf name=init\n")
string(APPEND INSTALLER_GRUB_CFG "  module /boot/initramfs.img name=initramfs\n")
string(APPEND INSTALLER_GRUB_CFG "  module /boot/system.img name=system\n")
string(APPEND INSTALLER_GRUB_CFG "  module /boot/disk_template.img name=disk_template\n")
string(APPEND INSTALLER_GRUB_CFG "}\n")

file(WRITE ${ISO_DIR}/boot/grub/grub_installer.cfg "${INSTALLER_GRUB_CFG}")

# ============================================
# ISO 构建目标 (常规)
# ============================================
set(ISO_DEPENDS xnix.elf init.elf initramfs system)
set(ISO_STAGE_DIR ${CMAKE_BINARY_DIR}/iso_stage)

add_custom_target(iso
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${ISO_STAGE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_STAGE_DIR}/boot/grub

        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/xnix.elf ${ISO_STAGE_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_STAGE_DIR}/boot/init.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_IMG} ${ISO_STAGE_DIR}/boot/initramfs.img
        COMMAND ${CMAKE_COMMAND} -E copy ${SYSTEM_IMG} ${ISO_STAGE_DIR}/boot/system.img

        COMMAND ${CMAKE_COMMAND} -E copy ${ISO_DIR}/boot/grub/grub.cfg ${ISO_STAGE_DIR}/boot/grub/grub.cfg

        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_STAGE_DIR} 2>/dev/null

        DEPENDS ${ISO_DEPENDS}
        COMMENT "创建可启动 ISO..."
)

# ============================================
# Installer ISO 构建目标
# ============================================
set(INSTALLER_ISO_STAGE_DIR ${CMAKE_BINARY_DIR}/iso_installer_stage)

add_custom_target(iso_installer
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${INSTALLER_ISO_STAGE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${INSTALLER_ISO_STAGE_DIR}/boot/grub

        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/xnix.elf ${INSTALLER_ISO_STAGE_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${INSTALLER_ISO_STAGE_DIR}/boot/init.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_IMG} ${INSTALLER_ISO_STAGE_DIR}/boot/initramfs.img
        COMMAND ${CMAKE_COMMAND} -E copy ${SYSTEM_INSTALLER_IMG} ${INSTALLER_ISO_STAGE_DIR}/boot/system.img
        COMMAND ${CMAKE_COMMAND} -E copy ${DISK_TEMPLATE_IMG} ${INSTALLER_ISO_STAGE_DIR}/boot/disk_template.img

        COMMAND ${CMAKE_COMMAND} -E copy ${ISO_DIR}/boot/grub/grub_installer.cfg ${INSTALLER_ISO_STAGE_DIR}/boot/grub/grub.cfg

        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix_installer.iso ${INSTALLER_ISO_STAGE_DIR} 2>/dev/null

        DEPENDS xnix.elf init.elf initramfs system_installer disk_template
        COMMENT "创建安装 ISO..."
)

# ============================================
# optional 目录（非核心服务）
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

foreach (BIN ${USER_BIN_TOOLS})
    list(APPEND OPT_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${BIN_TOOL_OUTPUT_DIR}/${BIN}.elf ${OPTIONAL_DIR}/bin/${BIN}.elf
    )
endforeach ()

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
