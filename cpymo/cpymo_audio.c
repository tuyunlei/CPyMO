#include "cpymo_audio.h"
#include <assert.h>
#include <cpymo_backend_audio.h>

void cpymo_audio_channel_reset(cpymo_audio_channel *c)
{
	if (c->swr_context) swr_free(&c->swr_context);
	if (c->codec_context) avcodec_free_context(&c->codec_context);
	if (c->format_context) avformat_close_input(&c->format_context);

	cpymo_audio_channel_init(c);
}

static int cpymo_audio_fmt2ffmpeg(
	int f) {
	switch (f) {
	case cpymo_backend_audio_s16le: return AV_SAMPLE_FMT_S16;
	}

	assert(false);
	return -1;
}

static bool cpymo_audio_channel_next_frame(cpymo_audio_channel *c) 
{
	int result = av_read_frame(c->format_context, c->packet);
	result = avcodec_send_packet(c->codec_context, c->packet);
	if (result < 0) {
		cpymo_audio_channel_reset(c);
		return false;
	}

	result = avcodec_receive_frame(c->codec_context, c->frame);
	if (result < 0) {
		cpymo_audio_channel_reset(c);
		return false;
	}

	av_packet_unref(c->packet);

	const cpymo_backend_audio_info *info = cpymo_backend_audio_get_info();

	if (result == 0) {
		size_t out_size = av_samples_get_buffer_size(
			NULL, 
			(int)info->channels,
			c->frame->nb_samples,
			cpymo_audio_fmt2ffmpeg(info->format),
			1);

		if (out_size > c->converted_buf_all_size) {
			c->converted_buf = (uint8_t *)realloc(c->converted_buf, out_size);
			if (c->converted_buf == NULL) {
				cpymo_audio_channel_reset(c);
				return false;
			}
			c->converted_buf_all_size = out_size;
		}

		int samples = swr_convert(
			c->swr_context,
			&c->converted_buf, 
			c->frame->nb_samples, 
			(const uint8_t **)c->frame->data,
			c->frame->nb_samples);

		if (samples < 0) {
			const char *err = av_err2str(result);
			printf("[Warning] swr_convert: %s.", err);
			return false;
		}
		c->converted_buf_size = av_samples_get_buffer_size(
			NULL,
			info->channels,
			c->frame->nb_samples,
			cpymo_audio_fmt2ffmpeg(info->format),
			1);

		c->converted_frame_current_offset = 0;
		return true;
	}
	else {
		
		if (result >= 0) return cpymo_audio_channel_next_frame(c);
		else return false;
	}
}

static void cpymo_audio_channel_write_samples(uint8_t *dst, size_t len, cpymo_audio_channel *c)
{
	const uint8_t *data = c->converted_buf;
	const size_t data_size = c->converted_buf_size;

	size_t write_bytes = data_size - c->converted_frame_current_offset;
	if (write_bytes >= len) write_bytes = len;

	memcpy(dst, data, write_bytes);

	c->converted_frame_current_offset += write_bytes;

	if (c->converted_frame_current_offset >= data_size) {
		if (cpymo_audio_channel_next_frame(c)) {
			if (write_bytes < len) {
				cpymo_audio_channel_write_samples(dst + write_bytes, len - write_bytes, c);
			}
		}
	}
}

error_t cpymo_audio_channel_play_file(
	cpymo_audio_channel *c, const char * filename, float volume, bool loop)
{
	const cpymo_backend_audio_info *info = 
		cpymo_backend_audio_get_info();
	if (info == NULL) return CPYMO_ERR_SUCC;

	printf("[Info] Play bgm %s.\n", filename);

	cpymo_audio_channel_reset(c);

	assert(c->enabled == false);

	assert(c->format_context == NULL);
	int result = 
		avformat_open_input(&c->format_context, filename, NULL, NULL);
	if (result != 0) {
		c->format_context = NULL;
		printf("[Error] Can not open %s with error ffmpeg error %s.\n", filename, av_err2str(result));
		return CPYMO_ERR_CAN_NOT_OPEN_FILE;
	}

	result = avformat_find_stream_info(c->format_context, NULL);
	if (result != 0) {
		cpymo_audio_channel_reset(c);
		printf("[Error] Can not get stream info from %s.\n", filename);
		return CPYMO_ERR_BAD_FILE_FORMAT;
	}

	int stream_id = av_find_best_stream(
		c->format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (result != 0) {
		cpymo_audio_channel_reset(c);
		printf("[Error] Can not find best stream from %s.\n", filename);
		return CPYMO_ERR_BAD_FILE_FORMAT;
	}

	AVStream *stream = c->format_context->streams[stream_id];
	assert(c->codec_context == NULL);
	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	c->codec_context = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(c->codec_context, stream->codecpar);
	c->codec_context->pkt_timebase = stream->time_base;
	if (c->codec_context == NULL) {
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_UNKNOWN;
	}

	result = avcodec_open2(c->codec_context, codec, NULL);
	if (result != 0) {
		c->codec_context = NULL;
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_UNSUPPORTED;
	}

	assert(c->swr_context == NULL);
	c->swr_context = swr_alloc_set_opts(
		NULL,
		av_get_default_channel_layout((int)info->channels),
		cpymo_audio_fmt2ffmpeg(info->format),
		(int)info->freq,
		stream->codecpar->channel_layout,
		stream->codecpar->format,
		stream->codecpar->sample_rate,
		0, NULL);
	if (c->swr_context == NULL) {
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_UNKNOWN;
	}

	result = swr_init(c->swr_context);
	if (result < 0) {
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_UNKNOWN;
	}

	if (c->packet == NULL) {
		c->packet = av_packet_alloc();
		if (c->packet == NULL) {
			cpymo_audio_channel_reset(c);
			return CPYMO_ERR_OUT_OF_MEM;
		}
	}

	if (c->frame == NULL) {
		c->frame = av_frame_alloc();
		if (c->frame == NULL) {
			cpymo_audio_channel_reset(c);
			return CPYMO_ERR_OUT_OF_MEM;
		}
	}

	c->enabled = true;
	c->volume = volume;
	c->loop = loop;
	c->converted_frame_current_offset = 0;

	// read first frame
	result = av_read_frame(c->format_context, c->packet);
	if (result < 0) {
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_SUCC;
	}

	if (!cpymo_audio_channel_next_frame(c)) {
		cpymo_audio_channel_reset(c);
		return CPYMO_ERR_SUCC;
	}

	return CPYMO_ERR_SUCC;
}

void cpymo_audio_init(cpymo_audio_system *s)
{
	s->enabled = cpymo_backend_audio_get_info() != NULL;

	for (size_t i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
		cpymo_audio_channel_init(s->channels + i);
		s->channels[i].packet = NULL;
		s->channels[i].frame = NULL;
		s->channels[i].converted_buf = NULL;
		s->channels[i].converted_buf_all_size = 0;
	}
}

void cpymo_audio_free(cpymo_audio_system *s)
{
	if (s->enabled == false) return;

	for (size_t i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
		cpymo_audio_channel_reset(s->channels + i);

		if (s->channels[i].packet) av_packet_free(&s->channels[i].packet);
		if (s->channels[i].frame) av_frame_free(&s->channels[i].frame);
		if (s->channels[i].converted_buf) free(s->channels[i].converted_buf);
	}

	s->enabled = false;
}

void cpymo_audio_copy_samples(void * dst, size_t len, cpymo_audio_system *s)
{
	if (s->enabled == false) return;

	for (size_t i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
		if (s->channels[i].enabled) {
			cpymo_audio_channel_write_samples(dst, len, s->channels + i);
		}
	}
}
