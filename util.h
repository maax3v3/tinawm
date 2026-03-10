#ifndef UTIL_H
#define UTIL_H

#include <stdarg.h>
#include <stdnoreturn.h>

noreturn void die(const char *fmt, ...);
void warn(const char *fmt, ...);
void spawn(const char *cmd);

#endif /* UTIL_H */
