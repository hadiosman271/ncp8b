// modified from ffmpeg/libavutil/log.c (commit d61977c)

#include <libavutil/bprint.h>
#include "log.h"

WINDOW *av_log_pad = NULL;

static const uint32_t color[16 + AV_CLASS_CATEGORY_NB] = {
    [AV_LOG_PANIC  / 8] =  52 << 16 | 196 << 8 | 0x41,
    [AV_LOG_FATAL  / 8] = 208 <<  8 | 0x41,
    [AV_LOG_ERROR  / 8] = 196 <<  8 | 0x11,
    [AV_LOG_WARNING/ 8] = 226 <<  8 | 0x03,
    [AV_LOG_INFO   / 8] = 253 <<  8 | 0x09,
    [AV_LOG_VERBOSE/ 8] =  40 <<  8 | 0x02,
    [AV_LOG_DEBUG  / 8] =  34 <<  8 | 0x02,
    [AV_LOG_TRACE  / 8] =  34 <<  8 | 0x07,
    [16 + AV_CLASS_CATEGORY_NA              ] = 250 << 8 | 0x09,
    [16 + AV_CLASS_CATEGORY_INPUT           ] = 219 << 8 | 0x15,
    [16 + AV_CLASS_CATEGORY_OUTPUT          ] = 201 << 8 | 0x05,
    [16 + AV_CLASS_CATEGORY_MUXER           ] = 213 << 8 | 0x15,
    [16 + AV_CLASS_CATEGORY_DEMUXER         ] = 207 << 8 | 0x05,
    [16 + AV_CLASS_CATEGORY_ENCODER         ] =  51 << 8 | 0x16,
    [16 + AV_CLASS_CATEGORY_DECODER         ] =  39 << 8 | 0x06,
    [16 + AV_CLASS_CATEGORY_FILTER          ] = 155 << 8 | 0x12,
    [16 + AV_CLASS_CATEGORY_BITSTREAM_FILTER] = 192 << 8 | 0x14,
    [16 + AV_CLASS_CATEGORY_SWSCALER        ] = 153 << 8 | 0x14,
    [16 + AV_CLASS_CATEGORY_SWRESAMPLER     ] = 147 << 8 | 0x14,
    [16 + AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT ] = 213 << 8 | 0x15,
    [16 + AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT  ] = 207 << 8 | 0x05,
    [16 + AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT ] = 213 << 8 | 0x15,
    [16 + AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT  ] = 207 << 8 | 0x05,
    [16 + AV_CLASS_CATEGORY_DEVICE_OUTPUT       ] = 213 << 8 | 0x15,
    [16 + AV_CLASS_CATEGORY_DEVICE_INPUT        ] = 207 << 8 | 0x05,
};

static void colored_fputs(int level, int tint, const char *str) {
    if (!*str)
        return;

	if (tint)
    	wattrset(av_log_pad, tint);
	else
    	wattrset(av_log_pad, COLOR_PAIR((color[level] >> 8) & 0xff));
	wprintw(av_log_pad, "%s", str);
}

static void sanitize(char *line) {
    while (*line) {
        if (*line < 0x08 || (*line > 0x0D && *line < 0x20))
            *line = '?';
        line++;
    }
}

static int get_category(void *ptr) {
    AVClass *avc = *(AVClass **) ptr;
    if (!avc
        || (avc->version & 0xFF) < 100
        ||  avc->version < (51 << 16 | 59 << 8)
        ||  avc->category >= AV_CLASS_CATEGORY_NB
	)
		return AV_CLASS_CATEGORY_NA + 16;

    if (avc->get_category)
        return avc->get_category(ptr) + 16;

    return avc->category + 16;
}

static const char *get_level_str(int level) {
	switch (level) {
	case AV_LOG_QUIET:
		return "quiet";
	case AV_LOG_DEBUG:
		return "debug";
	case AV_LOG_TRACE:
		return "trace";
	case AV_LOG_VERBOSE:
		return "verbose";
	case AV_LOG_INFO:
		return "info";
	case AV_LOG_WARNING:
		return "warning";
	case AV_LOG_ERROR:
		return "error";
	case AV_LOG_FATAL:
		return "fatal";
	case AV_LOG_PANIC:
		return "panic";
	default:
		return "";
	}
}

static void format_line(
	void *avcl, int level, const char *fmt, va_list vl,
	AVBPrint part[4], int *print_prefix, int type[2]
) {
    AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
    av_bprint_init(part + 0, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprint_init(part + 1, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprint_init(part + 2, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprint_init(part + 3, 0, 65536);

    if (type) type[0] = type[1] = AV_CLASS_CATEGORY_NA + 16;
    if (*print_prefix && avc) {
        if (avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) avcl) + avc->parent_log_context_offset);
            if (parent && *parent) {
                av_bprintf(part + 0, "[%s @ %p] ", (*parent)->item_name(parent), parent);
                if (type) type[0] = get_category(parent);
            }
        }
        av_bprintf(part + 1, "[%s @ %p] ", avc->item_name(avcl), avcl);
        if (type) type[1] = get_category(avcl);
    }

    if (*print_prefix && (level > AV_LOG_QUIET) && (av_log_get_flags() & AV_LOG_PRINT_LEVEL))
        av_bprintf(part + 2, "[%s] ", get_level_str(level));

    av_vbprintf(part + 3, fmt, vl);

    if (*part[0].str || *part[1].str || *part[2].str || *part[3].str) {
        char lastc = part[3].len && part[3].len <= part[3].size ? part[3].str[part[3].len - 1] : 0;
        *print_prefix = lastc == '\n' || lastc == '\r';
    }
}

#define LINE_SZ 1024
#define NB_LEVELS 8

void av_log_callback(void* ptr, int level, const char* fmt, va_list vl) {
    static int print_prefix = 1;
    static int count;
    static char prev[LINE_SZ];
    AVBPrint part[4];
    char line[LINE_SZ];
    int type[2];
    unsigned tint = 0;

    if (level >= 0) {
        tint = level & 0xff00;
        level &= 0xff;
    }

    if (level > av_log_get_level())
        return;

    format_line(ptr, level, fmt, vl, part, &print_prefix, type);
    snprintf(line, sizeof(line), "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);

    if (print_prefix && (av_log_get_flags() & AV_LOG_SKIP_REPEATED) && !strcmp(line, prev) &&
        *line && line[strlen(line) - 1] != '\r') {
        count++;
        wprintw(av_log_pad, "    Last message repeated %d times\r", count);
		av_bprint_finalize(part + 3, NULL);
		return;
    }
    if (count > 0) {
        wprintw(av_log_pad, "    Last message repeated %d times\n", count);
        count = 0;
    }

    strcpy(prev, line);
    sanitize(part[0].str);
    colored_fputs(type[0], 0, part[0].str);
    sanitize(part[1].str);
    colored_fputs(type[1], 0, part[1].str);
    sanitize(part[2].str);
    colored_fputs(av_clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[2].str);
    sanitize(part[3].str);
    colored_fputs(av_clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[3].str);
}
