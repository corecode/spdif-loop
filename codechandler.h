/*
 * codechandler.h
 *
 *  Created on: 25.04.2015
 *      Author: sebastian
 */

#ifndef CODECHANDLER_H_
#define CODECHANDLER_H_
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>

typedef struct s_codechandler{
	AVCodecContext *codecContext;
	AVCodec * codec;
	enum AVCodecID currentCodecID;
	int currentChannelCount;
	uint64_t currentChannelLayout;
	int currentSampleRate;
	SwrContext * swr;
	AVFrame * frame;
} CodecHandler;

void CodecHandler_init(CodecHandler* handler);
void CodecHandler_deinit(CodecHandler* handler);

int CodecHandler_loadCodec(CodecHandler * handler, AVFormatContext * formatcontext);
int CodecHandler_hasCodecChangend(CodecHandler * handler, AVFormatContext * formatcontext);

int CodecHandler_decodeCodec(CodecHandler * h, AVPacket * pkt,
		uint8_t *outbuffer, uint32_t* bufferfilled);
int CodecHandler_closeCodec(CodecHandler * handler);


#endif /* CODECHANDLER_H_ */
