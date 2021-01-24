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
#include <libavformat/avio.h>
#include <libavformat/spdif.h>

#include "resample.h"
#include "helper.h"
#include "myspdif.h"
#include "codechandler.h"

//#define DEBUG
//#define IO_BUFFER_SIZE	SPDIF_MAX_OFFSET// ((8+1792+4344)*1)
#define IO_BUFFER_SIZE	((8+1792+4344)*1)


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
			int i;
			for (i = 0; i < read_size; ++i) {
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

int
main(int argc, char **argv)
{
	int opt_test = 0;
	char *alsa_dev_name = NULL;
	char *out_driver_name = NULL;
	char *out_dev_name = NULL;
	int opt;
	for (opt = 0; (opt = getopt(argc, argv, "d:hi:o:tv")) != -1;) {
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
		// NOTREACHED
	}


	AVInputFormat *alsa_fmt = av_find_input_format("alsa");
	if (!alsa_fmt)
		errx(1, "cannot find alsa input driver");

	AVInputFormat *spdif_fmt = av_find_input_format("spdif");
	if (!spdif_fmt)
		errx(1, "cannot find S/PDIF demux driver");

	const int alsa_buf_size = IO_BUFFER_SIZE;
	unsigned char *alsa_buf = av_malloc(alsa_buf_size);
	if (!alsa_buf)
		errx(1, "cannot allocate input buffer");

	AVFormatContext *spdif_ctx = NULL;
	AVFormatContext *alsa_ctx = NULL;
	ao_device *out_dev = NULL;

    char *resamples = malloc(1*1024*1024);

	if (0) {
retry:
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
		errx(1, "cannot allocate S/PDIF context");

	if (avformat_open_input(&alsa_ctx, alsa_dev_name, alsa_fmt, NULL) != 0)
		errx(1, "cannot open alsa input");

	struct alsa_read_state read_state = {
		.ctx = alsa_ctx,
	};

	av_init_packet(&read_state.pkt);
	AVIOContext * avio_ctx = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
    if (!avio_ctx) {
    	errx(1, "cannot open avio_alloc_context");
    }

	spdif_ctx->pb = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
	if (!spdif_ctx->pb)
		errx(1, "cannot set up alsa reader");

	if (avformat_open_input(&spdif_ctx, "internal", spdif_fmt, NULL) != 0)
		errx(1, "cannot open S/PDIF input");

	av_dump_format(alsa_ctx, 0, alsa_dev_name, 0);

	AVPacket pkt = {.size = 0, .data = NULL};
	av_init_packet(&pkt);

    uint32_t howmuch = 0;

    CodecHandler codecHanlder;
    CodecHandler_init(&codecHanlder);
	printf("start loop\n");
	while (1) {
		int garbagefilled = 0;
		int r = my_spdif_read_packet(spdif_ctx, &pkt, (uint8_t*)resamples, IO_BUFFER_SIZE, &garbagefilled);

		if(r == 0){
			if(CodecHandler_loadCodec(&codecHanlder, spdif_ctx)!=0){
				printf("Could not load codec %s.\n", avcodec_get_name(codecHanlder.currentCodecID));
				goto retry;
			}

			if(CodecHandler_decodeCodec(&codecHanlder,&pkt,(uint8_t*)resamples, &howmuch) == 1){
				//channel count has changed
				//close out_dev
				if (out_dev) {
					ao_close(out_dev);
					out_dev = NULL;
				}
				printf("Detected S/PDIF codec %s\n", avcodec_get_name(codecHanlder.currentCodecID));
			}
			if(pkt.size != 0){
				printf("still some bytes left %d\n",pkt.size);
			}
		} else {
			codecHanlder.currentCodecID = AV_CODEC_ID_NONE;
			if(codecHanlder.currentChannelCount != 2 ||
					codecHanlder.currentSampleRate != 48000){
				printf("Detected S/PDIF uncompressed audio\n");

				if (out_dev) {
					ao_close(out_dev);
					out_dev = NULL;
				}
			}
			codecHanlder.currentChannelCount = 2;
			codecHanlder.currentSampleRate = 48000;
			codecHanlder.currentChannelLayout = 0;
			howmuch = garbagefilled;
		}

		if (!out_dev) {
			out_dev = open_output(out_driver_id,
					      out_dev_opts,
					      av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 8,
					      codecHanlder.currentChannelCount,
					      codecHanlder.currentSampleRate);
			if (!out_dev)
				errx(1, "cannot open audio output");
		}
		//found wav
		if(!ao_play(out_dev, resamples, howmuch)){
			printf("Could not play audio to output device...");
			goto retry;
		}
	}
	CodecHandler_deinit(&codecHanlder);
	return (0);
}
