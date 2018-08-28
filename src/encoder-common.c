/*
 *  encoder-common.c -- asynchronous encoder runtime control and statistics.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "libtc/libtc.h"
#include "encoder-common.h"
#include "tc_defaults.h"


/* volatile: for threadness paranoia */
static int pause_flag = 0;


void tc_pause_request(void)
{
    pause_flag = !pause_flag;
}

void tc_pause(void)
{
    while (pause_flag) {
    	usleep(TC_DELAY_MIN);
    }
}


/* counter, for stats and more */
static uint32_t frames_encoded = 0;
static uint32_t frames_dropped = 0;
static uint32_t frames_skipped = 0;
static uint32_t frames_cloned = 0;
/* counters can be accessed by other (ex: import) threads */
static pthread_mutex_t frame_counter_lock = PTHREAD_MUTEX_INITIALIZER;


uint32_t tc_get_frames_encoded(void)
{
    uint32_t val;

    pthread_mutex_lock(&frame_counter_lock);
    val = frames_encoded;
    pthread_mutex_unlock(&frame_counter_lock);

    return val;
}

void tc_update_frames_encoded(uint32_t val)
{
    pthread_mutex_lock(&frame_counter_lock);
    frames_encoded += val;
    pthread_mutex_unlock(&frame_counter_lock);
}

uint32_t tc_get_frames_dropped(void)
{
    uint32_t val;

    pthread_mutex_lock(&frame_counter_lock);
    val = frames_dropped;
    pthread_mutex_unlock(&frame_counter_lock);

    return val;
}

void tc_update_frames_dropped(uint32_t val)
{
    pthread_mutex_lock(&frame_counter_lock);
    frames_dropped += val;
    pthread_mutex_unlock(&frame_counter_lock);
}

uint32_t tc_get_frames_skipped(void)
{
    uint32_t val;

    pthread_mutex_lock(&frame_counter_lock);
    val = frames_skipped;
    pthread_mutex_unlock(&frame_counter_lock);

    return val;
}

void tc_update_frames_skipped(uint32_t val)
{
    pthread_mutex_lock(&frame_counter_lock);
    frames_skipped += val;
    pthread_mutex_unlock(&frame_counter_lock);
}

uint32_t tc_get_frames_cloned(void)
{
    uint32_t val;

    pthread_mutex_lock(&frame_counter_lock);
    val = frames_cloned;
    pthread_mutex_unlock(&frame_counter_lock);

    return val;
}

void tc_update_frames_cloned(uint32_t val)
{
    pthread_mutex_lock(&frame_counter_lock);
    frames_cloned += val;
    pthread_mutex_unlock(&frame_counter_lock);
}

uint32_t tc_get_frames_skipped_cloned(void)
{
    uint32_t s, c;

    pthread_mutex_lock(&frame_counter_lock);
    s = frames_skipped;
    c = frames_cloned;
    pthread_mutex_unlock(&frame_counter_lock);

    return (c - s);
}

/*************************************************************************/

pthread_mutex_t run_status_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int tc_run_status = TC_STATUS_RUNNING;
/* `volatile' is for threading paranoia */

static TCRunStatus tc_get_run_status(void)
{
    TCRunStatus rs;
    pthread_mutex_lock(&run_status_lock);
    rs = tc_run_status;
    pthread_mutex_unlock(&run_status_lock);
    return rs;
}

int tc_interrupted(void)
{
    return (TC_STATUS_INTERRUPTED == tc_get_run_status());
}

int tc_stopped(void)
{
    return (TC_STATUS_STOPPED == tc_get_run_status());
}

int tc_running(void)
{
    return (TC_STATUS_RUNNING == tc_get_run_status());
}

void tc_start(void)
{
    pthread_mutex_lock(&run_status_lock);
    tc_run_status = TC_STATUS_RUNNING;
    pthread_mutex_unlock(&run_status_lock);
}

void tc_stop(void)
{
    pthread_mutex_lock(&run_status_lock);
    /* no preemption, be polite */
    if (tc_run_status == TC_STATUS_RUNNING) {
        tc_run_status = TC_STATUS_STOPPED;
    }
    pthread_mutex_unlock(&run_status_lock);
}

void tc_interrupt(void)
{
    pthread_mutex_lock(&run_status_lock);
    /* preempt and don't care of politeness. */
    if (tc_run_status != TC_STATUS_INTERRUPTED) {
        tc_run_status = TC_STATUS_INTERRUPTED;
    }
    pthread_mutex_unlock(&run_status_lock);
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
