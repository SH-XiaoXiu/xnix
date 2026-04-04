#include <io/ioport.h>

void ioport_init(void) {
    /* IO 端口权限由 cap_check_ioport() 在 syscall 路径检查 */
}
