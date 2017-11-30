/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2015 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <libutil.h>
#include <utstring.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#define STALL_TIME 5

static char *progress_message = NULL;
static UT_string *msg_buf = NULL;
static int last_progress_percent = -1;
static bool progress_started = false;
static bool progress_interrupted = false;
static bool progress_debit = false;
static int64_t last_tick = 0;
static int64_t stalled;
static int64_t bytes_per_second;
static time_t last_update;
static time_t begin = 0;
static int add_deps_depth;
static bool signal_handler_installed = false;
static bool quiet = false;

/* units for format_size */
static const char *unit_SI[] = { " ", "k", "M", "G", "T", };

static void
format_rate_SI(char *buf, int size, off_t bytes)
{
    int i;

    bytes *= 100;
    for (i = 0; bytes >= 100*1000 && unit_SI[i][0] != 'T'; i++)
        bytes = (bytes + 500) / 1000;
    if (i == 0) {
        i++;
        bytes = (bytes + 500) / 1000;
    }
    snprintf(buf, size, "%3lld.%1lld%s%s",
        (long long) (bytes + 5) / 100,
        (long long) (bytes + 5) / 10 % 10,
        unit_SI[i],
        i ? "B" : " ");
}

void
provides_progressbar_stop(void)
{
    if (progress_started) {
        if (!isatty(STDOUT_FILENO))
            printf(" done");
        putchar('\n');
    }
    last_progress_percent = -1;
    progress_started = false;
    progress_interrupted = false;
}

void
provides_progressbar_start(const char *pmsg) {
    free(progress_message);
    progress_message = NULL;
    progress_debit = true;

    if (quiet)
        return;
    if (pmsg != NULL)
        progress_message = strdup(pmsg);
    else {
        progress_message = strdup(utstring_body(msg_buf));
    }
    last_progress_percent = -1;
    last_tick = 0;
    begin = last_update = time(NULL);
    bytes_per_second = 0;
    stalled = 0;

    progress_started = true;
    progress_interrupted = false;
    if (!isatty(STDOUT_FILENO))
        printf("%s: ", progress_message);
    else
        printf("%s:   0%%", progress_message);
}

static void
draw_progressbar(int64_t current, int64_t total)
{
    int percent;
    int64_t transferred;
    time_t elapsed = 0, now = 0;
    char buf[8];
    int64_t bytes_left;
    int cur_speed;
    int hours, minutes, seconds;
    float age_factor;

    if (!progress_started) {
        provides_progressbar_stop();
        return;
    }

    if (progress_debit) {
        now = time(NULL);
        elapsed = (now >= last_update) ? now - last_update : 0;
    }

    percent = (total != 0) ? (current * 100. / total) : 100;

    /**
     * Wait for interval for debit bars to keep calc per second.
     * If not debit, show on every % change, or if ticking after
     * an interruption (which removed our progressbar output).
     */
    if (current >= total || (progress_debit && elapsed >= 1) ||
        (!progress_debit &&
        (percent != last_progress_percent || progress_interrupted))) {
        last_progress_percent = percent;

        printf("\r%s: %3d%%", progress_message, percent);
        if (progress_debit) {
            transferred = current - last_tick;
            last_tick = current;
            bytes_left = total - current;
            if (bytes_left <= 0) {
                elapsed = now - begin;
                /* Always show at least 1 second at end. */
                if (elapsed == 0)
                    elapsed = 1;
                /* Calculate true total speed when done */
                transferred = total;
                bytes_per_second = 0;
            }

            if (elapsed != 0)
                cur_speed = (transferred / elapsed);
            else
                cur_speed = transferred;

#define AGE_FACTOR_SLOW_START 3
            if (now - begin <= AGE_FACTOR_SLOW_START)
                age_factor = 0.4;
            else
                age_factor = 0.9;

            if (bytes_per_second != 0) {
                bytes_per_second =
                    (bytes_per_second * age_factor) +
                    (cur_speed * (1.0 - age_factor));
            } else
                bytes_per_second = cur_speed;

            humanize_number(buf, sizeof(buf),
                current,"B", HN_AUTOSCALE, HN_IEC_PREFIXES);
            printf(" %*s", (int)sizeof(buf), buf);

            if (bytes_left > 0)
                format_rate_SI(buf, sizeof(buf), transferred);
            else /* Show overall speed when done */
                format_rate_SI(buf, sizeof(buf),
                    bytes_per_second);
            printf(" %s/s ", buf);

            if (!transferred)
                stalled += elapsed;
            else
                stalled = 0;

            if (stalled >= STALL_TIME)
                printf(" - stalled -");
            else if (bytes_per_second == 0 && bytes_left > 0)
                printf("   --:-- ETA");
            else {
                if (bytes_left > 0)
                    seconds = bytes_left / bytes_per_second;
                else
                    seconds = elapsed;

                hours = seconds / 3600;
                seconds -= hours * 3600;
                minutes = seconds / 60;
                seconds -= minutes * 60;

                if (hours != 0)
                    printf("%02d:%02d:%02d", hours,
                        minutes, seconds);
                else
                    printf("   %02d:%02d", minutes, seconds);

                if (bytes_left > 0)
                    printf(" ETA");
                else
                    printf("    ");
                last_update = now;
            }
        }
        fflush(stdout);
    }
    if (current >= total)
        return provides_progressbar_stop();
}

void
provides_progressbar_tick(int64_t current, int64_t total)
{
    int percent;

    if (!quiet && progress_started) {
        if (isatty(STDOUT_FILENO))
            draw_progressbar(current, total);
        else {
            if (progress_interrupted) {
                printf("%s...", progress_message);
            } else if (!getenv("NO_TICK")){
                percent = (total != 0) ? (current * 100. / total) : 100;
                if (last_progress_percent / 10 < percent / 10) {
                    last_progress_percent = percent;
                    printf(".");
                    fflush(stdout);
                }
            }
            if (current >= total)
                provides_progressbar_stop();
        }
    }
    progress_interrupted = false;
}
