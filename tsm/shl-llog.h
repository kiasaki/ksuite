#ifndef SHL_LLOG_H
#define SHL_LLOG_H

#include <stdarg.h>

typedef void (*llog_submit_t) (void *data,
                               const char *file,
                               int line,
                               const char *func,
                               const char *subs,
                               unsigned int sev,
                               const char *format,
                               va_list args);

#define LLOG_FATAL 0
#define LLOG_ALERT 1
#define LLOG_CRITICAL 2
#define LLOG_ERROR 3
#define LLOG_WARNING 4
#define LLOG_NOTICE 5
#define LLOG_INFO 6
#define LLOG_DEBUG 7

#define llog_format(_log, _data, _subs, _sev, _format, ...) do { } while (0)
#define llog_dprintf(_log, _data, _subs, _sev, _format, ...) do { } while (0)
#define llog_debug(_obj, _format, ...) do { } while (0)
#define llog_warning(_obj, _format, ...) do { } while (0)

#endif /* SHL_LLOG_H */
