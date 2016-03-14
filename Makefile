NAME=glheatmap
OBJS=${NAME}.o xy_from_ip.o cidr.o hilbert.o bbox.o

# Linux
LIBS=-lGL -lglut -lm -pthread
CFLAGS = -g -Wall -I/usr/local/include -I/usr/X11/include

# Mac
LIBS=-framework GLUT -framework OpenGL -framework Cocoa
CFLAGS = -g -Wall -Wno-deprecated


all: ${NAME}

${NAME}: ${OBJS}
	${CC} -g -o $@ ${OBJS} ${LIBS}

clean:
	rm -f ${OBJS}
	rm -f ${NAME}

tarball:
	tar czvf ${NAME}.tar.gz *.c *.h Makefile
