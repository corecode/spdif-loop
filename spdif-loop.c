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
		"  spdif-loop <hw:alsa-input-dev>\n");
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

int
main(int argc, char **argv)
{
	if (argc != 2)
		usage();

        char *alsa_dev = argv[1];

	av_register_all();
	avcodec_register_all();
        avdevice_register_all();

	AVInputFormat *alsa_fmt = av_find_input_format("alsa");
	if (!alsa_fmt)
		return (3);

	AVFormatContext *alsa_ctx = NULL;
	if (avformat_open_input(&alsa_ctx, alsa_dev, alsa_fmt, NULL) != 0)
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

	printf("streams: %d\n", spdif_ctx->nb_streams);
	for (int i = 0; i < spdif_ctx->nb_streams; ++i)
		printf("\t%d.\t%d\n", i, spdif_ctx->streams[i]->codec->codec_id);

	return (0);
}
