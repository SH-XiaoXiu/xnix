/**
 * @file kernel/sys/sys_input.c
 * @brief 输入队列系统调用
 */

#include <kernel/io/input.h>
#include <kernel/sys/syscall.h>
#include <xnix/syscall.h>

/* SYS_INPUT_WRITE: ebx=char */
static int32_t sys_input_write(const uint32_t *args) {
    char c = (char)args[0];
    return input_write(c);
}

/* SYS_INPUT_READ */
static int32_t sys_input_read(const uint32_t *args) {
    (void)args;
    return input_read();
}

void sys_input_init(void) {
    syscall_register(SYS_INPUT_WRITE, sys_input_write, 1, "input_write");
    syscall_register(SYS_INPUT_READ, sys_input_read, 0, "input_read");
}
