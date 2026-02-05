/**
 * @file serial.h
 * @brief Serial driver client SDK
 *
 * Public API for accessing the serial console service via IPC
 */

#ifndef LIBS_SERIAL_H
#define LIBS_SERIAL_H

#include <xnix/abi/types.h>

#ifndef size_t
typedef unsigned int size_t;
#endif

/* Message types (shared by client and server) */
#define SERIAL_MSG_WRITE 1
#define SERIAL_MSG_COLOR 2

/**
 * Initialize serial client SDK
 *
 * Looks up the "serial" endpoint handle
 *
 * @return 0 on success, -1 on failure
 */
int serial_init(void);

/**
 * Write data to serial port via IPC
 *
 * @param buf  Data buffer
 * @param len  Length of data
 * @return 0 on success, negative on error
 */
int serial_write(const char *buf, size_t len);

/**
 * Write single character to serial port
 *
 * @param c  Character to write
 * @return 0 on success, negative on error
 */
int serial_putchar(char c);

/**
 * Set serial output color
 *
 * @param color  Color code (VGA color)
 */
void serial_set_color(uint8_t color);

#endif /* LIBS_SERIAL_H */
