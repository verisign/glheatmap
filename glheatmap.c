// glheatmap -- OpenGL-based interactive IPv4 heatmap
//
// Copyright (C) 2016 Verisign, Inc.
//
//  This file is part of glheatmap.
//
//  glheatmap is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 2 of the License, or
//  (at your option) any later version.
//
//  glheatmap is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with glheatmap  If not, see <http://www.gnu.org/licenses/>.
//
//  glheatmap utilizes software developed by third parties:
//
//  * ipv4-heatmap, (C) 2007 The Measurement Factory, Inc [GPLv2]
//
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <netinet/in.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <err.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <pthread.h>


#if defined(__linux)
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#elif defined(__APPLE__)
#include <GLUT/glut.h>
#endif

#include "hue2rgb.h"
#include "bbox.h"

/*
 * Compile-time options
 */
#define DATA_DOUBLES 1

/*
 * Preprocessor macros
 */
#define bool int
#define NUM_DATA_COLORS 256
#define _32K 32768
#define _32KD 32768.0
#define _64K 65536

#ifndef MIN
#define MIN(a,b) (a<b?a:b)
#define MAX(a,b) (a>b?a:b)
#endif

typedef struct {
    uint16_t a, b, c, d;
} dq;

/*
 * Program Globals
 */
int DEBUG = 0;

/*
 * Configurable Parameters
 */
static int MAPWIDTH = 1024;
static int MAPHEIGHT = 1024;
static int WINWIDTH = 1280;
static int WINHEIGHT = 786; 
static bool OPT_FULLSCREEN = 0;
static bool OPT_AUTO_POINT_SIZE = 0;
static bool OPT_INPUT_UNTIMED = 0;
static const int ZOOM_STEPS = 20;     // number of steps to double
static int ZOOM_INDEX = 20;
static unsigned int MASK_KEEP = 0xffffffff;
static unsigned int MASK_SET = 0;
static unsigned int OPT_BREAKPOINTS[100];
static unsigned int BREAKPOINT_IDX = 0;


/*
 * Global Constants
 */
static const char *WHITESPACE = " \t\r\n";

/*
 * Non-Config Globals
 */
static unsigned int NOW = 0;
static unsigned int TIMEBASE = 0;
static unsigned int DC_TIME = 0;
static unsigned int NQUERY = 0;
static unsigned int NPIX = 0;
static double ZOOM_BASE;
static GLfloat ZOOM_SCALE = 1.0;
static double QPS = 0;
static bool READING = 0;
static int STREAM = -1;         /* network socket */
static double FILE_TIME;
static double FILE_TIME_OFFSET = 0;	/* difference between file time and wall clock */
static double PLAYBACK_SPEED = 4.0;
static double DRAW_TIME = 0.0;	/* how long drawData() takes */
static GLfloat POINT_SIZE = 0.0;
static GLfloat POINT_SCALE = 1.0;
static void *hud_font = GLUT_BITMAP_9_BY_15;
static void *label_font = GLUT_STROKE_MONO_ROMAN;
static GLfloat TRANS_X = 0.0;
static GLfloat TRANS_Y = 0.0;
static int LAST_BUTTON = 0;
static int DOWNX = 0;
static int DOWNY = 0;
static dq CURSOR_IP;
static unsigned int CURSOR_X;
static unsigned int CURSOR_Y;
static double MAP_X;
static double MAP_Y;
static dq CENTER_IP;
static bbox WINDOW;
static double HALF_LIFE = 10.0; /* seconds */
static unsigned int FADE_START = 0;


static pthread_t threadReadData;
//static pthread_t threadViewUpdate;
static pthread_mutex_t mutexData;

/*
 * External functions which are not in .h files
 */
extern unsigned int xy_from_ip(unsigned ip, unsigned *xp, unsigned *yp);
extern unsigned int ip_from_xy(unsigned x, unsigned y, unsigned *ip);
extern int set_order();
extern void set_bits_per_pixel(int);

/*
 * In-file Prototypes
 */
void decayData(double);
void decayByHalf();

#if DATA_DOUBLES
#define DATA_TYPE double
#else
#define DATA_TYPE uint8_t
#endif
DATA_TYPE ****DATA = 0;

dq
dq_from_ip(unsigned int i)
{
    dq dq;
    dq.a = (i >> 24);
    dq.b = (i >> 16) & 0xFF;
    dq.c = (i >> 8) & 0xFF;
    dq.d = i & 0xFF;
    return dq;
}

unsigned int
ip_from_dq(dq dq)
{
    return (dq.a << 24) | (dq.b << 16) | (dq.c << 8) | dq.d;
}

