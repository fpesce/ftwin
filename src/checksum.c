/*
--------------------------------------------------------------------
checksum.c, by Bob Jenkins, 1996, Public Domain
hash(), hash2(), and mix() are the only externally useful functions.
Routines to test the hash are included if SELF_TEST is defined.
You can use this free for any purpose.  It has no warranty.
--------------------------------------------------------------------

Adapted to work with apr, mainly apr_uint32_t for ub4.
*/
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "checksum.h"

#define HASHLEN   HASHSTATE

/*
--------------------------------------------------------------------
Mix -- mix 8 4-bit values as quickly and thoroughly as possible.
Repeating mix() three times achieves avalanche.
Repeating mix() four times eliminates all known funnels.
--------------------------------------------------------------------
*/
#define mix(a,b,c,d,e,f,g,h) \
{ \
   a^=b<<11; d+=a; b+=c; \
   b^=c>>2;  e+=b; c+=d; \
   c^=d<<8;  f+=c; d+=e; \
   d^=e>>16; g+=d; e+=f; \
   e^=f<<10; h+=e; f+=g; \
   f^=g>>4;  a+=f; g+=h; \
   g^=h<<8;  b+=g; h+=a; \
   h^=a>>9;  c+=h; a+=b; \
}

extern void hash(k, len, state)
     register ub1 *k;
     register apr_uint32_t len;
     register apr_uint32_t *state;
{
    register apr_uint32_t a, b, c, d, e, f, g, h, length;

    /* Use the length and level; add in the golden ratio. */
    length = len;
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

   /*---------------------------------------- handle most of the key */
    while (len >= 32) {
	a += (k[0] + (k[1] << 8) + (k[2] << 16) + (k[3] << 24));
	b += (k[4] + (k[5] << 8) + (k[6] << 16) + (k[7] << 24));
	c += (k[8] + (k[9] << 8) + (k[10] << 16) + (k[11] << 24));
	d += (k[12] + (k[13] << 8) + (k[14] << 16) + (k[15] << 24));
	e += (k[16] + (k[17] << 8) + (k[18] << 16) + (k[19] << 24));
	f += (k[20] + (k[21] << 8) + (k[22] << 16) + (k[23] << 24));
	g += (k[24] + (k[25] << 8) + (k[26] << 16) + (k[27] << 24));
	h += (k[28] + (k[29] << 8) + (k[30] << 16) + (k[31] << 24));
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	k += 32;
	len -= 32;
    }

   /*------------------------------------- handle the last 31 bytes */
    h += length;
    switch (len) {
    case 31:
	h += (k[30] << 24);
    case 30:
	h += (k[29] << 16);
    case 29:
	h += (k[28] << 8);
    case 28:
	g += (k[27] << 24);
    case 27:
	g += (k[26] << 16);
    case 26:
	g += (k[25] << 8);
    case 25:
	g += k[24];
    case 24:
	f += (k[23] << 24);
    case 23:
	f += (k[22] << 16);
    case 22:
	f += (k[21] << 8);
    case 21:
	f += k[20];
    case 20:
	e += (k[19] << 24);
    case 19:
	e += (k[18] << 16);
    case 18:
	e += (k[17] << 8);
    case 17:
	e += k[16];
    case 16:
	d += (k[15] << 24);
    case 15:
	d += (k[14] << 16);
    case 14:
	d += (k[13] << 8);
    case 13:
	d += k[12];
    case 12:
	c += (k[11] << 24);
    case 11:
	c += (k[10] << 16);
    case 10:
	c += (k[9] << 8);
    case 9:
	c += k[8];
    case 8:
	b += (k[7] << 24);
    case 7:
	b += (k[6] << 16);
    case 6:
	b += (k[5] << 8);
    case 5:
	b += k[4];
    case 4:
	a += (k[3] << 24);
    case 3:
	a += (k[2] << 16);
    case 2:
	a += (k[1] << 8);
    case 1:
	a += k[0];
    }
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);

   /*-------------------------------------------- report the result */
    state[0] = a;
    state[1] = b;
    state[2] = c;
    state[3] = d;
    state[4] = e;
    state[5] = f;
    state[6] = g;
    state[7] = h;
}

