// TODO:
//   error checking
//   play audio
//   sync audio and video
//   display in color
//   add controls

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <ncurses.h>
#define KEY_ESC 27

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: ncp8b [mp4 file]\n");
		return -1;
	}
	AVFormatContext *format_context = avformat_alloc_context();
	avformat_open_input(&format_context, argv[1], NULL, NULL);
	avformat_find_stream_info(format_context, NULL);
	printf("format: %s, duration: %ld\n", format_context->iformat->long_name, format_context->duration);

	AVStream *v_stream = NULL;
	int v_stream_idx = -1;
	AVCodecContext *v_codec_context = NULL;
	AVStream *a_stream = NULL;
	int a_stream_idx = -1;
	AVCodecContext *a_codec_context = NULL;

	for (int i = 0; i < format_context->nb_streams; i++) {
		AVStream *s = format_context->streams[i];

		if (v_stream == NULL && s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			v_stream = s;
			v_stream_idx = i;

			AVCodec *codec = avcodec_find_decoder(s->codecpar->codec_id);
			v_codec_context = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(v_codec_context, s->codecpar);
			avcodec_open2(v_codec_context, codec, NULL);

			printf("video (%d):\n"
				   "  resolution: %dx%d, frame rate: %d/%d, time base: %d/%d\n"
				   "  codec: %s, bit rate: %ld\n",
				i, s->codecpar->width, s->codecpar->height,
				s->avg_frame_rate.num, s->avg_frame_rate.den,
				s->time_base.num, s->time_base.den,
				codec->long_name, s->codecpar->bit_rate
		  	);
		} else if (a_stream == NULL && s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			a_stream = s;
			a_stream_idx = i;

			AVCodec *codec = avcodec_find_decoder(s->codecpar->codec_id);
			a_codec_context = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(a_codec_context, s->codecpar);
			avcodec_open2(a_codec_context, codec, NULL);
	
			printf("audio (%d):\n"
				   "  channels: %d, sample rate: %d, time base: %d/%d\n"
				   "  codec: %s, bit rate: %ld\n",
				i, s->codecpar->channels, s->codecpar->sample_rate,
				s->time_base.num, s->time_base.den,
				codec->long_name, s->codecpar->bit_rate
			);
		}
	}

	initscr(); cbreak(); noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	set_escdelay(1);
	int height = LINES * (5. / 6.);
	int width = height * ((float) v_stream->codecpar->width / v_stream->codecpar->height);

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	AVFrame *rgbframe = av_frame_alloc();
	av_image_alloc(rgbframe->data, rgbframe->linesize, width, height, AV_PIX_FMT_RGB24, av_cpu_max_align());
	struct SwsContext *sws_context = sws_getContext(
		v_stream->codecpar->width, v_stream->codecpar->height,
		v_stream->codecpar->format,
		width, height, AV_PIX_FMT_RGB24, 0, NULL, NULL, NULL
	);

	clock_t v_dt = 0, v_t1 = clock() * v_stream->time_base.den / CLOCKS_PER_SEC;
	//clock_t a_dt = 0, a_t1 = clock() * a_stream->time_base.den / CLOCKS_PER_SEC;

	int ch;
	while ((ch = getch()) != KEY_ESC && ch != 'q') {
		if (av_read_frame(format_context, packet) < 0)
			break;

		if (packet->stream_index == v_stream_idx) {
			avcodec_send_packet(v_codec_context, packet);
			int response = avcodec_receive_frame(v_codec_context, frame);
			if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
				av_packet_unref(packet);
				continue;
			}
			sws_scale(sws_context, (const uint8_t * const *) frame->data, frame->linesize,
				0, v_stream->codecpar->height,
				rgbframe->data, rgbframe->linesize
			);
			// buffer frame
		//} else if (packet->stream_index == a_stream_idx) {
		//	avcodec_send_packet(a_codec_context, packet);
		//    int response = avcodec_receive_frame(a_codec_context, frame);
		//    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
		//    	av_packet_unref(packet);
		//    	continue;
		//    }
		//    // buffer sample
		} else {
			av_packet_unref(packet);
			continue;
		}

		if (packet->stream_index == v_stream_idx) {
			while (v_dt < frame->pts)
				v_dt = clock() * v_stream->time_base.den / CLOCKS_PER_SEC - v_t1;

			uint8_t *pixel = rgbframe->data[0];
			int linesize = rgbframe->linesize[0];

			move(0, 0);
			for (int i = 0; i < width * height; i++) {
				int idx = (i / width) * linesize + (i % width) * 3;
				int brightness = (
					0.299 * pixel[idx]     +
					0.587 * pixel[idx + 1] +
					0.114 * pixel[idx + 2]
				) / 256. * 13.;
				char ch = " .,-~:;=!*#$@"[brightness];
				addch(ch); addch(ch);
				if ((i + 1) % width == 0) addch('\n');
			}
			attrset(A_NORMAL);
			printw("frame %3d: pts: %5ld\n", v_codec_context->frame_number, frame->pts);
			refresh();
		}

		av_packet_unref(packet);

		v_dt = clock() * v_stream->time_base.den / CLOCKS_PER_SEC - v_t1;
		// if (v_dt >= closest v_frame_pts)
			// display frame; free frame

		//a_dt = clock() * a_stream->time_base.den / CLOCKS_PER_SEC - a_t1;
		// if (a_dt >= closest a_frame_pts)
			// play sample; free sample
	}

	endwin();

	av_packet_free(&packet);
	av_frame_free(&frame);

	av_freep(&rgbframe->data[0]);
	av_frame_free(&rgbframe);
	sws_freeContext(sws_context);

	avcodec_free_context(&v_codec_context);
	avcodec_free_context(&a_codec_context);
	avformat_close_input(&format_context);

	return 0;
}
