/*
 *  encoder-common.h -- asynchronous encoder runtime control and statistics.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
 *
 *  This file is part of transcode, a video stream  processing tool
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

#ifndef ENCODER_COMMON_H
#define ENCODER_COMMON_H

/*
 * MULTITHREADING NOTE:
 * It is *GUARANTEED SAFE* to call those functions from different threads.
 */
/*************************************************************************/

/*
 * tc_get_frames_{dropped,skipped,encoded,cloned,skipped_cloned}:
 *     get the current value of a frame counter.
 *
 * Parameters:
 *     None
 * Return Value:
 *     the current value of requested counter
 */
uint32_t tc_get_frames_dropped(void);
uint32_t tc_get_frames_skipped(void);
uint32_t tc_get_frames_encoded(void);
uint32_t tc_get_frames_cloned(void);
uint32_t tc_get_frames_skipped_cloned(void);

/*
 * tc_update_frames_{dropped,skipped,encoded,cloned}:
 *     update the current value of a frame counter of a given value.
 *
 * Parameters:
 *     val: value to be added to the current value of requested counter.
 *     This parameter is usually just '1' (one)
 * Return Value:
 *     None
 */
void tc_update_frames_dropped(uint32_t val);
void tc_update_frames_skipped(uint32_t val);
void tc_update_frames_encoded(uint32_t val);
void tc_update_frames_cloned(uint32_t val);

/*************************************************************************/

/*
 * tc_pause_request:
 *     toggle pausing; if pausing is enabled, further calls to tc_pause()
 *     will effectively pause application's current thread; otherwise,
 *     tc_pause() calls will do just nothing.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */
void tc_pause_request(void);

/*
 * tc_pause:
 *     if pausing enabled, so if tc_pause_request was previously called
 *     at least once, pause the current application thread for at least
 *     TC_DELAY_MIN microseconds.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 * Side effects:
 *     Incoming socket requests (see socket code), if any, will be handled
 *     before to return.
 */
void tc_pause(void);

/*************************************************************************/
/*                   encoder (core) run control                          */
/*************************************************************************/

typedef enum tcrunstatus_ TCRunStatus;
enum tcrunstatus_  {
    TC_STATUS_RUNNING = 0,       /* default condition                    */
    TC_STATUS_STOPPED = 1,       /* regular stop or end of stream reched */
    TC_STATUS_INTERRUPTED = -1,  /* forced interruption (^C)             */
};

/*
 * tc_interrupt: perform an hard stop of encoder core. 
 * This means that all transcode parts has to stop as soon and as quickly
 * as is possible.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 */
void tc_interrupt(void);

/*
 * tc_stop: perform a soft stop of encoder core. Tipically, this function
 * is invoked after end of stream was reached, or after all requested
 * stream ranges were encoded succesfully, to notify all the transcode
 * parts to shutdown properly.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 */
void tc_stop(void);

/*
 * tc_interrupted (Thread safe): verify if the encoder (core) was halted
 * in response of an interruption.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      1: halting cause of encoder (core) was (user) interruption (^C).
 *      0: otherwise.
 *
 * PLEASE NOTE that if this function will return 0 even if encoder (core)
 * IS STILL RUNNING!
 */
int tc_interrupted(void);

/*
 * tc_stopped (Thread safe): verify if the encoder (core) was halted
 * regulary, most likely because end of stream was reached.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      1: halting cause of encoder (core) was regular (EOS).
 *      0: otherwise.
 *
 * PLEASE NOTE that if this function will return 0 even if encoder (core)
 * IS STILL RUNNING!
 */
int tc_stopped(void);

/*
 * tc_running (Thread safe): checks if encoder (core) is still running.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      1: encoder (core) is still running.
 *      0: encoder (core) not running.
 */
int tc_running(void);

/*
 * tc_start: start the encoder core. Tipically, this function
 * is invoked once at the start of the processing; however, some core modes
 * (e.g. PSU mode) may require multiple start.
 * Every call to this function should be paired with a tc_stop() call into
 * the same code path; however, it is safe to call this function multiple
 * times. 
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 */
void tc_start(void);

#endif /* ENCODER_COMMON_H */
