#ifndef OA_WINDOWS_ANDROID_LOG_H
#define OA_WINDOWS_ANDROID_LOG_H

#include <stdarg.h>
#include <stdio.h>

enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN, ANDROID_LOG_ERROR };

static inline int __android_log_print(int priority, const char *tag, const char *format, ...)
{
    (void)priority;
    fprintf(stderr, "[%s] ", tag);
    va_list args;
    va_start(args, format);
    int length = vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return length;
}

static inline int __android_log_write(int priority, const char *tag, const char *message)
{
    return __android_log_print(priority, tag, "%s", message);
}

#endif