DATA_TYPE *
data_ptr(unsigned int i)
{
    DATA_TYPE ****A;
    DATA_TYPE ***B;
    DATA_TYPE **C;
    DATA_TYPE *D;
    dq dq = dq_from_ip(i);
    pthread_mutex_lock(&mutexData);
    A = DATA + dq.a;
    if (0 == *A)
	*A = calloc(256, sizeof(**A));
    if (0 == *A)
	return 0;
    B = (*A) + dq.b;
    if (0 == *B)
	*B = calloc(256, sizeof(**B));
    if (0 == *B)
	return 0;
    C = (*B) + dq.c;
    if (0 == *C)
	*C = calloc(256, sizeof(**C));
    if (0 == *C)
	return 0;
    D = (*C) + dq.d;
    pthread_mutex_unlock(&mutexData);
    return D;
}

void
data_inc(unsigned int i)
{
    DATA_TYPE *D = data_ptr((i & MASK_KEEP) | MASK_SET);
    if (0 == D)
	return;
    if (*D < NUM_DATA_COLORS - 1)
	(*D)++;
}

void
data_set(unsigned int i, unsigned int v)
{
    DATA_TYPE *D = data_ptr((i & MASK_KEEP) | MASK_SET);
    if (0 == D)
	return;
    if (v > 255)
	v = 255;
    *D = v;
}

void
read_input_stdin(void)
{
    static double NEXT_PAUSE_CHECK = 0.0;
    static double LAST_QPS_TIME = 0.0;
    static unsigned int PNQUERY;
    char buf[512];
    unsigned int line = 1;
    for (;;) {
	unsigned int i;
	char *strtok_arg = buf;
	char *t;
	char *e;
	while (!READING)
	    usleep(1000);

	if (0 == fgets(buf, 512, stdin)) {
	    READING = 0;
	    return;
	}
	NQUERY++;
	if (OPT_BREAKPOINTS[BREAKPOINT_IDX] == NQUERY) {
	    READING = 0;
	    BREAKPOINT_IDX++;
	    continue;
	}
	/*
	 * The first field is a timestamp
	 */
	t = strtok(strtok_arg, WHITESPACE);
	strtok_arg = NULL;
	if (NULL == t)
	    continue;
	FILE_TIME = strtod(t, &e);
	if (e == t)
	    warnx("bad input parsing time on line %d: %s", line, t);

	/*
	 * next field is an IP address.  We also accept its integer notation
	 * equivalent.
	 */
	t = strtok(strtok_arg, WHITESPACE);
	if (NULL == t)
	    continue;
	if (strspn(t, "0123456789") == strlen(t))
	    i = strtoul(t, NULL, 10);
	else if (1 == inet_pton(AF_INET, t, &i))
	    i = ntohl(i);
	else
	    warnx("bad input parsing IP on line %d: %s", line, t);

	//if (debug)
	    //fprintf(stderr, "%s => %u =>\n", t, i);

	/* check for color value */
	t = strtok(strtok_arg, WHITESPACE);
	if (NULL == t)
		data_inc(i);
	else
		data_set(i, strtoul(t, NULL, 10));
	line++;
	if (FILE_TIME >= NEXT_PAUSE_CHECK) {
	    double now;
	    double delta;
	    struct timeval tv;
	    gettimeofday(&tv, 0);
	    now = tv.tv_sec + 0.000001 * tv.tv_usec;
	    if (0 == FILE_TIME_OFFSET) {
		FILE_TIME_OFFSET = now - (FILE_TIME / PLAYBACK_SPEED);
		delta = 0;
	    } else {
	        delta = (FILE_TIME / PLAYBACK_SPEED) + FILE_TIME_OFFSET - now;
	    }
	    QPS = (double) (NQUERY - PNQUERY) / (FILE_TIME - LAST_QPS_TIME);
	    LAST_QPS_TIME = FILE_TIME;
	    NEXT_PAUSE_CHECK = FILE_TIME + 0.01;
	    PNQUERY = NQUERY;
	    if (delta > 0) {
		unsigned int sleep_usecs = 1000000 * delta;
		usleep(sleep_usecs);
	    }
	}
    }
}

void
read_input_stdin_binary(void)
{
    unsigned int i1;
    unsigned int i2;
    for (;;) {
	while (!READING)
	    usleep(1000);
	if (4 != read(0, &i1, 4)) {
	    READING = 0;
	    return;
	}
	if (4 != read(0, &i2, 4)) {
	    READING = 0;
	    return;
	}
	NQUERY++;
	/*
	 * The first field is a timestamp
	 */
	FILE_TIME = (double)ntohl(i1);
	/*
	 * next field is an IP address.  We also accept its integer notation
	 * equivalent.
	 */
	data_inc(i2);
	if (0 == (NQUERY & 0xfff))
	    usleep(10000);
    }
}

