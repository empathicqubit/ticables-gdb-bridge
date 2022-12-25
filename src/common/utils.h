#ifndef __COMMON_UTILS_H__
#define __COMMON_UTILS_H__

#include <stdio.h>
#include <tilp2/ticables.h>

typedef enum {
    LEVEL_ERROR,
    LEVEL_WARN,
    LEVEL_INFO,
    LEVEL_DEBUG,
    LEVEL_TRACE,
} LOG_LEVEL;

#define log(level, fmt, values...) if(level <= current_log_level) { fprintf(stderr, fmt, ## values); fflush(stderr); }

extern LOG_LEVEL current_log_level;

CableHandle* utils_setup_cable();

void utils_parse_args(int argc, char *argv[]);

#endif