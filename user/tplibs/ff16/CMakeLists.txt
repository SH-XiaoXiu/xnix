# FatFs 文件系统库
add_library(fatfs STATIC
        ff.c
        ffunicode.c
)

target_include_directories(fatfs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 用户态编译选项
target_compile_options(fatfs PRIVATE
        -m32 -ffreestanding -fno-stack-protector -fno-pie
        -nostdlib -Wall -Wextra -Wno-unused-parameter
)
