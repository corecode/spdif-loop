/*
 * codechandler.c
 *
 *  Created on: 25.04.2015
 *      Author: sebastian
 */

#include <stdio.h>
#include <errno.h>
#include <err.h>
#include "resample.h"
#include "codechandler.h"

void CodecHandler_init(CodecHandler* h){
	h->codec = NULL;
	h->codecContext = NULL;
	h->currentChannelCount = 0;
	h->currentCodecID = AV_CODEC_ID_NONE;
	h->currentSampleRate = 0;
	h->swr = resample_init();
	h->frame = av_frame_alloc();
}
void CodecHandler_deinit(CodecHandler* h){
	resample_deinit(h->swr);
	av_frame_free(&h->frame);
}

int CodecHandler_loadCodec(CodecHandler * handler, AVFormatContext * formatcontext){
	if (formatcontext->nb_streams == 0){
		printf("could not find a stream\n");
		handler->currentCodecID = AV_CODEC_ID_NONE;
		return -1;
	}

	if(handler->currentCodecID == formatcontext->streams[0]->codec->codec_id){
		//Codec already loaded
		return 0;
	}

	if(handler->codecContext != NULL){
		CodecHandler_closeCodec(handler);
	}
	handler->currentCodecID = AV_CODEC_ID_NONE;

	handler->codec = avcodec_find_decoder(formatcontext->streams[0]->codec->codec_id);
	if (!handler->codec) {
		printf("could not find codec\n");
		return -1;
	}else{
		printf("found codec\n");
	}
	handler->codecContext = avcodec_alloc_context3(handler->codec);
	if (!handler->codecContext)
		errx(1, "cannot allocate codec");
	if (avcodec_open2(handler->codecContext, handler->codec, NULL) != 0)
		errx(1, "cannot open codec");
	handler->currentCodecID = formatcontext->streams[0]->codec->codec_id;
	return 0;
}

int CodecHandler_decodeCodec(CodecHandler * h, AVPacket * pkt,
		uint8_t *outbuffer, uint32_t* bufferfilled){
	int got_frame;
	int processed_len = avcodec_decode_audio4(h->codecContext, h->frame, &got_frame, pkt);
	if (processed_len < 0)
		errx(1, "cannot decode input");

	int ret = 0;

	pkt->data += processed_len;
	pkt->size -= processed_len;
	if(h->currentChannelCount != h->codecContext->channels
			|| h->currentSampleRate != h->codecContext->sample_rate
			|| h->currentChannelLayout != h->codecContext->channel_layout){
		resample_loadFromCodec(h->swr, h->codecContext);
		printf("c: %d, s: %d\n",h->codecContext->channels, h->codecContext->sample_rate);
		ret = 1;
	}

	swr_convert(h->swr, &outbuffer, h->frame->nb_samples, (const uint8_t **)h->frame->data, h->frame->nb_samples);
	*bufferfilled = av_samples_get_buffer_size(NULL,
			   h->codecContext->channels,
			   h->frame->nb_samples,
			   AV_SAMPLE_FMT_S16,
			   1);

	h->currentChannelCount = h->codecContext->channels;
	h->currentSampleRate = h->codecContext->sample_rate;
	h->currentChannelLayout = h->codecContext->channel_layout;
	return ret;
}


int CodecHandler_closeCodec(CodecHandler * handler){
	if(handler->codecContext != NULL){
		avcodec_close(handler->codecContext);
		avcodec_free_context(&handler->codecContext);
	}
	handler->codec = NULL;
	handler->codecContext = NULL;
	return 0;
}
