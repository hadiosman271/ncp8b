// TODO:
//   error checking
//   handle window resizes
//   media:
//     add pause, play, and seeking
//   player:
//     play audio
//     display in color
//   logs:
//     handle log overflow
//     autoscroll libav logs

#include <stdio.h>

#include <ncurses.h>
#define KEY_ESC 27
#define CTRL(x) ((x) & 0x1f)

#include "media.h"
#include "log.h"

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "terminal video player\n");
		fprintf(stderr, "usage: ncp8b [media file]\n");
		return -1;
	}
	initscr(); cbreak(); noecho(); nonl();
	set_escdelay(1);
	curs_set(0);
	start_color(); use_default_colors();
	for (int i = 0; i < 256; i++)
		init_pair(i, i, -1);

	WINDOW *player = newwin(LINES - 1, COLS, 1, 0);
	WINDOW *info = newwin(LINES - 1, COLS, 1, 0);

	av_log_pad = newpad(1000, COLS);
	int av_log_line = 0;
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

	int height = LINES - 1;
	int width = height * ((float) m->video.s->codecpar->width / m->video.s->codecpar->height);

	wprintw(info, "\nplayer size (pixels): %dx%d\n", width, height);
	wprintw(info, "screen size (chars): %dx%d\n", COLS, LINES);
	media_set_video_size(m, width, height);

#define TABS 3
	WINDOW *tab[TABS] = { player, info, av_log_pad };
	const char *tab_name[TABS] = { "player", "media info", "libav logs" };
	WINDOW *win = player;
	int win_idx = 0;

	move(0, COLS - 30);
	for (int i = 0; i < TABS; i++) {
		nodelay(tab[i], TRUE); keypad(tab[i], TRUE);

		if (tab[i] == win) attrset(A_REVERSE);
		printw("%s", tab_name[i]);
		attrset(A_NORMAL);
		printw("  ");
	}

	mvprintw(0, 0, " >  00:00");
	refresh();

	int ch;
	while ((ch = wgetch(win)) != KEY_ESC && ch != 'q') {
		// scrolling
		if (win == av_log_pad) {
			if (ch == CTRL('u') || ch == KEY_PPAGE)
				av_log_line > 10 ? av_log_line -= 10 : (av_log_line = 0);
			if (ch == CTRL('d') || ch == KEY_NPAGE)
				av_log_line < 1000 - LINES - 10 ? av_log_line += 10 : (av_log_line = 1000 - LINES);
			if (ch == 'g') av_log_line = 0;
			if (ch == 'G') av_log_line = 1000 - LINES;
		}

		// cycle tabs
		if (ch == '\t') {
			win_idx = (win_idx + 1) % TABS;
			win = tab[win_idx];
			move(0, COLS - 30);
			for (int i = 0; i < TABS; i++) {
				if (tab[i] == win) attrset(A_REVERSE);
				printw("%s", tab_name[i]);
				attrset(A_NORMAL);
				printw("  ");
			}
			refresh();
			redrawwin(win);
		}

		struct Frame frame = {0};
		if (media_read_frame(m, &frame) == -1)
			break;
		if (frame.video != NULL) {
			// display frame
			int start = COLS / 2 - width;
			uint8_t *pixel = frame.video->data[0];
			int linesize = frame.video->linesize[0];
			wmove(player, 0, start);
			for (int i = 0; i < width * height; i++) {
				int idx = (i / width) * linesize + (i % width) * 3;
				int brightness = (
					0.299 * pixel[idx]     + // r
					0.587 * pixel[idx + 1] + // g
					0.114 * pixel[idx + 2]   // b
				) / 256. * 13.; // scale [0, 255) to [0, 13)
				char ch = " .,-~:;=!*#$@"[brightness];
				waddch(player, ch); waddch(player, ch);
				if ((i + 1) % width == 0)
					wmove(player, getcury(player) + 1, start);
			}

			mvwprintw(info, 0, 0, "video frame %4d: pts: %7ld\n", frame.video->key_frame, frame.video->pts);
		}
		if (frame.audio != NULL) {
			// play samples

			mvwprintw(info, 1, 0, "audio frame %4d: pts: %7ld\n", frame.audio->key_frame, frame.audio->pts);
		}

		if (win == av_log_pad)
			prefresh(av_log_pad, av_log_line, 0, 1, 0, LINES - 1, COLS);
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
