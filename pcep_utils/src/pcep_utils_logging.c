/*
 * pcep_utils_logging.c
 *
 *  Created on: Dec 13, 2019
 *      Author: brady
 */

#include <stdarg.h>
#include <stdio.h>
#include "pcep_utils_logging.h"

/* Forward declaration */
int pcep_stdout_logger(int priority, const char *format, va_list args);

static pcep_logger_func logger_func = pcep_stdout_logger;
static int logging_level_ = LOG_INFO;

void register_logger(pcep_logger_func logger)
{
    logger_func = logger;
}

void set_logging_level(int level)
{
    logging_level_ = level;
}

int get_logging_level()
{
    return logging_level_;
}

void pcep_log(int priority, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    logger_func(priority, format, va);
    va_end(va);
}

void pcep_log_hexbytes(int priority, const char *message, const uint8_t *bytes, uint8_t bytes_len)
{
    char byte_str[2048];
    int i = 0;

    sprintf(byte_str, "%s ", message);
    for (; i < bytes_len; i++)
    {
        sprintf(byte_str, "%02x ", bytes[i]);
    }
    sprintf(byte_str, "\n");

    pcep_log(priority, "%s", byte_str);
}

/* Defined with a return type to match the FRR logging signature.
 * Assuming glibc printf() is thread-safe. */
int pcep_stdout_logger(int priority, const char *format, va_list args)
{
    if (priority <= logging_level_)
    {
        vprintf(format, args);
        printf("\n");
    }

    return 0;
}