void
read_input_untimed(void)
{
    char buf[512];
    unsigned int line = 1;
    while (line < 8192) {
	unsigned int i;
	char *strtok_arg = buf;
	char *t;

	if (0 == fgets(buf, 512, stdin)) {
	    READING = 0;
	    return;
	}
	NQUERY++;

	/*
	 * first field is an IP address.  We also accept its integer notation
	 * equivalent.
	 */
	t = strtok(strtok_arg, WHITESPACE);
	strtok_arg = NULL;
	if (NULL == t)
	    continue;
	if (strspn(t, "0123456789") == strlen(t))
	    i = strtoul(t, NULL, 10);
	else if (1 == inet_pton(AF_INET, t, &i))
	    i = ntohl(i);
	else
	    warnx("bad input parsing IP on line %d: %s", line, t);

	/*
	 * next (optional) field is a value
	 */
	t = strtok(strtok_arg, WHITESPACE);
	if (NULL == t) {
	    data_inc(i);
	} else {
	    unsigned int v;
	    v = strtoul(t, NULL, 10);
	    data_set(i, v);
	}
	line++;
    }
}

int
blocking_read(int s, void *buf, int len)
{
    int n = 0;
    while (len) {
	int x = read(s, buf, len);
	if (x < 0)
	    return x;
	if (x == 0)
	    usleep(1000);
	n += x;
	len -= x;
	buf += x;
    }
    return n;
}

void
read_input_stream(void)
{
    unsigned int i1;
    unsigned int i2;
    unsigned int i3;
    for (;;) {
	while (!READING)
	    usleep(1000);
	if (4 != blocking_read(STREAM, &i1, 4)) {
	    READING = 0;
	    return;
	}
	if (4 != blocking_read(STREAM, &i2, 4)) {
	    READING = 0;
	    return;
	}
	if (4 != blocking_read(STREAM, &i3, 4)) {
	    READING = 0;
	    return;
	}
	NQUERY++;
	/*
	 * The first field is a timestamp
	 */
	FILE_TIME = (double)ntohl(i1);
	FILE_TIME += .000001 * ntohl(i2);
	data_inc(i3);
    }
}

void *
read_input(void *unused)
{
    if (STREAM > -1)
	read_input_stream();
    else if (OPT_INPUT_UNTIMED)
	read_input_untimed();
    else
	read_input_stdin();
    fprintf(stderr, "exiting read_input()\n");
    return 0;
}

dq
ip_from_map_xy(double mx, double my)
{
    unsigned int i;
    ip_from_xy(mx + 0.5, my + 0.5, &i);
    return dq_from_ip(i);
}

bbox
window_box()
{
    bbox b;
    b.xmin = (int)(_32KD * (1.0 - TRANS_X - 1.0 / ZOOM_SCALE));
    b.xmax = (int)(_32KD * (1.0 - TRANS_X + 1.0 / ZOOM_SCALE));
    b.ymin = (int)(_32KD * (1.0 + TRANS_Y - 1.0 / ZOOM_SCALE));
    b.ymax = (int)(_32KD * (1.0 + TRANS_Y + 1.0 / ZOOM_SCALE));
    return b;
}

bool
box1_is_outside_box2(bbox smaller, bbox larger)
{
    if (smaller.xmax < larger.xmin || smaller.xmin > larger.xmax)
	return 1;
    if (smaller.ymax < larger.ymin || smaller.ymin > larger.ymax)
	return 1;
    return 0;
}

double
auto_point_size()
{
    if (!NPIX)
	return 0;
    return MIN(65536.0 / NPIX, 10.0);
}

