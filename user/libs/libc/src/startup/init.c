/**
 * @file init.c
 * @brief libc initialization
 *
 * Called by crt0 before main() to initialize libc services
 */

/* Weak reference to libserial (optional linkage) */
extern int serial_init(void) __attribute__((weak));

/**
 * Initialize libc services
 *
 * This function is called from crt0.s before main()
 *
 * Note: serial_init() is NOT called automatically to avoid bootstrap
 * circular dependency. Programs should call serial_init() explicitly
 * when the serial endpoint is available.
 */
void __libc_init(void) {
    /* No automatic initialization - let programs control output setup */
}
