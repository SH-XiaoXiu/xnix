# generate_grub_cfg.cmake
# 用于生成 grub.cfg 的 CMake 脚本
#
# 变量:
#   OUTPUT_FILE - 输出文件路径
#   DEMO_ELFS   - demo ELF 文件列表 (分号分隔)

set(GRUB_CFG "set timeout=0\n")
string(APPEND GRUB_CFG "set default=0\n")
string(APPEND GRUB_CFG "menuentry \"Xnix\" {\n")
string(APPEND GRUB_CFG "  multiboot /boot/xnix.elf xnix.initmod=0 xnix.serialmod=1\n")
string(APPEND GRUB_CFG "  module /boot/init.elf\n")
string(APPEND GRUB_CFG "  module /boot/seriald.elf\n")

# 添加 demo 模块
if(DEMO_ELFS)
    foreach(DEMO_ELF ${DEMO_ELFS})
        string(APPEND GRUB_CFG "  module /boot/${DEMO_ELF}\n")
    endforeach()
endif()

string(APPEND GRUB_CFG "}\n")

file(WRITE ${OUTPUT_FILE} "${GRUB_CFG}")
message(STATUS "Generated grub.cfg with demos: ${DEMO_ELFS}")
