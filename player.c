// TODO:
//   display frames on time
//   play audio with pipewire/pulse
//   display in color
//   error checking
//   add controls

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <ncurses.h>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: ncp8b [mp4 file]\n");
		return -1;
	}
	av_log_set_level(AV_LOG_ERROR);
	AVFormatContext *format_context = avformat_alloc_context();
	avformat_open_input(&format_context, argv[1], NULL, NULL);
	printf("format: %s, duration: %ld\n\n", format_context->iformat->long_name, format_context->duration);

	AVCodecContext *video_codec_context = NULL;
	AVStream *video_stream = NULL;
	int video_stream_index = -1;
	AVCodecContext *audio_codec_context = NULL;
	int audio_stream_index = -1;

	avformat_find_stream_info(format_context, NULL);
	for (int i = 0; i < format_context->nb_streams; i++) {
		AVCodecParameters *codec_parameters = format_context->streams[i]->codecpar;
		AVCodec *codec = avcodec_find_decoder(codec_parameters->codec_id);

		if (codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
			AVStream *s = format_context->streams[i];
			printf("video codec: resolution: %dx%d, ", codec_parameters->width, codec_parameters->height);
			printf("avg frame rate: %g, r frame rate: %g, time base: %g\n",
				(float) s->avg_frame_rate.num / s->avg_frame_rate.den,
				(float) s->r_frame_rate.num / s->r_frame_rate.den,
				(float) s->time_base.num / s->time_base.den
			);
			if (video_codec_context == NULL) {
				video_stream_index = i;
				video_stream = s;
				video_codec_context = avcodec_alloc_context3(codec);
				avcodec_parameters_to_context(video_codec_context, codec_parameters);
				avcodec_open2(video_codec_context, codec, NULL);
			}
		} else if (codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
			printf("audio codec: channels: %d, sample rate: %d\n", codec_parameters->channels, codec_parameters->sample_rate);
			if (audio_codec_context == NULL) {
				audio_stream_index = i;
				audio_codec_context = avcodec_alloc_context3(codec);
				avcodec_parameters_to_context(audio_codec_context, codec_parameters);
				avcodec_open2(audio_codec_context, codec, NULL);
			}
		}
		printf("codec: %s, id: %d, bitrate: %ld\n", codec->long_name, codec->id, codec_parameters->bit_rate);
	}
	printf("\n");

	initscr(); cbreak(); noecho();
	char brightness_char[13] = " .,-~:;=!*#$@";
	int height = (float) LINES * (3. / 4.);
	int width = height * ((float) video_stream->codecpar->width / video_stream->codecpar->height);

	struct SwsContext *sws_context = sws_getContext(
		video_stream->codecpar->width,
		video_stream->codecpar->height,
		video_stream->codecpar->format,
		width, height, AV_PIX_FMT_RGB24, 0, NULL, NULL, NULL
	);
	AVFrame *rgbframe = av_frame_alloc();
	av_image_alloc(rgbframe->data, rgbframe->linesize, width, height, AV_PIX_FMT_RGB24, 1);

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	while (av_read_frame(format_context, packet) >= 0) {
		AVCodecContext *codec_context = NULL;
		if (packet->stream_index == video_stream_index) {
			codec_context = video_codec_context;
		} else if (packet->stream_index == audio_stream_index) {
			codec_context = audio_codec_context;
		} else {
			continue;
		}
		avcodec_send_packet(codec_context, packet);
		int response = avcodec_receive_frame(codec_context, frame);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
			av_packet_unref(packet);
			continue;
		}
		if (packet->stream_index == video_stream_index) {
			sws_scale(sws_context, (const uint8_t * const *) frame->data, frame->linesize, 0,
				video_stream->codecpar->height,
				rgbframe->data, rgbframe->linesize
			);
			uint8_t *pixel = rgbframe->data[0];

			move(0, 0);
			for (int i = 0; i < width * height; i++) {
				int index = (
					0.299 * pixel[i * 3 + 1] +
					0.587 * pixel[i * 3 + 1] +
					0.114 * pixel[i * 3 + 1]
				) / 256. * 13.;
				char ch = brightness_char[index];
				addch(ch); addch(ch);
				attrset(A_NORMAL);
				if ((i + 1) % width == 0) addch('\n');
			}
			printw("frame %3d: pts: %5ld, dts: %5ld\n", codec_context->frame_number, frame->pts, frame->pkt_dts);
			refresh();
			getch();
		}
		av_packet_unref(packet);
	}

	endwin();

	av_packet_free(&packet);
	av_frame_free(&frame);

	av_freep(&rgbframe->data[0]);
	av_frame_free(&rgbframe);
	sws_freeContext(sws_context);

	avcodec_free_context(&video_codec_context);
	avcodec_free_context(&audio_codec_context);
	avformat_close_input(&format_context);

	return 0;
}
