/*
 * IEC 61937 demuxer
 * Copyright (c) 2010 Anssi Hannula <anssi.hannula at iki.fi>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * IEC 61937 demuxer, used for compressed data in S/PDIF
 * @author Anssi Hannula
 */

#include <libavformat/avformat.h>
#include <libavformat/spdif.h>
#include "myspdif.h"
#include <libavcodec/ac3.h>
#include <libavcodec/aacadtsdec.h>

static int spdif_get_offset_and_codec(AVFormatContext *s,
                                      enum IEC61937DataType data_type,
                                      const uint8_t *buf, int *offset,
                                      enum AVCodecID *codec)
{
    AACADTSHeaderInfo aac_hdr;
    GetBitContext gbc;

    switch (data_type & 0xff) {
    case IEC61937_AC3:
        *offset = AC3_FRAME_SIZE << 2;
        *codec = AV_CODEC_ID_AC3;
        break;
    case IEC61937_MPEG1_LAYER1:
        *offset = spdif_mpeg_pkt_offset[1][0];
        *codec = AV_CODEC_ID_MP1;
        break;
    case IEC61937_MPEG1_LAYER23:
        *offset = spdif_mpeg_pkt_offset[1][0];
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_MPEG2_EXT:
        *offset = 4608;
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_MPEG2_AAC:
        init_get_bits(&gbc, buf, AAC_ADTS_HEADER_SIZE * 8);
        if (avpriv_aac_parse_header(&gbc, &aac_hdr) < 0) {
            if (s) /* be silent during a probe */
                av_log(s, AV_LOG_ERROR, "Invalid AAC packet in IEC 61937\n");
            return AVERROR_INVALIDDATA;
        }
        *offset = aac_hdr.samples << 2;
        *codec = AV_CODEC_ID_AAC;
        break;
    case IEC61937_MPEG2_LAYER1_LSF:
        *offset = spdif_mpeg_pkt_offset[0][0];
        *codec = AV_CODEC_ID_MP1;
        break;
    case IEC61937_MPEG2_LAYER2_LSF:
        *offset = spdif_mpeg_pkt_offset[0][1];
        *codec = AV_CODEC_ID_MP2;
        break;
    case IEC61937_MPEG2_LAYER3_LSF:
        *offset = spdif_mpeg_pkt_offset[0][2];
        *codec = AV_CODEC_ID_MP3;
        break;
    case IEC61937_DTS1:
        *offset = 2048;
        *codec = AV_CODEC_ID_DTS;
        break;
    case IEC61937_DTS2:
        *offset = 4096;
        *codec = AV_CODEC_ID_DTS;
        break;
    case IEC61937_DTS3:
        *offset = 8192;
        *codec = AV_CODEC_ID_DTS;
        break;
    default:
        if (s) { /* be silent during a probe */

            //avpriv_request_sample(s, "Data type 0x%04x in IEC 61937",
            //                      data_type);
        }
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

int my_spdif_read_packet(AVFormatContext *s, AVPacket *pkt,
		uint8_t * garbagebuffer, int garbagebuffersize, int * garbagebufferfilled)
{
    AVIOContext *pb = s->pb;
    enum IEC61937DataType data_type;
    enum AVCodecID codec_id;
    uint32_t state = 0;
    int pkt_size_bits, offset, ret;
    *garbagebufferfilled = 0;
    while (state != (AV_BSWAP16C(SYNCWORD1) << 16 | AV_BSWAP16C(SYNCWORD2))) {
    	if(*garbagebufferfilled < garbagebuffersize){
    		*garbagebuffer = avio_r8(pb);
    		(*garbagebufferfilled)++;

    		state = (state << 8) | *garbagebuffer;
    		garbagebuffer++;

    		if (avio_feof(pb)){
                return AVERROR_EOF;
            }
    	}else{
    		return AVERROR_STREAM_NOT_FOUND;
    	}
    }
    *garbagebufferfilled -= 4;
    data_type = avio_rl16(pb);
    pkt_size_bits = avio_rl16(pb);

    if (pkt_size_bits % 16)
        avpriv_request_sample(s, "Packet not ending at a 16-bit boundary");

    ret = av_new_packet(pkt, FFALIGN(pkt_size_bits, 16) >> 3);
    if (ret)
        return ret;

    pkt->pos = avio_tell(pb) - BURST_HEADER_SIZE;

    if (avio_read(pb, pkt->data, pkt->size) < pkt->size) {
        av_free_packet(pkt);
        return AVERROR_EOF;
    }
    my_spdif_bswap_buf16((uint16_t *)pkt->data, (uint16_t *)pkt->data, pkt->size >> 1);

    ret = spdif_get_offset_and_codec(s, data_type, pkt->data,
                                     &offset, &codec_id);
    if (ret) {
        av_free_packet(pkt);
        return ret;
    }

    /* skip over the padding to the beginning of the next frame */
    avio_skip(pb, offset - pkt->size - BURST_HEADER_SIZE);

    if (!s->nb_streams) {
        /* first packet, create a stream */
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            av_free_packet(pkt);
            return AVERROR(ENOMEM);
        }
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = codec_id;
    } else if (codec_id != s->streams[0]->codec->codec_id) {
        avpriv_report_missing_feature(s, "Codec change in IEC 61937");
        return AVERROR_PATCHWELCOME;
    }

    if (!s->bit_rate && s->streams[0]->codec->sample_rate)
        /* stream bitrate matches 16-bit stereo PCM bitrate for currently
           supported codecs */
        s->bit_rate = 2 * 16 * s->streams[0]->codec->sample_rate;

    return 0;
}
