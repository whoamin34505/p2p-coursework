#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

void log_message(const char *level, const char *format, ...) {
    FILE *file;
    time_t now;
    struct tm *time_info;
    char time_buffer[32];
    va_list args;

    file = fopen("node.log", "a");
    if (file == NULL) {
        return;
    }

    now = time(NULL);
    time_info = localtime(&now);

    if (time_info != NULL) {
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "unknown-time");
    }

    fprintf(file, "[%s] %s ", time_buffer, level);

    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);

    fprintf(file, "\n");
    fclose(file);
}