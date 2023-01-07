#ifndef __COMMON_UTILS_H__
#define __COMMON_UTILS_H__

#include <stdio.h>
#include <tilp2/ticables.h>
#include <stdlib.h>
#include <string.h>

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#endif

typedef enum {
    LEVEL_ERROR,
    LEVEL_WARN,
    LEVEL_INFO,
    LEVEL_DEBUG,
    LEVEL_TRACE,
} LOG_LEVEL;

#define COLOR_RESET "\x1b[00m"
#define COLOR_BLACK "\x1b[30m"
#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN "\x1b[36m"
#define COLOR_WHITE "\x1b[37m"

static char* get_fullfmt(LOG_LEVEL level, const char* fmt) {
    char* color;
    if(level == LEVEL_ERROR) {
        color = COLOR_RED;
    }
    else if(level == LEVEL_WARN) {
        color = COLOR_YELLOW;
    }
    else if(level == LEVEL_DEBUG) {
        color = COLOR_MAGENTA;
    }
    else if(level == LEVEL_TRACE) {
        color = COLOR_CYAN;
    }
    else {
        color = COLOR_RESET;
    }

    char *fullfmt = malloc(10 + strlen(fmt)); \
    sprintf(fullfmt, "%s%s%s", color, fmt, COLOR_RESET); \

    return fullfmt;
}

#define log(level, fmt, values...) { \
    if(level <= current_log_level) { \
        char *fullfmt = get_fullfmt(level, fmt ""); \
        fprintf(stderr, fullfmt , ## values); \
        fflush(stderr); \
        free(fullfmt); \
    } \
}

extern LOG_LEVEL current_log_level;

CableHandle* utils_setup_cable();

void utils_parse_args(int argc, char *argv[]);

#endif