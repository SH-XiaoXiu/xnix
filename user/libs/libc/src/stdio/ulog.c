/**
 * @file ulog.c
 * @brief 用户态日志输出便捷实现
 */

#include <string.h>
#include <xnix/ulog.h>

static int ulog_emit_tag(FILE *stream, enum term_color tag_color, const char *tag) {
    if (!stream || !tag) {
        return -1;
    }

    if (termcolor_set(stream, tag_color, TERM_COLOR_BLACK) == 0) {
        fputs(tag, stream);
        fflush(stream);
        termcolor_reset(stream);
    } else {
        fputs(tag, stream);
    }

    return 0;
}

int ulog_vtagf(FILE *stream, enum term_color tag_color, const char *tag, const char *fmt,
               va_list ap) {
    if (!stream || !tag || !fmt) {
        return -1;
    }

    ulog_emit_tag(stream, tag_color, tag);
    return vfprintf(stream, fmt, ap);
}

int ulog_tagf(FILE *stream, enum term_color tag_color, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = ulog_vtagf(stream, tag_color, tag, fmt, ap);
    va_end(ap);
    return ret;
}

int ulog_okf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = ulog_vtagf(stdout, TERM_COLOR_LIGHT_GREEN, "[OK] ", fmt, ap);
    va_end(ap);
    return ret;
}

int ulog_infof(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = ulog_vtagf(stdout, TERM_COLOR_WHITE, "[INFO] ", fmt, ap);
    va_end(ap);
    return ret;
}

int ulog_warnf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = ulog_vtagf(stdout, TERM_COLOR_LIGHT_BROWN, "[WARN] ", fmt, ap);
    va_end(ap);
    return ret;
}

int ulog_errf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = ulog_vtagf(stdout, TERM_COLOR_LIGHT_RED, "[ERR] ", fmt, ap);
    va_end(ap);
    return ret;
}
