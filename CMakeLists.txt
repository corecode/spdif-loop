cmake_minimum_required (VERSION 2.8.11)
project (spdif-decoder)

add_executable (spdif-decoder 
    codechandler.c 
    helper.c
    myspdif.c
    myspdifdec.c
    resample.c
    spdif-loop.c
)

SET(FFMPEG /home/sebastian/software/ffmpeg-2.6.1)

target_include_directories (spdif-decoder
    PUBLIC ${FFMPEG}
)

FIND_LIBRARY(libavcodec avcodec ${FFMPEG}/libavcodec)
FIND_LIBRARY(libavformat avformat ${FFMPEG}/libavformat)
FIND_LIBRARY(libavdevice avdevice ${FFMPEG}/libavdevice)
FIND_LIBRARY(libavutil avutil ${FFMPEG}/libavutil)
FIND_LIBRARY(libswresample swresample ${FFMPEG}/libswresample)
FIND_LIBRARY(libavfilter avfilter ${FFMPEG}/libavfilter)
TARGET_LINK_LIBRARIES(spdif-decoder 
    ${libavcodec}
    ${libavformat}
    ${libavdevice}
    ${libavutil}
    ${libswresample}
    ${libavfilter}
    ao
    m
)
