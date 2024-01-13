// TODO:
//   error checking
//   play audio
//   display in color
//   add controls
//   make player fill window
//   handle window resizes
//   display libav logs properly

#include <stdio.h>

#include <ncurses.h>
#define KEY_ESC 27

#include "media.h"
#include "log.h"

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "terminal video player\n");
		fprintf(stderr, "usage: ncp8b [media file]\n");
		return -1;
	}
	initscr(); cbreak(); noecho();
	set_escdelay(1);
	curs_set(0);
	start_color(); use_default_colors();
	for (int i = 0; i < 256; i++)
		init_pair(i, i, -1);

	WINDOW *player = newwin(0, 0, 0, 0);
	WINDOW *info = newwin(0, 0, 0, 0);

	av_log_pad = newpad(1000, COLS);
	int av_log_line = LINES;
	av_log_set_callback(av_log_callback);
	av_log_set_level(AV_LOG_DEBUG);

	struct Media *m = media_open(argv[1]);
	if (m == NULL) {
		refresh();
		getch();
		delwin(player);
		delwin(info);
		delwin(av_log_pad);
		endwin();
		return -1;
	}
	wmove(info, 3, 0);
	media_print_info(info, m);

	int height = LINES;
	int width = height * ((float) m->video.s->codecpar->width / m->video.s->codecpar->height);

	media_set_video_size(m, width, height);

	nodelay(player,     TRUE); keypad(player,     TRUE);
	nodelay(info,       TRUE); keypad(info,       TRUE);
	nodelay(av_log_pad, TRUE); keypad(av_log_pad, TRUE);

	WINDOW *win = player;
	int ch;
	while ((ch = wgetch(win)) != KEY_ESC && ch != 'q') {
		// scrolling
		if (ch == KEY_PPAGE && win == av_log_pad)
			av_log_line > 10 ? av_log_line -= 10 : (av_log_line = 0);
		if (ch == KEY_NPAGE && win == av_log_pad)
			av_log_line < 1000 - LINES - 10 ? av_log_line += 10 : (av_log_line = 1000 - LINES);

		// cycle tabs
		if (ch == '\t') {
			if (win == player) win = info;
			else if (win == info) win = av_log_pad;
			else if (win == av_log_pad) win = player;
			redrawwin(win);
		}

		struct Frame frame = {0};
		if (media_read_frame(m, &frame) == -1)
			break;
		if (frame.video != NULL) {
			// display frame
			uint8_t *pixel = frame.video->data[0];
			int linesize = frame.video->linesize[0];
			wmove(player, 0, 0);
			for (int i = 0; i < width * height; i++) {
				int idx = (i / width) * linesize + (i % width) * 3;
				int brightness = (
					0.299 * pixel[idx]     + // r
					0.587 * pixel[idx + 1] + // g
					0.114 * pixel[idx + 2]   // b
				) / 256. * 13.; // scale [0, 255) to [0, 13)
				char ch = " .,-~:;=!*#$@"[brightness];
				waddch(player, ch); waddch(player, ch);
				if ((i + 1) % width == 0) waddch(player, '\n');
			}

			mvwprintw(info, 0, 0, "video frame %3d: pts: %5ld\n", frame.video->key_frame, frame.video->pts);
		}
		if (frame.audio != NULL) {
			// play samples

			mvwprintw(info, 1, 0, "audio frame %3d: pts: %6ld\n", frame.audio->key_frame, frame.audio->pts);
		}

		if (win == av_log_pad)
			prefresh(av_log_pad, av_log_line, 0, 0, 0, LINES - 1, COLS);
		else
			wrefresh(win);
	}

	media_close(m);

	delwin(player);
	delwin(info);
	delwin(av_log_pad);
	endwin();
	return 0;
}
