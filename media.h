#ifndef STREAM_H
#define STREAM_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <ncurses.h>

#define QUEUE_SIZE 32
struct FrameQueue {
	AVFrame *frame[QUEUE_SIZE];
	size_t start, size;
};

struct Frame {
	AVFrame *audio, *video;
};

struct Track {
	AVStream *s;
	int idx;
	AVCodecContext *codec_ctx;
	AVCodec *codec;
	struct FrameQueue queue;
	int64_t t1;
};

struct Media {
	const char *url;
	AVFormatContext *format_ctx;
	struct Track video, audio;
};

struct Media *media_open(const char *url);
void media_close(struct Media *m);
void media_set_video_size(struct Media *m, int width, int height);
int media_read_frame(struct Media *m, struct Frame *frame);
void media_print_info(WINDOW *win, struct Media *m);

#endif /* stream.h */
