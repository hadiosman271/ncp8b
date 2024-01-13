#include <ncurses.h>
#include <libavutil/avutil.h>
#include <libavutil/avstring.h>

WINDOW *av_log_pad = NULL;
// modified from libavutil/log.c
void av_log_callback(void *avcl, int level, const char *fmt, va_list vl) {
	if (av_log_pad == NULL) return;

	static int print_prefix = 1;
	static int count;
	static char prev[1024];
	char line[1024];
	AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
	unsigned tint = level & 0xff00;
	static const uint8_t color[8] = {
    	0x41, 0x41, 0x11, 0x03, 9, 0x02, 0x06, 0x07
	};

	level &= 0xff;

	if (level > av_log_get_level())
		return;
	line[0] = 0;
	if (print_prefix && avc) {
		if (avc->parent_log_context_offset) {
			AVClass** parent = *(AVClass ***) (((uint8_t *) avcl) + avc->parent_log_context_offset);
			if (parent && *parent) {
				snprintf(line, sizeof(line), "[%s @ %p] ", (*parent)->item_name(parent), parent);
			}
		}
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(avcl), avcl);
	}

	vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);

	print_prefix = strlen(line) && line[strlen(line) - 1] == '\n';

	if (print_prefix && (av_log_get_flags() & AV_LOG_SKIP_REPEATED) && !strncmp(line, prev, sizeof line)) {
		count++;
		wprintw(av_log_pad, "    Last message repeated %d times\r", count);
		return;
	}
	if (count > 0) {
		wprintw(av_log_pad, "    Last message repeated %d times\n", count);
		count = 0;
	}

	level = av_clip(level >> 3, 0, 8 - 1);
	tint = tint >> 8;
	//if (tint) print_256color(tint);
	wattrset(av_log_pad, COLOR_PAIR(color[level]));
	wprintw(av_log_pad, "%s", line);

	av_strlcpy(prev, line, sizeof line);
}
