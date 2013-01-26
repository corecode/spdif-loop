#define _GNU_SOURCE

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <getopt.h>

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

static int debug_data;

static void
usage(void)
{
	fprintf(stderr,
		"usage:\n"
		"  spdif-loop [-t | -i <hw:alsa-input-dev>] -d <alsa|pulse> -o <output-dev>\n");
	exit(1);
}

static int
alsa_reader(void *data, uint8_t *buf, int buf_size)
{
	struct alsa_read_state *st = data;
	int read_size = 0;

	while (buf_size > 0) {
		if (st->pkt.size <= 0) {
			int ret = av_read_frame(st->ctx, &st->pkt);
			st->offset = 0;

			if (ret != 0)
				return (ret);
		}

		int pkt_left = st->pkt.size - st->offset;
		int datsize = buf_size < pkt_left ? buf_size : pkt_left;

		memcpy(buf, st->pkt.data + st->offset, datsize);
		st->offset += datsize;
		read_size += datsize;
		buf += datsize;
		buf_size -= datsize;

		if (debug_data) {
			static int had_zeros = 0;
			for (int i = 0; i < read_size; ++i) {
				const char zeros[16] = {0};
				if (i % 16 == 0 && read_size - i >= 16 &&
				    memcmp((char *)buf + i, zeros, 16) == 0) {
					i += 15;
					if (had_zeros && had_zeros % 10000 == 0)
						printf("  (%d)\n", had_zeros * 16);
					if (!had_zeros)
						printf("...\n");
					had_zeros++;
					continue;
				}
				if (had_zeros)
					printf("  (%d)\n", had_zeros * 16);
				had_zeros = 0;
				printf("%02x%s", ((unsigned char *)buf)[i],
				       (i + 1) % 16 == 0 ? "\n" : " ");
			}
		}

		if (st->offset >= st->pkt.size)
			av_free_packet(&st->pkt);
	}

	return (read_size);
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
open_output(int driver_id, ao_option *dev_opts, int bits, int channels, int sample_rate)
{
	printf("%d bit, %d channels, %dHz\n",
	       bits,
	       channels,
	       sample_rate);

	ao_sample_format out_fmt = {
		.bits = bits,
		.channels = channels,
		.rate = sample_rate,
		.byte_format = AO_FMT_NATIVE,
		.matrix = "L,R,C,LFE,BL,BR",
	};

	return (ao_open_live(driver_id, &out_fmt, dev_opts));
}

static int
test_audio_out(int driver_id, ao_option *dev_opts)
{
	struct chan_map {
		const char *name;
		int freq;
		int idx;
	} map[] = {
		/* This needs to match the order in open_output(). */
		{ "left",	500, 0 },
		{ "center",	500, 2 },
		{ "right",	500, 1 },
		{ "rear right", 500, 5 },
		{ "rear left",	500, 4 },
		{ "sub",	 50, 3 }
	};

	ao_device *odev = open_output(driver_id, dev_opts, 16, 6, 48000);
	if (!odev)
		errx(1, "cannot open audio output");

	for (int ch = 0; ch < 6; ++ch) {
		const size_t buflen = 4800; /* 1/10 of a second */
		int16_t buf[buflen * 6];

		printf("channel %d: %s\n", map[ch].idx, map[ch].name);

		/* prepare sine samples */
		memset(buf, 0, sizeof(buf));
		for (int i = 0; i < buflen; ++i) {
			buf[i * 6 + map[ch].idx] = INT16_MAX / 10 * cos(2 * M_PI * map[ch].freq * i / 48000.0);
		}

		/* play for 2 sec, 1 sec pause */
		for (int i = 0; i < 30; ++i) {
			if (i == 20) {
				/* now pause */
				memset(buf, 0, sizeof(buf));
			}
			if (!ao_play(odev, (char *)buf, sizeof(buf)))
				errx(1, "cannot play test audio");
		}
	}

	return (0);
}

int
main(int argc, char **argv)
{
	int opt_test = 0;
	char *alsa_dev_name = NULL;
	char *out_driver_name = NULL;
	char *out_dev_name = NULL;

	for (int opt = 0; (opt = getopt(argc, argv, "d:hi:o:tv")) != -1;) {
		switch (opt) {
		case 'd':
			out_driver_name = optarg;
			break;
		case 'i':
			alsa_dev_name = optarg;
			break;
		case 'o':
			out_dev_name = optarg;
			break;
		case 't':
			opt_test = 1;
			break;
		case 'v':
			debug_data = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (!(opt_test ^ !!alsa_dev_name)) {
		fprintf(stderr, "please specify either input device or testing mode\n\n");
		usage();
	}

	av_register_all();
	avcodec_register_all();
	avdevice_register_all();
	ao_initialize();

	ao_option *out_dev_opts = NULL;
	if (out_dev_name) {
		if (!ao_append_option(&out_dev_opts, "dev", out_dev_name))
			errx(1, "cannot set output device `%s'", out_dev_name);
	}

	int out_driver_id = ao_default_driver_id();
	if (out_driver_name)
		out_driver_id = ao_driver_id(out_driver_name);
	if (out_driver_id < 0)
		errx(1, "invalid output driver `%s'",
		     out_driver_name ? out_driver_name : "default");

	if (opt_test) {
		exit(test_audio_out(out_driver_id, out_dev_opts));
		/* NOTREACHED */
	}

	AVInputFormat *alsa_fmt = av_find_input_format("alsa");
	if (!alsa_fmt)
		errx(1, "cannot find alsa input driver");

	AVInputFormat *spdif_fmt = av_find_input_format("spdif");
	if (!spdif_fmt)
		errx(1, "cannot find spdif demux driver");

	const int alsa_buf_size = IO_BUFFER_SIZE;
	unsigned char *alsa_buf = av_malloc(alsa_buf_size);
	if (!alsa_buf)
		errx(1, "cannot allocate input buffer");

	AVFormatContext *spdif_ctx = NULL;
	AVFormatContext *alsa_ctx = NULL;
	ao_device *out_dev = NULL;

	if (0) {
retry:
		printf("failure...\n");
		if (spdif_ctx)
			avformat_close_input(&spdif_ctx);
		if (alsa_ctx)
			avformat_close_input(&alsa_ctx);
		if (out_dev) {
			ao_close(out_dev);
			out_dev = NULL;
		}
		sleep(1);
		printf("retrying.\n");
	}


	spdif_ctx = avformat_alloc_context();
	if (!spdif_ctx)
		errx(1, "cannot allocate spdif context");

	if (avformat_open_input(&alsa_ctx, alsa_dev_name, alsa_fmt, NULL) != 0)
		errx(1, "cannot open alsa input");

	struct alsa_read_state read_state = {
		.ctx = alsa_ctx,
	};
	av_init_packet(&read_state.pkt);

	spdif_ctx->pb = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
	if (!spdif_ctx->pb)
		errx(1, "cannot set up alsa reader");

	if (avformat_open_input(&spdif_ctx, "internal", spdif_fmt, NULL) != 0)
		errx(1, "cannot open spdif input");

	enum CodecID spdif_codec_id = probe_codec(spdif_ctx);

#if HAVE_AVCODEC_GET_NAME
	printf("detected spdif codec %s\n", avcodec_get_name(spdif_codec_id));
#endif

	AVCodec *spdif_codec = avcodec_find_decoder(spdif_codec_id);
	if (!spdif_codec) {
		printf("could not find codec\n");
		goto retry;
	}

	AVCodecContext *spdif_codec_ctx = avcodec_alloc_context3(spdif_codec);
	if (!spdif_codec_ctx)
		errx(1, "cannot allocate codec");
	spdif_codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
	if (avcodec_open2(spdif_codec_ctx, spdif_codec, NULL) != 0)
		errx(1, "cannot open codec");

	AVPacket pkt, pkt1 = {.size = 0, .data = NULL};
	av_init_packet(&pkt1);
	pkt = pkt1;

	AVFrame frame;

	for (;;) {
		if (pkt.size == 0) {
			av_free_packet(&pkt1);
			int e = av_read_frame(spdif_ctx, &pkt1);
			if (e != 0) {
				printf("reading frame failed: %d\n", e);
				goto retry;
			}
			pkt = pkt1;
		}

		avcodec_get_frame_defaults(&frame);
		int got_frame = 0;
		int processed_len = avcodec_decode_audio4(spdif_codec_ctx, &frame, &got_frame, &pkt);
		if (processed_len < 0)
			errx(1, "cannot decode input");
		pkt.data += processed_len;
		pkt.size -= processed_len;

		if (!got_frame)
			continue;


		if (!out_dev) {
			/**
			 * We open the output only here, because we need a full frame decoded
			 * before we can know the output format.
			 */
			out_dev = open_output(out_driver_id,
					      out_dev_opts,
					      av_get_bytes_per_sample(spdif_codec_ctx->sample_fmt) * 8,
					      spdif_codec_ctx->channels,
					      spdif_codec_ctx->sample_rate);
			if (!out_dev)
				errx(1, "cannot open audio output");
		}

		int framesize = av_samples_get_buffer_size(NULL,
							   spdif_codec_ctx->channels,
							   frame.nb_samples,
							   spdif_codec_ctx->sample_fmt,
							   1);

#if DEBUG
		int max = 0;
		int16_t *fb = (void *)frame.data[0];
		for (int i = 0; i < frame.nb_samples * spdif_codec_ctx->channels; ++i) {
			int v = fb[i];
			if (v < 0)
				v = -v;
			if (v > max)
				max = v;
		}

		/* Debug latency */
		for (int i = 0; i < max / 100; ++i)
			putchar('*');
		printf("\n");
		//printf("%d\n", max);
#endif

		if (!ao_play(out_dev, (void *)frame.data[0], framesize)) {
			goto retry;
		}
	}

	return (0);
}
