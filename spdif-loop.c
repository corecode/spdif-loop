#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <ao/ao.h>

#define IO_BUFFER_SIZE	32768

struct spdif_read_state {
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
spdif_reader(void *data, uint8_t *buf, int buf_size)
{
	struct spdif_read_state *st = data;
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

        char *in_dev = argv[1];

	av_register_all();
	avcodec_register_all();
        avdevice_register_all();

	AVInputFormat *in_fmt = av_find_input_format("alsa");
	if (!in_fmt)
		return (3);

	AVFormatContext *in_ctx = NULL;
	if (avformat_open_input(&in_ctx, in_dev, in_fmt, NULL) != 0)
		return (2);

	AVFormatContext *spdif_ctx = avformat_alloc_context();
	if (!spdif_ctx)
		return (4);

	const int spdif_buf_size = IO_BUFFER_SIZE;
	unsigned char *spdif_buf = av_malloc(spdif_buf_size);
	if (!spdif_buf)
		return (5);

	struct spdif_read_state read_state = {
		.ctx = in_ctx,
	};
	av_init_packet(&read_state.pkt);

	spdif_ctx->pb = avio_alloc_context(spdif_buf, spdif_buf_size, 0, &read_state, spdif_reader, NULL, NULL);
	if (!spdif_ctx->pb)
		return (6);

	AVInputFormat *spdif_fmt = av_find_input_format("spdif");
	if (!spdif_fmt)
		return (8);
	if (avformat_open_input(&spdif_ctx, "internal", spdif_fmt, NULL) != 0)
		return (7);

	return (0);
}
