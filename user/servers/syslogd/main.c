/**
 * @file main.c
 * @brief 系统日志服务 (syslogd)
 *
 * 提供用户态系统日志收集服务, 支持 IO_WRITE 写入日志和 IO_READ 读取日志.
 * 日志存储在环形缓冲区中.
 *
 * IO 协议:
 *   IO_WRITE: 将数据追加到日志环形缓冲区
 *   IO_READ:  从指定偏移读取日志数据
 */

#include <stdio.h>
#include <string.h>
#include <xnix/abi/io.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/sys/server.h>
#include <xnix/syscall.h>

/* 日志环形缓冲区大小 (16 KB) */
#define LOG_RING_SIZE 16384

/* 环形缓冲区 */
static char g_log_ring[LOG_RING_SIZE];
static uint32_t g_log_write_pos;  /* 下一次写入位置 (累计, 不取模) */

/* IO 回复缓冲区 */
#define IO_BUF_SIZE 4096
static char g_io_buf[IO_BUF_SIZE];

/**
 * 向环形缓冲区追加数据
 *
 * @param data 数据指针
 * @param len  数据长度
 * @return 实际写入字节数
 */
static uint32_t ring_write(const char *data, uint32_t len) {
    if (len > LOG_RING_SIZE) {
        /* 数据超过整个缓冲区, 只保留尾部 */
        data += len - LOG_RING_SIZE;
        len = LOG_RING_SIZE;
    }

    uint32_t pos = g_log_write_pos % LOG_RING_SIZE;
    uint32_t first = LOG_RING_SIZE - pos;

    if (first >= len) {
        memcpy(g_log_ring + pos, data, len);
    } else {
        memcpy(g_log_ring + pos, data, first);
        memcpy(g_log_ring, data + first, len - first);
    }

    g_log_write_pos += len;
    return len;
}

/**
 * 从环形缓冲区读取数据
 *
 * @param offset 全局偏移 (累计写入位置)
 * @param buf    输出缓冲区
 * @param max    最大读取字节数
 * @return 实际读取字节数 (0 = 无更多数据)
 */
static uint32_t ring_read(uint32_t offset, char *buf, uint32_t max) {
    /* 计算可用数据范围 */
    uint32_t earliest;
    if (g_log_write_pos > LOG_RING_SIZE) {
        earliest = g_log_write_pos - LOG_RING_SIZE;
    } else {
        earliest = 0;
    }

    /* 偏移在已丢弃区域, 跳到最早可用位置 */
    if (offset < earliest) {
        offset = earliest;
    }

    /* 偏移已到写入位置, 无数据 */
    if (offset >= g_log_write_pos) {
        return 0;
    }

    uint32_t avail = g_log_write_pos - offset;
    if (avail > max) {
        avail = max;
    }

    uint32_t pos = offset % LOG_RING_SIZE;
    uint32_t first = LOG_RING_SIZE - pos;

    if (first >= avail) {
        memcpy(buf, g_log_ring + pos, avail);
    } else {
        memcpy(buf, g_log_ring + pos, first);
        memcpy(buf + first, g_log_ring, avail - first);
    }

    return avail;
}

/**
 * 消息处理函数
 */
static int syslogd_handler(struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case IO_WRITE: {
        uint32_t size = msg->regs.data[3];
        const char *data = (const char *)(uintptr_t)msg->buffer.data;

        if (!data || size == 0 || msg->buffer.size < size) {
            msg->regs.data[0] = (uint32_t)-22; /* -EINVAL */
            msg->buffer.data = 0;
            msg->buffer.size = 0;
            return 0;
        }

        uint32_t written = ring_write(data, size);

        msg->regs.data[0] = written;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }

    case IO_READ: {
        uint32_t offset = msg->regs.data[2];
        uint32_t max_size = msg->regs.data[3];

        if (max_size > IO_BUF_SIZE) {
            max_size = IO_BUF_SIZE;
        }

        uint32_t bytes_read = ring_read(offset, g_io_buf, max_size);

        msg->regs.data[0] = bytes_read;
        msg->buffer.data = (uint64_t)(uintptr_t)g_io_buf;
        msg->buffer.size = bytes_read;
        return 0;
    }

    case IO_CLOSE: {
        msg->regs.data[0] = 0;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }

    default:
        msg->regs.data[0] = (uint32_t)-38; /* -ENOSYS */
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 获取 endpoint (由 init 注入) */
    handle_t ep = env_require("syslog_ep");
    if (ep == HANDLE_INVALID) {
        printf("[syslog] FATAL: syslog_ep not found\n");
        return 1;
    }

    printf("[syslog] started, endpoint=%u\n", ep);

    /* 通知 init 就绪 */
    svc_notify_ready("syslog");

    /* 运行服务主循环 */
    struct sys_server srv = {
        .endpoint = ep,
        .handler  = syslogd_handler,
        .name     = "syslog",
    };

    sys_server_init(&srv);
    sys_server_run(&srv);

    return 0;
}
