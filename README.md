spdif-decoder
=============

SPDIF to 5.1 software loop decoder.  Use if you want to connect a
digital surround signal (Dolby Digital), such as an Xbox 360 S or TV, via
your PC to your 5.1 (analog) stereo.  Like a digital receiver/decoder,
just in software.

FFMPEG is licenced under the GNU Lesser General Public License version 2.1 and so is 
this piece of software. 

Requirements
------------
- libasound2-dev
- libao-dev
- cmake
- ffmpeg from Source https://www.ffmpeg.org/download.html (tested with 4.3.1)

Build ffmpeg shared library with a minimal set for alsa and AC3 support
-----
    ./configure --enable-shared --disable-static --disable-everything --enable-demuxer=spdif --enable-decoder=ac3 --enable-indev=alsa
    make

Build spdif-decoder
-----
Prepare CMakeLists.txt - set FFMPEG Var with Path to ffmpeg (if not in ../ffmpeg-4.3.1)

    cmake .
    make

Run
---

I run it like this:

    ./spdif-decoder -i hw:CARD=Device -d pulse -o alsa_output.usb-0d8c_USB_Sound_Device-00-Device.analog-surround51

Alsa's `hw:CARD=Device` is my SPDIF input.  You can list your alsa devices with

    aplay -l

I had to use `amixer` to set the capture source to SPIF.  In this case
`Device` is the alsa name of my SPDIF-in card:

	amixer -c Device scontrols                   # show controls
	amixer -c Device get 'PCM Capture Source'    # show options
    amixer -c Device set 'PCM Capture Source' 'IEC958 In'    # set input


Thanks to
-------
Sebastian Morgenstern <Sebastian.Morgenstern@gmail.com>
Simon Schubert <2@0x2c.org>
