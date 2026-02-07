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
# initramfs.img 构建 (包含核心服务)
# ============================================
set(INITRAMFS_FILES "")

# 核心服务 -> initramfs /sbin/
foreach (SVC ${CORE_SERVICES})
    list(APPEND INITRAMFS_FILES "${CMAKE_BINARY_DIR}/${SVC}.elf:sbin/${SVC}.elf")
endforeach ()

# 核心服务配置 (从 user 目录)
list(APPEND INITRAMFS_FILES "${CMAKE_SOURCE_DIR}/user/core_services.conf:etc/core_services.conf")

# 写入文件列表
set(INITRAMFS_FILELIST ${CMAKE_BINARY_DIR}/initramfs_files.txt)
string(REPLACE ";" "\n" INITRAMFS_FILES_CONTENT "${INITRAMFS_FILES}")
file(WRITE ${INITRAMFS_FILELIST} "${INITRAMFS_FILES_CONTENT}")

# 生成 initramfs.img
set(INITRAMFS_IMG ${CMAKE_BINARY_DIR}/initramfs.img)
set(INITRAMFS_SIZE 2048)  # 2MB

# 收集依赖
set(INITRAMFS_DEPENDS "")
foreach (SVC ${CORE_SERVICES})
    list(APPEND INITRAMFS_DEPENDS ${SVC}.elf)
endforeach ()

add_custom_command(
        OUTPUT ${INITRAMFS_IMG}
        # 创建临时目录并复制文件
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/initramfs_tmp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/initramfs_tmp
        # 使用 CMake 脚本读取文件列表并复制
        COMMAND ${CMAKE_COMMAND}
        -DFILELIST=${INITRAMFS_FILELIST}
        -DTMPDIR=${CMAKE_BINARY_DIR}/initramfs_tmp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_initramfs_files.cmake
        # 打包为 TAR
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/initramfs_tmp
        tar -cf ${INITRAMFS_IMG} --format=ustar .
        # 清理临时目录
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/initramfs_tmp
        DEPENDS ${INITRAMFS_DEPENDS}
        COMMENT "生成 initramfs.img (TAR)..."
)

add_custom_target(initramfs DEPENDS ${INITRAMFS_IMG})

# ============================================
# system.img 内容收集 (FAT32)
# ============================================
# 使用 app.cmake 收集的全局变量 SYSTEM_IMG_FILES
# 各服务通过 APP_INSTALL_TO_SYSTEM=ON 自动注册

# 配置文件
# 1. 核心服务配置 -> initramfs:/etc/core_services.conf（已包含）
# 2. 用户服务配置 -> system.img:/etc/user_services.conf（手写）
set(SYSTEM_FILES ${SYSTEM_IMG_FILES})
list(APPEND SYSTEM_FILES "${CMAKE_SOURCE_DIR}/user/user_services.conf:/etc/user_services.conf")

# 写入文件列表
set(SYSTEM_FILELIST ${CMAKE_BINARY_DIR}/system_files.txt)
string(REPLACE ";" "\n" SYSTEM_FILES_CONTENT "${SYSTEM_FILES}")
file(WRITE ${SYSTEM_FILELIST} "${SYSTEM_FILES_CONTENT}")

# ============================================
# system.img 生成 (FAT32, 使用 build_system_img.sh)
# ============================================
set(SYSTEM_IMG ${CMAKE_BINARY_DIR}/system.img)

# 收集依赖：从 SYSTEM_FILES 中提取 ELF 文件
set(SYSTEM_DEPENDS "")
foreach (ENTRY ${SYSTEM_FILES})
    string(REGEX MATCH "^([^:]+)" SRC_FILE "${ENTRY}")
    if (SRC_FILE MATCHES "\\.elf$")
        get_filename_component(TARGET_NAME ${SRC_FILE} NAME_WE)
        # 如果是从 build 目录的 .elf，添加为依赖
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
        COMMAND mkfs.fat -F 32 ${SYSTEM_IMG}
        # 创建目录
        COMMAND mmd -i ${SYSTEM_IMG} ::/sbin ::/bin ::/etc ::/drivers || true
        # 使用 CMake 脚本复制文件
        COMMAND ${CMAKE_COMMAND}
        -DIMG_FILE=${SYSTEM_IMG}
        -DFILELIST=${SYSTEM_FILELIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_system_files.cmake
        DEPENDS ${SYSTEM_DEPENDS}
        COMMENT "生成 system.img (FAT32, 16MB)..."
)

add_custom_target(system DEPENDS ${SYSTEM_IMG})

# ============================================
# grub.cfg 生成 (加载 init, initramfs 和 system.img)
# ============================================
set(GRUB_CFG_CONTENT "set timeout=0\nset default=0\nmenuentry \"Xnix\" {\n")
string(APPEND GRUB_CFG_CONTENT "  multiboot /boot/xnix.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/init.elf name=init\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/initramfs.img name=initramfs\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/system.img name=system\n")
string(APPEND GRUB_CFG_CONTENT "}\n")

file(MAKE_DIRECTORY ${ISO_DIR}/boot/grub)
file(WRITE ${ISO_DIR}/boot/grub/grub.cfg "${GRUB_CFG_CONTENT}")

# ============================================
# ISO 构建目标
# ============================================
set(ISO_DEPENDS xnix.elf init.elf initramfs system)

# 使用干净的临时目录，避免打包 CMake 生成的文件
set(ISO_STAGE_DIR ${CMAKE_BINARY_DIR}/iso_stage)

add_custom_target(iso
        # 清理旧的 stage 目录
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${ISO_STAGE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_STAGE_DIR}/boot/grub

        # 复制启动文件
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/xnix.elf ${ISO_STAGE_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_STAGE_DIR}/boot/init.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_IMG} ${ISO_STAGE_DIR}/boot/initramfs.img
        COMMAND ${CMAKE_COMMAND} -E copy ${SYSTEM_IMG} ${ISO_STAGE_DIR}/boot/system.img

        # 复制 grub.cfg
        COMMAND ${CMAKE_COMMAND} -E copy ${ISO_DIR}/boot/grub/grub.cfg ${ISO_STAGE_DIR}/boot/grub/grub.cfg

        # 生成 ISO（只打包 stage 目录）
        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_STAGE_DIR} 2>/dev/null

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
