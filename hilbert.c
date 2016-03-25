
/*
 * Figure 14-5 from Hacker's Delight (by Henry S. Warren, Jr. published by
 * Addison Wesley, 2002)
 * 
 * From http://www.hackersdelight.org/permissions.htm:
 *
 * You are free to use, copy, and distribute any of the code on
 * this web site, whether modified by you or not. You need not give
 * attribution. This includes the algorithms (some of which appear
 * in Hacker's Delight), the Hacker's Assistant, and any code
 * submitted by readers. Submitters implicitly agree to this.
 *
 * The textural material and pictures are copyright by the author,
 * and the usual copyright rules apply. E.g., you may store the
 * material on your computer and make hard or soft copies for your
 * own use. However, you may not incorporate this material into
 * another publication without written permission from the author
 * (which the author may give by email).
 *
 * The author has taken care in the preparation of this material,
 * but makes no expressed or implied warranty of any kind and assumes
 * no responsibility for errors or omissions. No liability is assumed
 * for incidental or consequential damages in connection with or
 * arising out of the use of the information or programs contained
 * herein.
 */

void
hil_xy_from_s(unsigned s, int order, unsigned *xp, unsigned *yp)
{

    int i;
    unsigned state, x, y, row;

    state = 0;			/* Initialize. */
    x = y = 0;

    for (i = 2 * order - 2; i >= 0; i -= 2) {	/* Do n times. */
	row = 4 * state | ((s >> i) & 3);	/* Row in table. */
	x = (x << 1) | ((0x936C >> row) & 1);
	y = (y << 1) | ((0x39C6 >> row) & 1);
	state = (0x3E6B94C1 >> 2 * row) & 3;	/* New state. */
    }
    *xp = x;			/* Pass back */
    *yp = y;			/* results. */
}


void
hil_s_from_xy(unsigned int x, unsigned int y, int order, unsigned int *s)
{
  int i = 0;
  unsigned int state = 0;
  unsigned int row;
  for (i = order - 1; i >= 0; i--) {
    row = (4*state) | (2*((x >> i)&1)) | ((y >> i)&1);
    *s = (*s<<2) | ((0x361E9CB4 >> (2*row)) & 3);
    state = (0x8FE65831 >> (2*row)) & 3;
  }
}
