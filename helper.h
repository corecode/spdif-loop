/*
 * helper.h
 *
 *  Created on: 21.04.2015
 *      Author: sebastian
 */

#ifndef HELPER_H_
#define HELPER_H_

#include <stdint.h>
#include <ao/ao.h>

extern ao_device * open_output(int driver_id, ao_option *dev_opts, int bits, int channels, int sample_rate);
extern int test_audio_out(int driver_id, ao_option *dev_opts);
#endif /* HELPER_H_ */