void
drawData()
{
    dq dq = {0, 0, 0, 0};
    double R, G, B;
    double R0 = -1.0;
    double G0 = -1.0;
    double B0 = -1.0;

    glViewport(0, 0, MAPWIDTH, MAPHEIGHT);
    glScissor(0, 0, MAPWIDTH, MAPHEIGHT);
    glEnable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glScalef(ZOOM_SCALE, ZOOM_SCALE, ZOOM_SCALE);
    glTranslatef(TRANS_X, TRANS_Y, 0.0);
    glOrtho(0, _64K, _64K, 0, -1, 1);
    //glMatrixMode(GL_MODELVIEW);
    glDisable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);
    POINT_SIZE = POINT_SCALE * ZOOM_SCALE * MAPWIDTH / _64K;
    if (OPT_AUTO_POINT_SIZE) {
	double aps = auto_point_size();
	POINT_SIZE = MAX(POINT_SIZE, aps);
    }
    //if (POINT_SIZE > 0.5)
	//POINT_SIZE = ceil(POINT_SIZE);
    POINT_SIZE = MIN(MAX(POINT_SIZE, 1.0), 64.0);
    glPointSize(POINT_SIZE);

    CENTER_IP = ip_from_map_xy((1.0 - TRANS_X) * _32KD, (1.0 + TRANS_Y) * _32KD);
    NPIX = 0;

    for (dq.a = 0; dq.a < 256; dq.a++) {
	if (!DATA[dq.a])
	    continue;
	dq.b = dq.c = dq.d = 0;
	if (box1_is_outside_box2(bbox_from_int_slash(ip_from_dq(dq), 8), WINDOW))
	    continue;
	for (dq.b = 0; dq.b < 256; dq.b++) {
	    if (!DATA[dq.a][dq.b])
		continue;
	    dq.c = dq.d = 0;
	    if (box1_is_outside_box2(bbox_from_int_slash(ip_from_dq(dq), 16), WINDOW))
		continue;
	    for (dq.c = 0; dq.c < 256; dq.c++) {
		if (!DATA[dq.a][dq.b][dq.c])
		    continue;
		dq.d = 0;
		if (box1_is_outside_box2(bbox_from_int_slash(ip_from_dq(dq), 24), WINDOW))
		    continue;
		for (dq.d = 0; dq.d < 256; dq.d++) {
		    unsigned int x, y;
		    DATA_TYPE v = DATA[dq.a][dq.b][dq.c][dq.d];
		    if (0 == v)
			continue;
		    if (0 == xy_from_ip(ip_from_dq(dq), &x, &y)) {
			fprintf(stderr, "failed to convert ip %u.%u.%u.%u to X,Y\n", dq.a, dq.b, dq.c, dq.d);
			continue;
		    }
		    double hue = 240.0 * (255.0 - v) / 255.0;
		    HUE_TO_RGB(hue, R, G, B);
		    if (R0 != R || G0 != G || B0 != B) {
			glEnd();
			glColor4f(R0 = R, G0 = G, B0 = B, v > FADE_START ? (GLfloat) v : (GLfloat) v / FADE_START);
			glBegin(GL_POINTS);
		    }
		    glVertex2f(x, y);
		    NPIX++;
		}
	    }
	}
    }
    glEnd();
}

void
drawCidrBox(double scale, const char *label, const char *fmt,...)
{
    bbox box;
    char cidr[64];
    va_list ap;
    GLfloat fw;
    const char *s;
    va_start(ap, fmt);
    vsnprintf(cidr, sizeof(cidr), fmt, ap);
    box = bbox_from_cidr(cidr);
    if (box1_is_outside_box2(box, WINDOW))
	return;
    glLineWidth(1.0);
    glBegin(GL_LINE_LOOP);
    glVertex2f(-0.5 + box.xmin, -0.5 + box.ymin);
    glVertex2f(-0.5 + box.xmin, 0.5 + box.ymax);
    glVertex2f(0.5 + box.xmax, 0.5 + box.ymax);
    glVertex2f(0.5 + box.xmax, -0.5 + box.ymin);
    glEnd();
    for (s = label, fw = 0; *s; s++)
	fw += glutStrokeWidth(label_font, *s);
    glPushMatrix();
    fw /= scale;
    glTranslatef((box.xmax + box.xmin - fw) / 2.0, (box.ymax + box.ymin) / 2.0, 0);
    glScalef(1.0 / scale, 1.0 / scale, 1);
    glOrtho(0, 2, 2, 0, 1, -1);
    //the hilbert map is y = 0 at the top
	// glLineWidth(POINT_SIZE * 3.0);
    for (s = label; *s; s++)
	glutStrokeCharacter(label_font, *s);
    glPopMatrix();
}

void
drawLabelsA()
{
    unsigned int a;
    GLfloat alpha = ZOOM_INDEX < 60 ? (20.0 + ZOOM_INDEX) / 80.0 : (140.0 - ZOOM_INDEX) / 80.0;
    if (alpha < 0.0 || alpha > 1.0)
	return;
    glColor4f(1.0, 1.0, 1.0, alpha);
    for (a = 0; a < 256; a++) {
	char buf[128];
	snprintf(buf, sizeof(buf), "%u", a);
	drawCidrBox(0.125, buf, "%u.0.0.0/8", a);
    }
}

void
drawLabelsB()
{
    dq ip = CENTER_IP;
    GLfloat alpha = ZOOM_INDEX < 140 ? ((double)ZOOM_INDEX - 60.0) / 80.0 : (220.0 - ZOOM_INDEX) / 80.0;
    if (alpha < 0.0 || alpha > 1.0)
	return;
    glColor4f(1.0, 1.0, 1.0, alpha);
    for (ip.b = 0; ip.b < 256; ip.b++) {
	char buf[128];
	snprintf(buf, sizeof(buf), "%hu.%hu", ip.a, ip.b);
	drawCidrBox(4.0, buf, "%hu.%hu.0.0/16", ip.a, ip.b);
    }
}

