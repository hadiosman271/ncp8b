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

struct Track {
	AVStream *s;
	int idx;
	AVCodecContext *codec_ctx;
	AVCodec *codec;
	struct FrameQueue queue;
	int64_t t1, dt;
};

struct Media {
	const char *url;
	AVFormatContext *format_ctx;
	struct Track video, audio;

	AVPacket *_av_packet;
	AVFrame *_av_frame;
	struct SwsContext *_sws_ctx;
};

struct Media *m_open(const char *url);
void m_close(struct Media *m);

void m_print_info(WINDOW *win, struct Media *m);

void m_set_video_size(struct Media *m, int width, int height);
int m_decode_frame(struct Media *m);
// frame queue functions
AVFrame *m_queue_peek(struct FrameQueue q);
void m_queue_pop(struct FrameQueue *q);
// playback functions
void m_seek(struct Media *m, int t);
void m_toggle_pause(struct Media *m);

#endif /* stream.h */
