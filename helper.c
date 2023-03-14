/*
 * helper.c
 *
 *  Created on: 21.04.2015
 *      Author: sebastian
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <math.h>

#include <ao/ao.h>

ao_device * open_output(int driver_id, ao_option *dev_opts, int bits, int channels, int sample_rate)
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
		//.matrix = "L,R,BL,BR,C,LFE",
		.matrix = "L,R,C,LFE,BL,BR",
	};

	return (ao_open_live(driver_id, &out_fmt, dev_opts));
}

int test_audio_out(int driver_id, ao_option *dev_opts)
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
	int ch;
	for (ch = 0; ch < 6; ++ch) {
		const size_t buflen = 4800; /* 1/10 of a second */
		int16_t buf[buflen * 6];

		printf("channel %d: %s\n", map[ch].idx, map[ch].name);

		/* prepare sine samples */
		memset(buf, 0, sizeof(buf));
		int i;
		for (i = 0; i < buflen; ++i) {
			buf[i * 6 + map[ch].idx] = INT16_MAX / 10 * cos(2 * (3.14159265358979323846) * map[ch].freq * i / 48000.0);
		}

		/* play for 2 sec, 1 sec pause */
		for (i = 0; i < 30; ++i) {
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
