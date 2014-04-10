#include <stdio.h>
#include <stdarg.h>
#include "logger.h"

/*
 * This log function simulates normal logging system.
 */
FILE *log_fp;// = stdout;

void log_print(int level, char* filename, int line, char *fmt,...)
{
    va_list list;
    
    switch (level) {
    case LOG_DEBUG:
        fprintf(log_fp,"-DEBUG: ");
        break;
    case LOG_INFO:
        fprintf(log_fp,"-INFO: ");
        break;
    case LOG_WARN:
        fprintf(log_fp,"-WARN: ");
        break;
    case LOG_ERROR:
        fprintf(log_fp,"-ERROR: ");
        break;
    default:
        break;
    }

    fprintf(log_fp,"[%s][line: %d] ",filename,line);
    va_start( list, fmt );

    vfprintf(log_fp, fmt, list);

    va_end( list );
    fputc( '\n', log_fp );
    fflush(log_fp);
}

int init_log(const char *logfile) {
    if (NULL == logfile) {
        log_fp = stdout;
    } else {
        log_fp = fopen(logfile, "w+");
        if (NULL == log_fp) {
            fprintf(stderr, "open() log error\n");
            return -1;
        }
    }

    return 0;
}

int deinit_log() {
    return fclose(log_fp);
}