extern void hash2(k, len, state)
     register ub1 *k;
     register apr_uint32_t len;
     register apr_uint32_t *state;
{
    register apr_uint32_t a, b, c, d, e, f, g, h, length;

    /* Use the length and level; add in the golden ratio. */
    length = len;
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

   /*---------------------------------------- handle most of the key */
    while (len >= 32) {
	a += *(apr_uint32_t *) (k + 0);
	b += *(apr_uint32_t *) (k + 4);
	c += *(apr_uint32_t *) (k + 8);
	d += *(apr_uint32_t *) (k + 12);
	e += *(apr_uint32_t *) (k + 16);
	f += *(apr_uint32_t *) (k + 20);
	g += *(apr_uint32_t *) (k + 24);
	h += *(apr_uint32_t *) (k + 28);
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	mix(a, b, c, d, e, f, g, h);
	k += 32;
	len -= 32;
    }

   /*------------------------------------- handle the last 31 bytes */
    h += length;
    switch (len) {
    case 31:
	h += (k[30] << 24);
    case 30:
	h += (k[29] << 16);
    case 29:
	h += (k[28] << 8);
    case 28:
	g += (k[27] << 24);
    case 27:
	g += (k[26] << 16);
    case 26:
	g += (k[25] << 8);
    case 25:
	g += k[24];
    case 24:
	f += (k[23] << 24);
    case 23:
	f += (k[22] << 16);
    case 22:
	f += (k[21] << 8);
    case 21:
	f += k[20];
    case 20:
	e += (k[19] << 24);
    case 19:
	e += (k[18] << 16);
    case 18:
	e += (k[17] << 8);
    case 17:
	e += k[16];
    case 16:
	d += (k[15] << 24);
    case 15:
	d += (k[14] << 16);
    case 14:
	d += (k[13] << 8);
    case 13:
	d += k[12];
    case 12:
	c += (k[11] << 24);
    case 11:
	c += (k[10] << 16);
    case 10:
	c += (k[9] << 8);
    case 9:
	c += k[8];
    case 8:
	b += (k[7] << 24);
    case 7:
	b += (k[6] << 16);
    case 6:
	b += (k[5] << 8);
    case 5:
	b += k[4];
    case 4:
	a += (k[3] << 24);
    case 3:
	a += (k[2] << 16);
    case 2:
	a += (k[1] << 8);
    case 1:
	a += k[0];
    }
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);
    mix(a, b, c, d, e, f, g, h);

   /*-------------------------------------------- report the result */
    state[0] = a;
    state[1] = b;
    state[2] = c;
    state[3] = d;
    state[4] = e;
    state[5] = f;
    state[6] = g;
    state[7] = h;
}

#ifdef SELF_TEST

/* check that every input bit changes every output bit half the time */
#define MAXPAIR  80
#define MAXLEN   70
/* used for timings */
void driver1()
{
    ub1 buf[256];
    apr_uint32_t i;
    apr_uint32_t state[8];

    for (i = 0; i < 256; ++i) {
	hash(buf, i, state);
    }
}

void driver2()
{
    ub1 qa[MAXLEN + 1], qb[MAXLEN + 2], *a = &qa[0], *b = &qb[1];
    apr_uint32_t c[HASHSTATE], d[HASHSTATE], i, j = 0, k, l, m, z;
    apr_uint32_t e[HASHSTATE], f[HASHSTATE], g[HASHSTATE], h[HASHSTATE];
    apr_uint32_t x[HASHSTATE], y[HASHSTATE];
    apr_uint32_t hlen;

    printf("No more than %d trials should ever be needed \n", MAXPAIR / 2);
    for (hlen = 0; hlen < MAXLEN; ++hlen) {
	z = 0;
	for (i = 0; i < hlen; ++i) {
/*----------------------- for each byte, */
	    for (j = 0; j < 8; ++j) {
/*------------------------ for each bit, */
		for (m = 1; m < 8; ++m) {
/*-------- for serveral possible levels, */
		    for (l = 0; l < HASHSTATE; ++l)
			e[l] = f[l] = g[l] = h[l] = x[l] = y[l] = ~((apr_uint32_t) 0);

	  /*---- check that every input bit affects every output bit */
		    for (k = 0; k < MAXPAIR; k += 2) {
			apr_uint32_t finished = 1;
			/* keys have one bit different */
			for (l = 0; l < hlen + 1; ++l) {
			    a[l] = b[l] = (ub1) 0;
			}
			/* have a and b be two keys differing in only one bit */
			for (l = 0; l < HASHSTATE; ++l) {
			    c[l] = d[l] = m;
			}
			a[i] ^= (k << j);
			a[i] ^= (k >> (8 - j));
			hash(a, hlen, c);
			b[i] ^= ((k + 1) << j);
			b[i] ^= ((k + 1) >> (8 - j));
			hash(b, hlen, d);
			/* check every bit is 1, 0, set, and not set at least once */
			for (l = 0; l < HASHSTATE; ++l) {
			    e[l] &= (c[l] ^ d[l]);
			    f[l] &= ~(c[l] ^ d[l]);
			    g[l] &= c[l];
			    h[l] &= ~c[l];
			    x[l] &= d[l];
			    y[l] &= ~d[l];
			    if (e[l] | f[l] | g[l] | h[l] | x[l] | y[l])
				finished = 0;
			}
			if (finished)
			    break;
		    }
		    if (k > z)
			z = k;
		    if (k == MAXPAIR) {
			apr_uint32_t *bob;
			for (l = 0; l < HASHSTATE; ++l) {
			    if (e[l] | f[l] | g[l] | h[l] | x[l] | y[l])
				break;
			}
			bob = e[l] ? e : f[l] ? f : g[l] ? g : h[l] ? h : x[l] ? x : y;
			printf("Some bit didn't change: ");
			for (l = 0; l < HASHSTATE; ++l)
			    printf("%.8x ", bob[l]);
			printf("  i %d j %d len %d\n", i, j, hlen);
		    }
		    if (z == MAXPAIR)
			goto done;
		}
	    }
	}
      done:
	if (z < MAXPAIR) {
	    printf("Mix success  %2d bytes  %2d levels  ", i, j);
	    printf("required  %d  trials\n", z / 2);
	}
    }
}

