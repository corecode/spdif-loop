spdif-loop
==========

SPDIF to 5.1 software loop decoder.  Use if you want to connect a
digital surround signal (Dolby Digital), such as an Xbox 360 S, via
your PC to your 5.1 (analog) stereo.  Like a digital receiver/decoder,
just in software.


Requirements
------------

- ffmpeg/libav
- libao


Build
-----

make


Run
---

I run it like this:

    ./spdif-loop hw:1 pulse alsa_output.usb-0d8c_USB_Sound_Device-00-Device.analog-surround51

Alsa's hw:1 is my SPDIF input.  You can list your alsa devices with

    aplay -l

I had to use `amixer` to set the capture source to SPIF:

	amixer -c 1 scontrols                   # show controls
	amixer -c 1 get 'PCM Capture Source'    # show options
    amixer -c 1 set 'PCM Capture Source' 'IEC958 In'    # set input


Contact
-------

Simon Schubert <2@0x2c.org>
