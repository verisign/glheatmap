NAME=glheatmap
OBJS=${NAME}.o xy_from_ip.o cidr.o hilbert.o bbox.o
UNAME_S := $(shell uname -s)

# Linux
ifeq ($(UNAME_S),Linux)
	LIBS=-lGL -lglut -lm -pthread
	CFLAGS = -g -Wall -I/usr/local/include -I/usr/X11/include
endif

# Mac
ifeq ($(UNAME_S),Darwin)
	LIBS=-framework GLUT -framework OpenGL -framework Cocoa
	CFLAGS = -g -Wall -Wno-deprecated
endif


all: ${NAME}

${NAME}: ${OBJS}
	${CC} -g -o $@ ${OBJS} ${LIBS}

clean:
	rm -f ${OBJS}
	rm -f ${NAME}

tarball:
	tar czvf ${NAME}.tar.gz *.c *.h Makefile
