#ifndef LOG_H
#define LOG_H

#include <ncurses.h>

#include <libavutil/log.h>

extern WINDOW *av_log_pad;
void av_log_callback(void *avcl, int level, const char *fmt, va_list vl);

#endif /* log.h */
