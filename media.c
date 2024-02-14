#include <time.h>

#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "media.h"

int open_codec(struct Track *t) {
	t->codec = avcodec_find_decoder(t->s->codecpar->codec_id);
	t->codec_ctx = avcodec_alloc_context3(t->codec);
	avcodec_parameters_to_context(t->codec_ctx, t->s->codecpar);
	avcodec_open2(t->codec_ctx, t->codec, NULL);
	return 0;
}

struct Media *media_open(const char *url) {
	av_log_set_level(AV_LOG_DEBUG);
	struct Media *m = calloc(1, sizeof(struct Media));
	m->format_ctx = NULL;
	m->url = url;
	if (avformat_open_input(&m->format_ctx, m->url, NULL, NULL) < 0) {
		printw("error: failed to open %s\n", m->url);
		free(m);
		return NULL;
	}

	avformat_find_stream_info(m->format_ctx, NULL);
	for (int i = 0; i < m->format_ctx->nb_streams; i++) {
		AVStream *stream = m->format_ctx->streams[i];

		if (m->video.s == NULL && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			m->video.s = stream;
			m->video.idx = i;
			open_codec(&m->video);
		} else if (m->audio.s == NULL && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			m->audio.s = stream;
			m->audio.idx = i;
			open_codec(&m->audio);
		}
	}

	m->_av_frame = av_frame_alloc();
	m->_av_packet = av_packet_alloc();
	for (int i = 0; i < QUEUE_SIZE; i++) {
		m->video.queue.frame[i] = av_frame_alloc();
		av_image_alloc(m->video.queue.frame[i]->data, m->video.queue.frame[i]->linesize,
			m->video.s->codecpar->width, m->video.s->codecpar->height,
			AV_PIX_FMT_RGB24, av_cpu_max_align()
		);
		m->audio.queue.frame[i] = av_frame_alloc();
	}
	m->_sws_ctx = sws_getContext(
		m->video.s->codecpar->width, m->video.s->codecpar->height,
		m->video.s->codecpar->format,
		m->video.s->codecpar->width, m->video.s->codecpar->height,
		AV_PIX_FMT_RGB24, 0, NULL, NULL, NULL
	);

	clock_t t = clock();
	m->video.t1 = t * m->video.s->time_base.den / CLOCKS_PER_SEC;
	m->audio.t1 = t * m->audio.s->time_base.den / CLOCKS_PER_SEC;

	return m;
}

void media_close(struct Media *m) {
	for (int i = 0; i < QUEUE_SIZE; i++) {
		av_freep(&m->video.queue.frame[i]->data[0]);
		av_frame_free(&m->video.queue.frame[i]);
	}
	for (int i = 0; i < QUEUE_SIZE; i++) {
		av_frame_free(&m->audio.queue.frame[i]);
	}

	av_packet_free(&m->_av_packet);
	av_frame_free(&m->_av_frame);
	sws_freeContext(m->_sws_ctx);

	avcodec_free_context(&m->video.codec_ctx);
	avcodec_free_context(&m->audio.codec_ctx);
	avformat_close_input(&m->format_ctx);
	free(m);
}

void media_set_video_size(struct Media *m, int width, int height) {
	for (int i = 0; i < QUEUE_SIZE; i++) {
		av_freep(&m->video.queue.frame[i]->data[0]);
		av_image_alloc(m->video.queue.frame[i]->data, m->video.queue.frame[i]->linesize,
			width, height, AV_PIX_FMT_RGB24, av_cpu_max_align()
		);
	}
	sws_freeContext(m->_sws_ctx);
	m->_sws_ctx = sws_getContext(
		m->video.s->codecpar->width, m->video.s->codecpar->height,
		m->video.s->codecpar->format,
		width, height, AV_PIX_FMT_RGB24, 0, NULL, NULL, NULL
	);
}

void media_print_info(WINDOW *win, struct Media *m) {
	wprintw(win, "%s:\nformat: %s, duration: %ld\n",
		m->url, m->format_ctx->iformat->long_name, m->format_ctx->duration
	);
	wprintw(win, "video (%d):\n"
		   "  resolution: %dx%d, frame rate: %d/%d, time base: %d/%d\n"
		   "  codec: %s, bit rate: %ld\n",
		m->video.idx, m->video.s->codecpar->width, m->video.s->codecpar->height,
		m->video.s->avg_frame_rate.num, m->video.s->avg_frame_rate.den,
		m->video.s->time_base.num, m->video.s->time_base.den,
		m->video.codec->long_name, m->video.s->codecpar->bit_rate
	);

	wprintw(win, "audio (%d):\n"
		   "  channels: %d, sample rate: %d, time base: %d/%d\n"
		   "  codec: %s, bit rate: %ld\n",
		m->audio.idx, m->audio.s->codecpar->channels, m->audio.s->codecpar->sample_rate,
		m->audio.s->time_base.num, m->audio.s->time_base.den,
		m->audio.codec->long_name, m->audio.s->codecpar->bit_rate
	);
}

int media_decode_frame(struct Media *m) {
	if (m->video.queue.size < QUEUE_SIZE && m->audio.queue.size < QUEUE_SIZE) {
		if (av_read_frame(m->format_ctx, m->_av_packet) < 0)
			return -1;
		if (m->_av_packet->stream_index == m->video.idx) {
			avcodec_send_packet(m->video.codec_ctx, m->_av_packet);

			int response = avcodec_receive_frame(m->video.codec_ctx, m->_av_frame);
			if (response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
				AVFrame *v_frame = m->video.queue.frame[(m->video.queue.start + m->video.queue.size) % QUEUE_SIZE];

				sws_scale(m->_sws_ctx, (const uint8_t * const *) m->_av_frame->data, m->_av_frame->linesize,
					0, m->video.s->codecpar->height, v_frame->data, v_frame->linesize
				);

				v_frame->key_frame = m->video.codec_ctx->frame_number; // using key_frame to store frame number
				v_frame->pts = m->_av_frame->pts;
				m->video.queue.size++;
			}	
		} else if (m->_av_packet->stream_index == m->audio.idx) {
			avcodec_send_packet(m->audio.codec_ctx, m->_av_packet);

			AVFrame *a_frame = m->audio.queue.frame[(m->audio.queue.start + m->audio.queue.size) % QUEUE_SIZE];

			int response = avcodec_receive_frame(m->audio.codec_ctx, a_frame);
			if (response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
				a_frame->key_frame = m->audio.codec_ctx->frame_number; // using key_frame to store frame number
				m->audio.queue.size++;
			}	
		}
		av_packet_unref(m->_av_packet);
	}

	clock_t t = clock();
	m->video.dt = t * m->video.s->time_base.den / CLOCKS_PER_SEC - m->video.t1;
	m->audio.dt = t * m->audio.s->time_base.den / CLOCKS_PER_SEC - m->audio.t1;

	return 0;
}
