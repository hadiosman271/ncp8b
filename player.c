// TODO:
//   use less cpu
//   error checking
//   handle window resizes
//   load video file in main loop
//   media:
//     add pause, play, and seeking
//   player:
//     play audio
//     display in color
//   logs:
//     handle log overflow
//     fix log levels

#include <stdio.h>

#include <ncurses.h>
#define KEY_ESC 27
#define CTRL(x) ((x) & 0x1f)

#include "media.h"
#include "log.h"

typedef struct {
	int player_width, player_height;

	int log_cur_line;
	bool log_snap_end;

#define TABS 3
	WINDOW *tab[TABS];
	const char *tab_name[TABS];

	WINDOW *win;
	int win_idx;
} State;


void resize_player(State *s, struct Media *m) {
	s->player_height = LINES - 1;
	s->player_width = s->player_height * ((float) m->video.s->codecpar->width / m->video.s->codecpar->height);

	m_set_video_size(m, s->player_width, s->player_height);
}

void print_tab_bar(State s) {
	move(0, COLS - 30);
	for (int i = 0; i < TABS; i++) {
		if (s.tab[i] == s.win) {
			attrset(A_REVERSE);
		}
		printw("%s", s.tab_name[i]);
		attrset(A_NORMAL);
		printw("  ");
	}
}

void print_playback_info(State s) {
	// fake!
	mvprintw(0, 0, " >  00:00");
}

void print_frame_info(State s, struct Media *m, AVFrame *video, AVFrame *audio) {
	WINDOW *info = s.tab[1];

	if (video != NULL) {
		wmove(info, 12, 0);
		wprintw(info, "video:\n"
				"  frame: %4d/%4d: pts: %6ld dt: %6ld\n"
				"  queue: start: %2ld size: %2ld\n",
			video->key_frame, m->video.codec_ctx->frame_number, video->pts, m->video.dt,
			m->video.queue.start, m->video.queue.size
		);
	}

	if (audio != NULL) {
		wmove(info, 15, 0);
		wprintw(info, "audio:\n"
				"  frame: %4d/%4d: pts: %7ld dt: %7ld\n"
				"  queue: start: %2ld size: %2ld\n",
			audio->key_frame, m->audio.codec_ctx->frame_number, audio->pts, m->audio.dt,
			m->audio.queue.start, m->audio.queue.size
		);
	}
}

int rgb2ansi(uint8_t *pixel) {
	uint8_t r = pixel[0], g = pixel[1], b = pixel[2];
	// TODO
	return 0;
}

void print_frame(State s, AVFrame *video) {
	WINDOW *player = s.tab[0];

	int start = COLS / 2 - s.player_width;
	uint8_t *pixel = video->data[0];
	int linesize = video->linesize[0];

	wmove(player, 0, start);
	for (int i = 0; i < s.player_width * s.player_height; i++) {
		int idx = (i / s.player_width) * linesize + (i % s.player_width) * 3;

		int brightness = (
			0.299 * pixel[idx]     + // r
			0.587 * pixel[idx + 1] + // g
			0.114 * pixel[idx + 2]   // b
		) / 256. * 13.; // scale [0, 256) to [0, 13)

		attrset(COLOR_PAIR(rgb2ansi(pixel)));
		char ch = " .,-~:;=!*#$@"[brightness];
		waddch(player, ch); waddch(player, ch);
		attrset(A_NORMAL);

		if ((i + 1) % s.player_width == 0) {
			wmove(player, getcury(player) + 1, start);
		}
	}
}

void print_audio_bar(State s) {
	// TODO
}

