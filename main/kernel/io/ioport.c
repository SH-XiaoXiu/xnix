#include <arch/cpu.h>

#include <kernel/io/ioport.h>
#include <xnix/mm.h>

void ioport_init(void) {
    /*
     * I/O 端口访问控制现已迁移到 Permission System.
     * 初始化逻辑移至 perm_init().
     * 此函数保留为空,以防有其他初始化需求.
     */
}
