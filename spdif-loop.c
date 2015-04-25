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
	AVIOContext * avio_ctx = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
    if (!avio_ctx) {
    	errx(1, "cannot open avio_alloc_context");
    }


	spdif_ctx->pb = avio_alloc_context(alsa_buf, alsa_buf_size, 0, &read_state, alsa_reader, NULL, NULL);
	if (!spdif_ctx->pb)
		errx(1, "cannot set up alsa reader");

	if (avformat_open_input(&spdif_ctx, "internal", spdif_fmt, NULL) != 0)
		errx(1, "cannot open spdif input");

	av_dump_format(alsa_ctx, 0, alsa_dev_name, 0);

#ifdef HAVE_AVCODEC_GET_NAME
	printf("detected spdif codec %s\n", avcodec_get_name(spdif_codec_id));
#endif

	AVPacket pkt = {.size = 0, .data = NULL};
	av_init_packet(&pkt);

    char *resamples= malloc(1*1024*1024);

    uint32_t howmuch = 0;

    CodecHandler codecHanlder;
    CodecHandler_init(&codecHanlder);
	printf("start loop\n");
	while (1) {
		int garbagefilled = 0;
		int r = my_spdif_read_packet(spdif_ctx, &pkt, (uint8_t*)resamples, IO_BUFFER_SIZE, &garbagefilled);

		if(r == 0){
			//Play rest of garbage before decode newly found codec
			if(		garbagefilled > 0
					&& codecHanlder.currentCodecID == AV_CODEC_ID_NONE
					&& codecHanlder.currentChannelCount == 2
					&& codecHanlder.currentSampleRate == 48000
					&& out_dev != NULL){
				if(!ao_play(out_dev, resamples, garbagefilled)){
					goto retry;
				}
			}

			if(CodecHandler_loadCodec(&codecHanlder, spdif_ctx)!=0){
				goto retry;
			}

			if(CodecHandler_decodeCodec(&codecHanlder,&pkt,(uint8_t*)resamples, &howmuch) == 1){
				//channel count has changed
				//close out_dev
				if (out_dev) {
					ao_close(out_dev);
					out_dev = NULL;
				}
			}
			if(pkt.size != 0){
				printf("still some bytes left %d\n",pkt.size);
			}

		}else{
			codecHanlder.currentCodecID = AV_CODEC_ID_NONE;
			if(codecHanlder.currentChannelCount != 2 ||
					codecHanlder.currentSampleRate != 48000){
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
#if 0
		if(r>0){
			//avio_tell()
			avio_seek(avio_ctx, avio_ctx->buffer_size, SEEK_CUR);
			//found spdif
			spdif_ctx->pb = avio_ctx;
			int readpak = spdif_fmt->read_packet(spdif_ctx, &pkt);
			printf("readpak %d: %X\n", readpak, spdif_ctx->streams[0]->codec->codec_id);
			//av_dump_format(spdif_ctx, 0, "test1", 0);

			if(spdif_codec_ctx == NULL){
				if (spdif_ctx->nb_streams == 0){
					printf("could not find a stream\n");
					goto retry;
				}

				spdif_codec = avcodec_find_decoder(spdif_ctx->streams[0]->codec->codec_id);
				if (!spdif_codec) {
					printf("could not find codec\n");
					goto retry;
				}else{
					printf("found codec\n");
				}
				spdif_codec_ctx = avcodec_alloc_context3(spdif_codec);
				if (!spdif_codec_ctx)
					errx(1, "cannot allocate codec");
				if (avcodec_open2(spdif_codec_ctx, spdif_codec, NULL) != 0)
					errx(1, "cannot open codec");


				//av_dump_format(spdif_ctx, 0, "test1", 0);
				/*
				printf("c: %d, samples %d, format %d\n",
						spdif_codec_ctx->channels, frame->nb_samples,
						spdif_codec_ctx->sample_fmt);
						*/

			}
			int got_frame;
			int processed_len = avcodec_decode_audio4(spdif_codec_ctx, frame, &got_frame, &pkt);
			if (processed_len < 0)
				errx(1, "cannot decode input");

			pkt.data += processed_len;
			pkt.size -= processed_len;
			if(swr == NULL){
				swr = resample_init(spdif_codec_ctx);
				printf("c: %d, s: %d\n",spdif_codec_ctx->channels, spdif_codec_ctx->sample_rate);
			}

			swr_convert(swr, &resamples, frame->nb_samples, frame->data, frame->nb_samples);

			channels = spdif_codec_ctx->channels;
			sample_rate = spdif_codec_ctx->sample_rate;
			whattoplay = (char*)resamples;
			howmuch = av_samples_get_buffer_size(NULL,
					   spdif_codec_ctx->channels,
					   frame->nb_samples,
					   AV_SAMPLE_FMT_S16,
					   1);
		}else{
			//found wav
			channels = 2;
			sample_rate = 48000;
			whattoplay = (char*)pd.buf;
			howmuch = pd.buf_size;
		}
#endif

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
			goto retry;
		}
#if 0
		if (pkt.size == 0) {
			av_free_packet(&pkt);
			int e = av_read_frame(alsa_ctx, &pkt);
			if (e != 0) {
				printf("reading frame failed: %d\n", e);
				goto retry;

			}
			/*
			if (spdif_ctx->nb_streams == 0){
				if(oldCodec != alsa_ctx->streams[0]->codec->codec_id){
					oldCodec = alsa_ctx->streams[0]->codec->codec_id;
					av_dump_format(alsa_ctx, 0, "input", 0);
				}
			}else{
				//printf("no Stream\n");
			}
			*/
			/*
			if(pkt.size >= 4){
				if(pkt.data[0] !=0
					&& pkt.data[1] !=0
					&& pkt.data[2] !=0
					&& pkt.data[3] !=0){
						if(    pkt.data[0] == 0xF8 && pkt.data[1] == 0x72
							|| pkt.data[1] == 0xF8 && pkt.data[0] == 0x72){
							printf("size: %d, header %02X %02X %02X %02X\n", pkt.size,
									pkt.data[0], pkt.data[1],pkt.data[2],pkt.data[3]);
						}
				}
				uint16_t * ptr = (uint16_t*)pkt.data;
				int i=0;
				while (i<pkt.size/2){
					if(ptr[i] == 0xF872 && ptr[i+1] == 0x4E1F){
						printf("found spdif at %d/%d %04X, %04X, %04X\n",
								i,framecount,
								ptr[i], ptr[i+2],ptr[i+3]);
					}
					i++;
				}
			}else{
				printf("size: %d\n", pkt.size);
			}
			*/
			framecount++;

			//pkt = pkt1;
		}
		av_frame_unref(frame);
		int got_frame = 1;

#if 0
		if(spdif_codec_ctx == NULL){
			if (spdif_ctx->nb_streams == 0){
				printf("could not find a stream\n");
				goto retry;
			}

			spdif_codec = avcodec_find_decoder(spdif_ctx->streams[0]->codec->codec_id);
			if (!spdif_codec) {
				printf("could not find codec\n");
				goto retry;
			}else{
				printf("found codec\n");
			}
			spdif_codec_ctx = avcodec_alloc_context3(spdif_codec);
			if (!spdif_codec_ctx)
				errx(1, "cannot allocate codec");
			if (avcodec_open2(spdif_codec_ctx, spdif_codec, NULL) != 0)
				errx(1, "cannot open codec");

			av_dump_format(spdif_ctx, 0, "test1", 0);
			printf("c: %d, samples %d, format %d\n",
					spdif_codec_ctx->channels, frame->nb_samples,
					spdif_codec_ctx->sample_fmt);

		}

		int processed_len = avcodec_decode_audio4(spdif_codec_ctx, frame, &got_frame, &pkt);
		if (processed_len < 0)
			errx(1, "cannot decode input");
			*/
		pkt.data += processed_len;
		pkt.size -= processed_len;
#else
		pkt.size = 0;
#endif
		if (!got_frame)
			continue;


		if (!out_dev) {
			/**
			 * We open the output only here, because we need a full frame decoded
			 * before we can know the output format.
			 */

			out_dev = open_output(out_driver_id,
					      out_dev_opts,
					      av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 8,
					      spdif_codec_ctx->channels,
					      spdif_codec_ctx->sample_rate);
			if (!out_dev)
				errx(1, "cannot open audio output");

			swr = resample_init(spdif_codec_ctx);
			//av_dump_format(spdif_ctx, 0, "test1", 0);

		}

		int framesize = av_samples_get_buffer_size(NULL,
							   spdif_codec_ctx->channels,
							   frame->nb_samples,
							   AV_SAMPLE_FMT_S16,
							   1);
/*
		printf("fs: %d, c: %d, samples %d, format %d\n",
				framesize, spdif_codec_ctx->channels, frame->nb_samples,
				spdif_codec_ctx->sample_fmt);
*/

#ifdef DEBUG
		int max = 0;
		int16_t *fb = (void *)frame->data[0];
		for (int i = 0; i < frame->nb_samples * spdif_codec_ctx->channels; ++i) {
			int v = fb[i];
			if (v < 0)
				v = -v;
			if (v > max)
				max = v;
		}

		/* Debug latency */
		for (int i = 0; i < max / 100; ++i)
			putchar('*');
		//printf("\n");
		printf("%d\n", max);
#endif

		if(frame->data[0] != NULL){
			swr_convert(swr, &samples, frame->nb_samples, frame->data, frame->nb_samples);
			if(!ao_play(out_dev, (char*)samples, framesize)){
				goto retry;
			}
		}else{
			printf("frame data == NULL\n");
		}
#endif
	}
	CodecHandler_deinit(&codecHanlder);
	return (0);
}
