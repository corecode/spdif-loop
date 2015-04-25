/*
 * resample.h
 *
 *  Created on: 20.04.2015
 *      Author: sebastian
 */

#ifndef RESAMPLE_H_
#define RESAMPLE_H_

#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <stdint.h>

SwrContext* resample_init();
void resample_deinit(SwrContext* swr);
void resample_loadFromCodec(SwrContext *swr, AVCodecContext* audioCodec);
void resample_do(SwrContext* swr, AVFrame *audioFrame, uint8_t* outputBuffer);

#endif /* RESAMPLE_H_ */