void
drawLabelsC()
{
    dq ip = CENTER_IP;
    GLfloat alpha = ZOOM_INDEX < 220 ? ((double)ZOOM_INDEX - 140.0) / 80.0 : (300.0 - ZOOM_INDEX) / 80.0;
    if (alpha < 0.0 || alpha > 1.0)
	return;
    glColor4f(1.0, 1.0, 1.0, alpha);
    for (ip.c = 0; ip.c < 256; ip.c++) {
	char buf[128];
	snprintf(buf, sizeof(buf), "%hu.%hu.%hu", ip.a, ip.b, ip.c);
	drawCidrBox(128.0, buf, "%hu.%hu.%hu.0/24", ip.a, ip.b, ip.c);
    }
}

void
drawLabels()
{
    if (ZOOM_INDEX < 140)
	drawLabelsA();
    if (ZOOM_INDEX > 60 && ZOOM_INDEX < 220)
	drawLabelsB();
    if (ZOOM_INDEX > 140 && ZOOM_INDEX < 300)
	drawLabelsC();
}

void
drawStr(int x, int y, const char *fmt,...)
{
    va_list ap;
    char buffer[256], *s;
    if (!fmt)
	return;
    if (y < 0)
	y += MAPHEIGHT;
    glRasterPos2d(x, y);
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    for (s = buffer; *s; s++)
	glutBitmapCharacter(hud_font, *s);
}

void
drawText(void)
{
    char tbuf[256];
    time_t theTime = FILE_TIME;
    glColor3f(0.7, 0.7, 0.7);
    unsigned int n = 1;
    int TW, TH;

    if (WINWIDTH > WINHEIGHT) {
	glViewport(MAPWIDTH, 0, TW = (WINWIDTH - MAPWIDTH), TH = WINHEIGHT);
	glScissor(MAPWIDTH, 0, TW, TH);
    } else {
	glViewport(0, MAPHEIGHT, TW = WINWIDTH, TH = (WINHEIGHT - MAPHEIGHT));
	glScissor(0, MAPHEIGHT, TW, TH);
    }
    glEnable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, TW, TH, 0, -1, 1);

    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", gmtime(&theTime));
    drawStr(5, n++ * 15, "File time      %s", tbuf);
    drawStr(5, n++ * 15, "NQUERY         %12u", NQUERY);
    drawStr(5, n++ * 15, "NPIX           %12u", NPIX);
    drawStr(5, n++ * 15, "QPS            %12.2f", QPS);
    drawStr(5, n++ * 15, "DRAW TIME      %12.3f", DRAW_TIME);
    drawStr(5, n++ * 15, "POINT SCALE    %12.3f", POINT_SCALE);
    drawStr(5, n++ * 15, "POINT SIZE     %12.3f", POINT_SIZE);
    n++;
    drawStr(5, n++ * 15, "%s", "Controls");
    drawStr(5, n++ * 15, "[-/=] Scale           %7.3f/%d", ZOOM_SCALE, ZOOM_INDEX);
    drawStr(5, n++ * 15, "[d/D] HALF LIFE       %7.2fs", HALF_LIFE);
    drawStr(5, n++ * 15, "[s/S] PLAYBACK SPEED  %7.0fx", PLAYBACK_SPEED);
    n++;
    drawStr(5, n++ * 15, "%s", "Position");
    drawStr(5, n++ * 15, "Translate      %f, %f", TRANS_X, TRANS_Y);
    drawStr(5, n++ * 15, "Cursor         %u.%u.%u.%u", CURSOR_IP.a, CURSOR_IP.b, CURSOR_IP.c, CURSOR_IP.d);
    drawStr(5, n++ * 15, "Window         %d,%d", CURSOR_X, CURSOR_Y);
    drawStr(5, n++ * 15, "Map X,Y        %7.1f,%7.1f", MAP_X, MAP_Y);
}

GLfloat
zoom_scale()
{
    return pow(ZOOM_BASE, (double)ZOOM_INDEX) / 2.0;
}

void
zoom_scale_up()
{
    ZOOM_INDEX++;
    if (ZOOM_INDEX > (ZOOM_STEPS * 13))
	//4096 = 0.5 << 13
	    ZOOM_INDEX = (ZOOM_STEPS * 13);
    ZOOM_SCALE = zoom_scale();
}

void
zoom_scale_dn()
{
    ZOOM_INDEX--;
    if (ZOOM_INDEX < 0)
	ZOOM_INDEX = 0;
    ZOOM_SCALE = zoom_scale();
}

/* ============================================================================ */




void
cb_Display(void)
{
    unsigned int T0, T1;
    T0 = glutGet(GLUT_ELAPSED_TIME);
    WINDOW = window_box();
    drawData();
    drawLabels();
    T1 = glutGet(GLUT_ELAPSED_TIME);
    DRAW_TIME = 0.001 * (T1 - T0);
    drawText();
    glutSwapBuffers();
}

