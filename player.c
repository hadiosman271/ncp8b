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

#include <libavdevice/avdevice.h>

#include "media.h"
#include "log.h"

void audio_prepare_output(AVFormatContext **ctx, AVStream **stream, AVCodecContext **codec_ctx) {
	avdevice_register_all();
	*ctx = NULL;
	*stream = NULL;
	*codec_ctx = NULL;
	AVOutputFormat *device = NULL;

	// there might be a better way to find the first working audio device
	while ((device = av_output_audio_device_next(device)) != NULL) {
		avformat_alloc_output_context2(ctx, device, NULL, NULL);
		*stream = avformat_new_stream(*ctx, NULL);

		AVCodec *codec = avcodec_find_encoder(device->audio_codec);
		*codec_ctx = avcodec_alloc_context3(codec);

		(*codec_ctx)->sample_rate = 196000;
		(*codec_ctx)->channels = 2;
		(*codec_ctx)->channel_layout = av_get_default_channel_layout(2);	

		avcodec_parameters_from_context((*stream)->codecpar, *codec_ctx);
		if (avformat_write_header(*ctx, NULL) >= 0)
			break;

		avcodec_free_context(codec_ctx);
		avformat_free_context(*ctx);
		*ctx = NULL;
		*stream = NULL;
	}

	if (device == NULL)
		av_log(NULL, AV_LOG_ERROR, "can\'t open output audio device\n");
}

void audio_end_output(AVFormatContext **ctx, AVCodecContext **codec_ctx) {
	// add null checks
	av_write_trailer(*ctx);
	avcodec_free_context(codec_ctx);
	avformat_free_context(*ctx);
}

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

	AVFormatContext *audio_output_ctx;
	AVStream *audio_output_stream;
	AVCodecContext *audio_output_codec_ctx;
	audio_prepare_output(&audio_output_ctx, &audio_output_stream, &audio_output_codec_ctx);

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
	media_print_info(info, m);

	int height = LINES - 1;
	int width = height * ((float) m->video.s->codecpar->width / m->video.s->codecpar->height);

	media_set_video_size(m, width, height);
	wprintw(info, "\nplayer size (pixels): %dx%d\n", width, height);
	wprintw(info, "screen size (chars): %dx%d", COLS, LINES);

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

	AVFrame *video = NULL, *audio = NULL;
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

		int ret = media_decode_frame(m);

		if (video == NULL && m->video.queue.size > 0)
			video = m->video.queue.frame[m->video.queue.start];
		if (audio == NULL && m->audio.queue.size > 0)
			audio = m->audio.queue.frame[m->audio.queue.start];

		if (ret == -1 && video == NULL && audio == NULL)
			break;
 
		if (video != NULL && m->video.dt >= video->pts) {
			// display frame
			int start = COLS / 2 - width;
			uint8_t *pixel = video->data[0];
			int linesize = video->linesize[0];
			wmove(player, 0, start);
			for (int i = 0; i < width * height; i++) {
				int idx = (i / width) * linesize + (i % width) * 3;
				int brightness = (
					0.299 * pixel[idx]     + // r
					0.587 * pixel[idx + 1] + // g
					0.114 * pixel[idx + 2]   // b
				) / 256. * 13.; // scale [0, 256) to [0, 13)
				char ch = " .,-~:;=!*#$@"[brightness];
				waddch(player, ch); waddch(player, ch);
				if ((i + 1) % width == 0)
					wmove(player, getcury(player) + 1, start);
			}

			wmove(info, 12, 0);
			wprintw(info, "video:\n"
					"  frame: %4d/%4d: pts: %6ld dt: %6ld\n"
					"  queue: start: %2ld size: %2ld\n",
				video->key_frame, m->video.codec_ctx->frame_number, video->pts, m->video.dt,
				m->video.queue.start, m->video.queue.size
			);

			video = NULL;
			m->video.queue.start = (m->video.queue.start + 1) % QUEUE_SIZE;
			m->video.queue.size--;
		}
		if (audio != NULL && m->audio.dt >= audio->pts) {
			// show audio bar

			// TODO: encode and write audio frame
			//avcodec_send_frame(audio_output_codec_ctx, audio);
			//int response = avcodec_receive_packet(audio_output_codec_ctx, m->_av_packet);
			//if (response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
			//	m->_av_packet->stream_index = 0;
			//	av_packet_rescale_ts(m->_av_packet, m->audio.s->time_base, audio_output_stream->time_base);
			//	av_write_frame(audio_output_ctx, m->_av_packet);
			//	av_packet_unref(m->_av_packet);
			//}

			wmove(info, 15, 0);
			wprintw(info, "audio:\n"
					"  frame: %4d/%4d: pts: %7ld dt: %7ld\n"
					"  queue: start: %2ld size: %2ld\n",
				audio->key_frame, m->audio.codec_ctx->frame_number, audio->pts, m->audio.dt,
				m->audio.queue.start, m->audio.queue.size
			);

			audio = NULL;
			m->audio.queue.start = (m->audio.queue.start + 1) % QUEUE_SIZE;
			m->audio.queue.size--;
		}

		if (win == av_log_pad)
			prefresh(av_log_pad, av_log_line, 0, 1, 0, LINES - 1, COLS);
		else
			wrefresh(win);
	}

	audio_end_output(&audio_output_ctx, &audio_output_codec_ctx);

	media_close(m);

	delwin(player);
	delwin(info);
	delwin(av_log_pad);
	endwin();

	return 0;
}
