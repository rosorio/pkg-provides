/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
 * Copyright (c) 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/param.h>

#include "bigram.h"

char separator='\n';   /* line separator */

/* 
 * Validate bigram chars. If the test failed the database is corrupt 
 * or the database is obviously not a locate database.
 */
int
check_bigram_char(ch)
    int ch;
{
    /* legal bigram: 0, ASCII_MIN ... ASCII_MAX */
    if (ch == 0 ||
        (ch >= ASCII_MIN && ch <= ASCII_MAX))
        return(ch);

    fprintf(stderr, "Provides database corrupted, perform a forced update to correct it.\n");
    exit (1);
}

/*
 * Read integer from mmap pointer.
 * Essentially a simple ``return *(int *)p'' but avoids sigbus
 * for integer alignment (SunOS 4.x, 5.x).
 *
 * Convert network byte order to host byte order if necessary.
 * So we can read a locate database on FreeBSD/i386 (little endian)
 * which was built on SunOS/sparc (big endian).
 */

int
getwm(p)
    caddr_t p;
{
    union {
        char buf[INTSIZE];
        int i;
    } u;
    register int i, hi;

    for (i = 0; i < (int)INTSIZE; i++)
        u.buf[i] = *p++;

    i = u.i;

    if (i > MAXPATHLEN || i < -(MAXPATHLEN)) {
        hi = ntohl(i);
        if (hi > MAXPATHLEN || hi < -(MAXPATHLEN)) {
            fprintf(stderr, "integer out of +-MAXPATHLEN (%d): %u",
                    MAXPATHLEN, abs(i) < abs(hi) ? i : hi);
            exit (1);
        }
        return(hi);
    }
    return(i);
}

/*
 * Read integer from stream.
 *
 * Convert network byte order to host byte order if necessary.
 * So we can read on FreeBSD/i386 (little endian) a locate database
 * which was built on SunOS/sparc (big endian).
 */

int
getwf(fp)
    FILE *fp;
{
    register int word, hword;

    word = getw(fp);

    if (word > MAXPATHLEN || word < -(MAXPATHLEN)) {
        hword = ntohl(word);
        if (hword > MAXPATHLEN || hword < -(MAXPATHLEN))
            errx(1, "integer out of +-MAXPATHLEN (%d): %u",
                MAXPATHLEN, abs(word) < abs(hword) ? word : hword);
        return(hword);
    }
    return(word);
}

int
bigram_expand(FILE *fp, int (*match_cb)(char *,void *), void * extra)
{
    register u_char *p, *s;
    register int c;
    int count, found, globflag;
    u_char *cutoff;
    u_char bigram1[NBG], bigram2[NBG], path[MAXPATHLEN];

    /* use a lookup table for case insensitive search */
    u_char table[UCHAR_MAX + 1];

    /* init bigram table */
    for (c = 0, p = bigram1, s = bigram2; c < NBG; c++) {
        p[c] = check_bigram_char(getc(fp));
        s[c] = check_bigram_char(getc(fp));
    }

    /* main loop */
    found = count = 0;

    c = getc(fp);
    for (; c != EOF; ) {

        /* go forward or backward */
        if (c == SWITCH) { /* big step, an integer */
            count +=  getwf(fp) - OFFSET;
        } else {       /* slow step, =< 14 chars */
            count += c - OFFSET;
        }

        if (count < 0 || count > MAXPATHLEN)
            return (-1);
        /* overlay old path */
        p = path + count;

        for (;;) {
            c = getc(fp);
            /*
             * == UMLAUT: 8 bit char followed
             * <= SWITCH: offset
             * >= PARITY: bigram
             * rest:      single ascii char
             *
             * offset < SWITCH < UMLAUT < ascii < PARITY < bigram
             */
            if (c < PARITY) {
                if (c <= UMLAUT) {
                    if (c == UMLAUT) {
                        c = getc(fp);
                    } else
                        break; /* SWITCH */
                }
                *p++ = c;
            }
            else {      
                /* bigrams are parity-marked */
                TO7BIT(c);

                *p++ = bigram1[c];
                *p++ = bigram2[c];
            }
        }
        *p-- = '\0';
        match_cb((char *)path, extra);
    }

    return (0);
}
