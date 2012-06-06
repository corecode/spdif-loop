#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <ao/ao.h>

#define IO_BUFFER_SIZE	32768

struct alsa_read_state {
	AVFormatContext *ctx;
	AVPacket	 pkt;
	int		 offset;
};

static void
usage(void)
{
	fprintf(stderr,
		"usage:\n"
		"  spdif-loop <hw:alsa-input-dev> <alsa|pulse> <output-dev>\n");
	exit(1);
}

static int
alsa_reader(void *data, uint8_t *buf, int buf_size)
{
	struct alsa_read_state *st = data;
	int ret;

	if (st->pkt.size <= 0) {
		ret = av_read_frame(st->ctx, &st->pkt);
		st->offset = 0;

		if (ret != 0)
			return (ret);
	}

	int pkt_left = st->pkt.size - st->offset;
	int datsize = buf_size < pkt_left ? buf_size : pkt_left;

	memcpy(buf, st->pkt.data + st->offset, datsize);
	ret = datsize;

	if (datsize == st->pkt.size)
		av_free_packet(&st->pkt);

	return (ret);
}

static enum CodecID
probe_codec(AVFormatContext *s)
{
	AVPacket pkt;

	av_init_packet(&pkt);
	if (av_read_frame(s, &pkt) != 0)
		return (CODEC_ID_NONE);

	av_free_packet(&pkt);

	if (s->nb_streams == 0)
		return (CODEC_ID_NONE);
	return (s->streams[0]->codec->codec_id);
}

static ao_device *
open_output(int driver_id, ao_option *dev_opts, AVCodecContext *s)
{
        printf("%d bit, %d channels, %dHz\n",
               av_get_bytes_per_sample(s->sample_fmt) * 8,
               s->channels,
               s->sample_rate);

        ao_sample_format out_fmt = {
                .bits = av_get_bytes_per_sample(s->sample_fmt) * 8,
                .channels = s->channels,
                .rate = s->sample_rate,
                .byte_format = AO_FMT_NATIVE,
        };

	return (ao_open_live(driver_id, &out_fmt, dev_opts));
}

int
main(int argc, char **argv)
{
	if (argc != 4)
		usage();

	char *alsa_dev_name = argv[1];
	char *out_driver_name = argv[2];
	char *out_dev_name = argv[3];

	av_register_all();
	avcodec_register_all();
	avdevice_register_all();
	ao_initialize();

	int out_driver_id = ao_driver_id(out_driver_name);
	if (out_driver_id < 0)
		return (17);

	ao_option *out_dev_opts = NULL;
	if (!ao_append_option(&out_dev_opts, "dev", out_dev_name))
		return (18);

	AVInputFormat *alsa_fmt = av_find_input_format("alsa");
	if (!alsa_fmt)
		return (3);

	AVFormatContext *alsa_ctx = NULL;
	if (avformat_open_input(&alsa_ctx, alsa_dev_name, alsa_fmt, NULL) != 0)
		return (2);

	AVFormatContext *spdif_ctx = avformat_alloc_context();
	if (!spdif_ctx)
		return (4);

	const int alsa_buf_size = IO_BUFFER_SIZE;
	unsigned char *alsa_buf = av_malloc(alsa_buf_size);
	if (!alsa_buf)
		return (5);

	struct alsa_read_state read_state = {
		.ctx = alsa_ctx,
	};
	av_init_packet(&read_state.pkt);

	spdif_ctx->pb = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
	if (!spdif_ctx->pb)
		return (6);

	AVInputFormat *spdif_fmt = av_find_input_format("spdif");
	if (!spdif_fmt)
		return (8);
	if (avformat_open_input(&spdif_ctx, "internal", spdif_fmt, NULL) != 0)
		return (7);

	enum CodecID spdif_codec_id = probe_codec(spdif_ctx);

	printf("detected spdif codec %s\n", avcodec_get_name(spdif_codec_id));

	AVCodec *spdif_codec = avcodec_find_decoder(spdif_codec_id);
	if (!spdif_codec)
		return (9);
	AVCodecContext *spdif_codec_ctx = avcodec_alloc_context3(spdif_codec);
	if (!spdif_codec_ctx)
		return (10);
	spdif_codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
	if (avcodec_open2(spdif_codec_ctx, spdif_codec, NULL) != 0)
		return (11);

	AVPacket pkt, pkt1 = {.size = 0, .data = NULL};
	av_init_packet(&pkt1);
	pkt = pkt1;

	AVFrame frame;

	ao_device *out_dev = NULL;

	for (;;) {
		if (pkt.size == 0) {
			av_free_packet(&pkt1);
			if (av_read_frame(spdif_ctx, &pkt1) != 0)
				return (13);
			pkt = pkt1;
		}

		avcodec_get_frame_defaults(&frame);
		int got_frame = 0;
		int processed_len = avcodec_decode_audio4(spdif_codec_ctx, &frame, &got_frame, &pkt);
		if (processed_len < 0)
			return (14);
		pkt.data += processed_len;
		pkt.size -= processed_len;

		if (!got_frame)
			continue;

		
		if (!out_dev) {
			/**
			 * We open the output only here, because we need a full frame decoded
			 * before we can know the output format.
			 */
			out_dev = open_output(out_driver_id, out_dev_opts, spdif_codec_ctx);
			if (!out_dev)
				return (15);
		}

		int framesize = av_samples_get_buffer_size(NULL,
							   spdif_codec_ctx->channels,
							   frame.nb_samples,
							   spdif_codec_ctx->sample_fmt,
							   1);
		if (!ao_play(out_dev, (void *)frame.data[0], framesize))
			return (16);
	}
	
	return (0);
}