void end_all(State s) {
	for (int i = 0; i < TABS; i++) {
		delwin(s.tab[i]);
	}
	endwin();
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "terminal video player\n");
		fprintf(stderr, "usage: ncp8b [video_file_path]\n");
		return -1;
	}

	initscr(); cbreak(); noecho(); nonl();
	set_escdelay(1);
	curs_set(0);
	start_color(); use_default_colors();
	for (int i = 0; i < 256; i++) {
		init_pair(i, i, -1);
	}

	State s = {
		.tab_name[0] = "player",     .tab[0] = newwin(LINES - 1, COLS, 1, 0),
		.tab_name[1] = "media info", .tab[1] = newwin(LINES - 1, COLS, 1, 0),
		.tab_name[2] = "libav logs", .tab[2] = newpad(LOG_MAX_LINES, COLS),
	};

	WINDOW *player = s.tab[0];
	WINDOW *info   = s.tab[1];
	av_log_pad     = s.tab[2];

	s.win = player;
	s.win_idx = 0;

	// settings for every tab
	for (int i = 0; i < TABS; i++) {
		nodelay(s.tab[i], TRUE);
		keypad(s.tab[i], TRUE);
	}

	// init logging
	extern int av_log_lines;
	s.log_cur_line = 0;
	s.log_snap_end = true;
	av_log_set_callback(av_log_callback);
	av_log_set_level(AV_LOG_INFO);

	// TODO: load file in main loop
	struct Media *m = m_open(argv[1]);
	if (m == NULL) {
		refresh(); // to show any errors
		getch();

		end_all(s);
		return -1;
	}

	m_print_info(info, m);

	resize_player(&s, m);

	wprintw(info, "\nplayer size (pixels): %dx%d\n", s.player_width, s.player_height);
	wprintw(info, "screen size (chars): %dx%d", COLS, LINES);

	print_tab_bar(s);
	print_playback_info(s);

	refresh();

	// TODO
	const char *filename = NULL;
	bool reading_line = false;

	AVFrame *video = NULL, *audio = NULL;
	int ch;
	while ((ch = wgetch(s.win)) != KEY_ESC && ch != 'q') {
		if (!reading_line) {
			if (ch == ':') {
				reading_line = true;
			}
			// scrolling
			if (s.win == av_log_pad) {
				switch (ch) {
				case CTRL('u'): case KEY_PPAGE:
					s.log_cur_line > 10 ? s.log_cur_line -= 10 : (s.log_cur_line = 0);
					s.log_snap_end = false;
					break;
				case CTRL('d'): case KEY_NPAGE:
					s.log_cur_line < av_log_lines - LINES - 10 ? s.log_cur_line += 10 : (s.log_cur_line = av_log_lines - LINES);
					s.log_snap_end = false;
					break;
				case 'g':
					s.log_cur_line = 0;
					s.log_snap_end = false;
					break;
				case 'G':
					s.log_cur_line = av_log_lines - LINES;
					s.log_snap_end = false;
					break;
				}

				if (s.log_snap_end) {
					s.log_cur_line = av_log_lines - LINES;
				} else if (s.log_cur_line == av_log_lines - LINES) {
					s.log_snap_end = true;
				}
			} else if (s.win == player) {
				// seeking
				switch (ch) {
				case KEY_LEFT:
					m_seek(m, -5000);
					break;
				case KEY_RIGHT:
					m_seek(m, 5000);
					break;
				case ' ':
					m_toggle_pause(m);
					break;
				}
			}

			if (ch == '\t') {
				// cycle tabs
				s.win_idx = (s.win_idx + 1) % TABS;
				s.win = s.tab[s.win_idx];

				print_tab_bar(s);
				refresh();
				redrawwin(s.win);
			}
		}
		if (reading_line) {
			// read file name
		}


		int ret = m_decode_frame(m);
		if (ret == -1 && video == NULL && audio == NULL) {
			break;
		}

		if (video == NULL) {
			video = m_queue_peek(m->video.queue);
		}
		if (audio == NULL) {
			audio = m_queue_peek(m->audio.queue);
		}
		print_frame_info(s, m, video, audio);
 
		if (video != NULL && m->video.dt >= video->pts) {
			print_frame(s, video);

			m_queue_pop(&m->video.queue);
			video = NULL;
		}
		if (audio != NULL && m->audio.dt >= audio->pts) {
			print_audio_bar(s);

			m_queue_pop(&m->audio.queue);
			audio = NULL;
		}

		if (s.win == av_log_pad) {
			prefresh(av_log_pad, s.log_cur_line, 0, 1, 0, LINES - 1, COLS);
		}
		else {
			wrefresh(s.win);
		}
	}

	m_close(m);

	end_all(s);

	return 0;
}