void
cb_Mouse(int button, int state, int x, int y)
{
    /*
     * These x,y coordinates are based on 0,0 in the top,left corner. Since Y
     * increases downward, need to convert it to map coordinates.
     */
    LAST_BUTTON = button;
    y = WINHEIGHT - y;
    CURSOR_X = x;
    CURSOR_Y = y;
    //fprintf(stderr, "button=%d, state=%d, x=%d, y=%d\n", button, state, x, y);
    if (GLUT_UP == state)
	return;
    if (GLUT_LEFT_BUTTON == button) {
	NOW = glutGet(GLUT_ELAPSED_TIME);
	if (0 == DC_TIME || x != DOWNX || y != DOWNY) {
	    DC_TIME = NOW;
	    DOWNX = x;
	    DOWNY = y;
	} else if (x == DOWNX && y == DOWNY && NOW - DC_TIME < 300) {
	    zoom_scale_up();
	    TRANS_X -= 2.0 * (x - MAPWIDTH / 2) / MAPWIDTH / ZOOM_SCALE;
	    TRANS_Y -= 2.0 * (y - MAPHEIGHT / 2) / MAPHEIGHT / ZOOM_SCALE;
	    DC_TIME = 0;
	} else {
	    DC_TIME = NOW;
	}
    } else if (GLUT_RIGHT_BUTTON == button) {
	DOWNX = x;
	DOWNY = y;
    } else if (3 == button) {
	/* scroll up -- zoom in */
	zoom_scale_up();
	TRANS_X = (2.0 * CURSOR_X / MAPWIDTH - 1.0) / ZOOM_SCALE + 1.0 - MAP_X / _32K;
	TRANS_Y = MAP_Y / _32K - 1.0 - (1.0 - 2.0 * CURSOR_Y / MAPHEIGHT) / ZOOM_SCALE;
    } else if (4 == button) {
	/* scroll down -- zoom out */
	zoom_scale_dn();
	TRANS_X = (2.0 * CURSOR_X / MAPWIDTH - 1.0) / ZOOM_SCALE + 1.0 - MAP_X / _32K;
	TRANS_Y = MAP_Y / _32K - 1.0 - (1.0 - 2.0 * CURSOR_Y / MAPHEIGHT) / ZOOM_SCALE;
    }
    glutPostRedisplay();
}

void
cb_Drag(int x, int y)
{
    static int THEN = 0;
    /*
     * These x,y coordinates are based on 0,0 in the top,left corner. Since Y
     * increases downward, need to convert it to map coordinates.
     */
    if (GLUT_LEFT_BUTTON == LAST_BUTTON) {
	y = WINHEIGHT - y;
	TRANS_X += 2.0 * (x - DOWNX) / MAPWIDTH / ZOOM_SCALE;
	TRANS_Y += 2.0 * (y - DOWNY) / MAPHEIGHT / ZOOM_SCALE;
	DOWNX = x;
	DOWNY = y;
	//printf("TRANS_X=%f TRANS_Y=%f\n", TRANS_X, TRANS_Y);
	glutPostRedisplay();
    } else if (GLUT_RIGHT_BUTTON == LAST_BUTTON) {
	NOW = glutGet(GLUT_ELAPSED_TIME);
	if (x > DOWNX && NOW - THEN > 5) {
	    zoom_scale_up();
	    TRANS_X = (2.0 * CURSOR_X / MAPWIDTH - 1.0) / ZOOM_SCALE + 1.0 - MAP_X / _32K;
	    TRANS_Y = MAP_Y / _32K - 1.0 - (1.0 - 2.0 * CURSOR_Y / MAPHEIGHT) / ZOOM_SCALE;
	    THEN = NOW;
	} else if (x < DOWNX && NOW - THEN > 50) {
	    zoom_scale_dn();
	    TRANS_X = (2.0 * CURSOR_X / MAPWIDTH - 1.0) / ZOOM_SCALE + 1.0 - MAP_X / _32K;
	    TRANS_Y = MAP_Y / _32K - 1.0 - (1.0 - 2.0 * CURSOR_Y / MAPHEIGHT) / ZOOM_SCALE;
	    THEN = NOW;
	}
	DOWNX = x;
	DOWNY = y;
    }
}

void
cb_Motion(int wx, int wy)
{
    /*
     * These x,y coordinates are based on 0,0 in the top,left corner. Since Y
     * increases downward, need to convert it to map coordinates.
     */
    wy = WINHEIGHT - wy;

    MAP_X = _32KD * ((2.0 * wx / MAPWIDTH - 1.0) / ZOOM_SCALE - TRANS_X + 1.0);
    MAP_Y = _32KD * ((1.0 - 2.0 * wy / MAPHEIGHT) / ZOOM_SCALE + TRANS_Y + 1.0);
    CURSOR_IP = ip_from_map_xy(MAP_X, MAP_Y);
    CURSOR_X = wx;
    CURSOR_Y = wy;
    glutPostRedisplay();
}

