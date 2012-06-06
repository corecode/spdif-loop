PROG=	spdif-loop

CFLAGS+=	-Wall -std=c99 -g
LDFLAGS+=	-lavcodec -lavformat -lavdevice -lavutil -lao

all: ${PROG}

clean:
	-rm -f ${PROG}