/* Check for reading beyond the end of the buffer and alignment problems */
void driver3()
{
    ub1 buf[MAXLEN + 20], *b;
    apr_uint32_t len;
    ub1 q[] = "This is the time for all good men to come to the aid of their country";
    ub1 qq[] = "xThis is the time for all good men to come to the aid of their country";
    ub1 qqq[] = "xxThis is the time for all good men to come to the aid of their country";
    ub1 qqqq[] = "xxxThis is the time for all good men to come to the aid of their country";
    apr_uint32_t h, i, j, ref[HASHSTATE], x[HASHSTATE], y[HASHSTATE];

    printf("Endianness.  These should all be the same:\n");
    for (j = 0; j < HASHSTATE; ++j)
	ref[j] = (apr_uint32_t) 0;
    hash(q, sizeof(q) - 1, ref);
    for (j = 0; j < HASHSTATE; ++j)
	printf("%.8x", ref[j]);
    printf("\n");
    for (j = 0; j < HASHSTATE; ++j)
	ref[j] = (apr_uint32_t) 0;
    hash(qq + 1, sizeof(q) - 1, ref);
    for (j = 0; j < HASHSTATE; ++j)
	printf("%.8x", ref[j]);
    printf("\n");
    for (j = 0; j < HASHSTATE; ++j)
	ref[j] = (apr_uint32_t) 0;
    hash(qqq + 2, sizeof(q) - 1, ref);
    for (j = 0; j < HASHSTATE; ++j)
	printf("%.8x", ref[j]);
    printf("\n");
    for (j = 0; j < HASHSTATE; ++j)
	ref[j] = (apr_uint32_t) 0;
    hash(qqqq + 3, sizeof(q) - 1, ref);
    for (j = 0; j < HASHSTATE; ++j)
	printf("%.8x", ref[j]);
    printf("\n\n");

    for (h = 0, b = buf + 1; h < 8; ++h, ++b) {
	for (i = 0; i < MAXLEN; ++i) {
	    len = i;
	    for (j = 0; j < MAXLEN + 4; ++j)
		b[j] = 0;

	    /* these should all be equal */
	    for (j = 0; j < HASHSTATE; ++j)
		ref[j] = x[j] = y[j] = (apr_uint32_t) 0;
	    hash(b, len, ref);
	    b[i] = (ub1) ~ 0;
	    hash(b, len, x);
	    hash(b, len, y);
	    for (j = 0; j < HASHSTATE; ++j) {
		if ((ref[j] != x[j]) || (ref[j] != y[j])) {
		    printf("alignment error: %.8x %.8x %.8x %d %d\n", ref[j], x[j], y[j], h, i);
		}
	    }
	}
    }
}

/* check for problems with nulls */
void driver4()
{
    ub1 buf[1];
    apr_uint32_t i, j, state[HASHSTATE];


    buf[0] = ~0;
    for (i = 0; i < HASHSTATE; ++i)
	state[i] = 1;
    printf("These should all be different\n");
    for (i = 0; i < 8; ++i) {
	hash(buf, (apr_uint32_t) 0, state);
	printf("%2d  strings  ", i);
	for (j = 0; j < HASHSTATE; ++j)
	    printf("%.8x", state[j]);
	printf("\n");
    }
    printf("\n");
}


int main()
{
    driver1();			/* test that the key is hashed: used for timings */
    driver2();			/* test that whole key is hashed thoroughly */
    driver3();			/* test that nothing but the key is hashed */
    driver4();			/* test hashing multiple buffers (all buffers are null) */
    return 1;
}

#endif /* SELF_TEST */