void
cb_Reshape(int nw, int nh)
{
    WINWIDTH = nw;
    WINHEIGHT = nh;
    MAPHEIGHT = WINHEIGHT < WINWIDTH ? WINHEIGHT : WINWIDTH;
    MAPWIDTH = MAPHEIGHT;
}

void 
resetGL(void)
{
}

void
toggle(bool * v)
{
    *v = !*v;
}

void
cb_SpecialKey(int c, int x, int y)
{
    switch (c) {
    case GLUT_KEY_LEFT:
	TRANS_X += .01 / ZOOM_SCALE;
	break;
    case GLUT_KEY_RIGHT:
	TRANS_X -= .01 / ZOOM_SCALE;
	break;
    case GLUT_KEY_UP:
	TRANS_Y -= .01 / ZOOM_SCALE;
	break;
    case GLUT_KEY_DOWN:
	TRANS_Y += .01 / ZOOM_SCALE;
	break;
    };
    glutPostRedisplay();
}

void
cb_Key(unsigned char c, int x, int y)
{
    switch (c) {
    case '=':
    case '+':
	zoom_scale_up();
	break;
    case '-':
    case '_':
	zoom_scale_dn();
	break;
    case ' ':
	toggle(&READING);
	if (!READING)
	    FILE_TIME_OFFSET = 0;
	break;
    case 'r':
	TRANS_X = TRANS_Y = 0.0;
	ZOOM_INDEX = ZOOM_STEPS;
	zoom_scale_dn();
	break;
    case 'p':
	POINT_SCALE -= 1.0;
	if (POINT_SCALE < 1.0)
	    POINT_SCALE = 1.0;
	break;
    case 'P':
	POINT_SCALE += 1.0;
	break;
    case 'h':
	decayByHalf();
	break;
    case 'd':
	HALF_LIFE -= 1.0;
	if (HALF_LIFE < 0.0)
	    HALF_LIFE = 0.0;
	break;
    case 'D':
	HALF_LIFE += 1.0;
	break;
    case 's':
	FILE_TIME_OFFSET = 0;
	if (PLAYBACK_SPEED)
	    PLAYBACK_SPEED /= 2.0;
	else
	    PLAYBACK_SPEED = 1.0;
	break;
    case 'S':
	FILE_TIME_OFFSET = 0;
	PLAYBACK_SPEED *= 2.0;
	break;
    case 'q':
	exit(0);
    default:
	return;
    }
    glutPostRedisplay();
}

void
cb_Idle(void)
{
    static double FILE_TIME_LAST_DECAY = 0;
    NOW = glutGet(GLUT_ELAPSED_TIME);
    if ((NOW - TIMEBASE) > 10) {
	glutPostRedisplay();
    } else {
	usleep(10000);
    }
    if (HALF_LIFE > 0.0 && FILE_TIME - FILE_TIME_LAST_DECAY >= 0.01) {
	decayData(pow(2.0, -1.0 * (FILE_TIME - FILE_TIME_LAST_DECAY) / HALF_LIFE ));
	FILE_TIME_LAST_DECAY = FILE_TIME;
    }
}

void
decayData(double decay)
{
    dq dq;
    for (dq.a = 0; dq.a < 256; dq.a++) {
	if (!DATA[dq.a])
	    continue;
	for (dq.b = 0; dq.b < 256; dq.b++) {
	    if (!DATA[dq.a][dq.b])
		continue;
	    for (dq.c = 0; dq.c < 256; dq.c++) {
		if (!DATA[dq.a][dq.b][dq.c])
		    continue;
		for (dq.d = 0; dq.d < 256; dq.d++) {
		    if (DATA[dq.a][dq.b][dq.c][dq.d]) {
			DATA[dq.a][dq.b][dq.c][dq.d] *= decay;
		    }
		}
	    }
	}
    }
    glEnd();
}

void
decayByHalf()
{
    dq dq;
    for (dq.a = 0; dq.a < 256; dq.a++) {
	if (!DATA[dq.a])
	    continue;
	for (dq.b = 0; dq.b < 256; dq.b++) {
	    if (!DATA[dq.a][dq.b])
		continue;
	    for (dq.c = 0; dq.c < 256; dq.c++) {
		if (!DATA[dq.a][dq.b][dq.c])
		    continue;
		for (dq.d = 0; dq.d < 256; dq.d++) {
		    if (DATA[dq.a][dq.b][dq.c][dq.d])
#if DATA_DOUBLES
			DATA[dq.a][dq.b][dq.c][dq.d] /= 2.0;
#else
			DATA[dq.a][dq.b][dq.c][dq.d] >>= 1;
#endif
		}
	    }
	}
    }
}

void
open_stream(const char *arg)
{
    char *t = strchr(arg, ':');
    int s;
    struct sockaddr_in S;
    char hello[12];
    int x;
    if (0 == t)
	err(1, "bad stream '%s'", arg);
    *t = 0;
    memset(&S, 0, sizeof(S));
    S.sin_family = AF_INET;
    S.sin_addr.s_addr = inet_addr(arg);
    S.sin_port = htons(atoi(t + 1));
    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
	err(1, "socket");
    if (connect(s, (struct sockaddr *)&S, sizeof(S)) < 0)
	err(1, "connect %s", arg);
    /*
     * if ((flags = fcntl(s, F_GETFL, flags)) < 0) err(1, "fcntl F_GETFL"); if
     * (fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) err(1, "fcntl F_SETFL");
     */
    STREAM = s;
    x = read(s, hello, 12);
    fprintf(stderr, "read hello %d bytes\n", x);
    fprintf(stderr, "read hello '%s'\n", hello);
    if (0 == memcmp(hello, "HELLO THERE!", 12)) {
	fprintf(stderr, "connected, hello received\n");
    } else {
	err(1, "bad hello");
    }
}

int
main(int argc, char *argv[])
{
    int ch;
    char temp[64];
    const char *prog = argv[0];
    char *t;

    memset(OPT_BREAKPOINTS, 0, sizeof(OPT_BREAKPOINTS));

    while ((ch = getopt(argc, argv, "ad:p:s:uFm:b:X:Y:Z:")) != -1) {
	switch (ch) {
	case 'a':
	    OPT_AUTO_POINT_SIZE = 1;
	    break;
	case 'd':
	    HALF_LIFE = strtod(optarg, 0);
	    break;
	case 'p':
	    POINT_SCALE = strtod(optarg, 0);
	    break;
	case 'b':
	    if (BREAKPOINT_IDX < 99) {
		OPT_BREAKPOINTS[BREAKPOINT_IDX] = strtoul(optarg, 0, 0);
		BREAKPOINT_IDX++;
	    }
	    break;
	case 's':
	    open_stream(optarg);
	    break;
	case 'u':
	    OPT_INPUT_UNTIMED = 1;
	    break;
	case 'F':
	    OPT_FULLSCREEN = 1;
	    break;
	case 'm':
	    strtok(optarg, "/");
	    MASK_KEEP = strtoul(optarg, 0, 0);
	    if ((t = strtok(NULL, "")))
		MASK_SET = strtoul(t, 0, 0);
	    break;
	case 'X':
	    TRANS_X = strtod(optarg, 0);
	    break;
	case 'Y':
	    TRANS_Y = strtod(optarg, 0);
	    break;
	case 'Z':
	    ZOOM_INDEX = strtoul(optarg, 0, 0);
	    ZOOM_SCALE = zoom_scale();
	    break;
	default:
	    fprintf(stderr, "usage: %s [-a] [-d half-life] [-p pointscale] [-b breakpoint] [-s stream] [-u] [-F] [-m keep/set]\n", prog);
	    exit(1);
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    BREAKPOINT_IDX = 0;

    DATA = calloc(256, sizeof(***DATA));
    set_bits_per_pixel(0);
    set_order();
    srand48((int)time(NULL));
    ZOOM_BASE = pow(2.0, 1.0 / (double)ZOOM_STEPS);
    zoom_scale_dn();

    glutInit(&argc, argv);
    glutInitWindowSize(WINWIDTH, WINHEIGHT);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_MULTISAMPLE);
    MAPWIDTH = glutGet(GLUT_SCREEN_WIDTH);
    MAPHEIGHT = glutGet(GLUT_SCREEN_HEIGHT);
    sprintf(temp, "%dx%d:32", MAPWIDTH, MAPHEIGHT);
    if ((OPT_FULLSCREEN) && (MAPWIDTH) && (MAPHEIGHT)) {
	glutGameModeString(temp);
	glutEnterGameMode();
    } else {
	glutCreateWindow("GL Heatmap");
    }

    glutDisplayFunc(cb_Display);
    glutMouseFunc(cb_Mouse);
    glutMotionFunc(cb_Drag);
    glutPassiveMotionFunc(cb_Motion);
    glutKeyboardFunc(cb_Key);
    glutSpecialFunc(cb_SpecialKey);
    glutReshapeFunc(cb_Reshape);
    glutIdleFunc(cb_Idle);

    //glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    pthread_create(&threadReadData, 0, read_input, 0);
    //pthread_create(&threadViewUpdate, 0, view_update, 0);
    glutMainLoop();
    pthread_join(threadReadData, 0);
    return 0;
}
