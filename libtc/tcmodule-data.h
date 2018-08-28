/*
 * tcmodule-data.h -- transcode module system, take two: data types.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * this header file contains basic data types declarations for transcode's
 * new module system (1.1.x and later).
 * Should not be included directly, but doing this will not harm anything.
 */
#ifndef TCMODULE_DATA_H
#define TCMODULE_DATA_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#include "framebuffer.h"
#include "transcode.h"
#include "tcmodule-info.h"

#define TC_MODULE_VERSION_MAJOR     2
#define TC_MODULE_VERSION_MINOR     0
#define TC_MODULE_VERSION_MICRO     0

#define TC_MAKE_MOD_VERSION(MAJOR, MINOR, MICRO) \
         (((    0UL & 0xFF) << 24) \
         |(((MAJOR) & 0xFF) << 16) \
         |(((MINOR) & 0xFF) <<  8) \
         | ((MICRO) & 0xFF))

#define TC_MODULE_VERSION   \
        TC_MAKE_MOD_VERSION(TC_MODULE_VERSION_MAJOR, \
                            TC_MODULE_VERSION_MINOR, \
                            TC_MODULE_VERSION_MICRO)

/*
 * allowed status transition chart:
 *
 *                     init                 configure
 *  +--------------+ -----> +-----------+ ------------> +--------------+
 *  | module limbo |        | [created] |               | [configured] |
 *  +--------------+ <----- +-----------+ <-----------  +--------------+
 *                    fini  A                stop       |
 *                          |                           |
 *                          |                           |
 *                          |   any specific operation: |
 *                          |       encode_*, filter_*, |
 *                          |            multiplex, ... |
 *                          |                           V
 *                          `-------------- +-----------+
 *                                 stop     | [running] |
 *                                          +-----------+
 *
 */

/*
 * Data structure private for each instance.
 * This is an almost-opaque structure.
 *
 * The main purpose of this structure is to let each module (class)
 * to have it's private data, totally opaque to loader and to the
 * client code.
 * This structure also keep some accounting informations useful
 * both for module code and for loader. Those informations are
 * a module id, which identifies uniquely a given module instance
 * in a given timespan, and a string representing the module 'type',
 * a composition of it's class and specific name.
 */
typedef struct tcmoduleinstance_ TCModuleInstance;
struct tcmoduleinstance_ {
    int id; /* instance id; */
    const char *type; /* packed class + name of module */
    uint32_t features; /* subset of enabled features for this instance */

    void *userdata; /* opaque to factory, used by each module */

    void *extradata;
    size_t extradata_size;
    /*
     * extradata:
     * opaque data needed by a module that should'nt be private.
     * Used mainly into encoder->multiplexor communication.
     * NOTE:
     * I'don't see a better way to handle extradata (see encode_ffmpeg
     * module) in current architecture. I don't want to stack modules
     * (if encoder drives multiplexor can handle itself the extradata)
     * nor add more operations and/or accessors. This way looks as the
     * cleanest and cheapest to me. Suggestions welcome. -- FRomani.
     */
    // FIXME: add status to enforce correct operation sequence?
};

/* can be shared between _all_ instances */
typedef struct tcmoduleclass_ TCModuleClass;
struct tcmoduleclass_ {
    uint32_t version;

    int id; /* opaque internal handle */

    const TCModuleInfo *info;

    /* mandatory operations: */
    int (*init)(TCModuleInstance *self, uint32_t features);
    int (*fini)(TCModuleInstance *self);
    int (*configure)(TCModuleInstance *self, const char *options, vob_t *vob);
    int (*stop)(TCModuleInstance *self);
    int (*inspect)(TCModuleInstance *self, const char *param, const char **value);

    /*
     * not-mandatory operations, a module doing something useful implements
     * at least one of following.
     */
    int (*encode_audio)(TCModuleInstance *self,
                        aframe_list_t *inframe, aframe_list_t *outframe);
    int (*encode_video)(TCModuleInstance *self,
                        vframe_list_t *inframe, vframe_list_t *outframe);
    int (*decode_audio)(TCModuleInstance *self,
                        aframe_list_t *inframe, aframe_list_t *outframe);
    int (*decode_video)(TCModuleInstance *self,
                        vframe_list_t *inframe, vframe_list_t *outframe);
    int (*filter_audio)(TCModuleInstance *self, aframe_list_t *frame);
    int (*filter_video)(TCModuleInstance *self, vframe_list_t *frame);
    int (*multiplex)(TCModuleInstance *self,
                     vframe_list_t *vframe, aframe_list_t *aframe);
    int (*demultiplex)(TCModuleInstance *self,
                       vframe_list_t *vframe, aframe_list_t *aframe);
};

/**************************************************************************
 * TCModuleClass operations documentation:                                *
 **************************************************************************
 *
 * init:
 *      initialize a module, acquiring all needed resources.
 *      A module must also be configure()d before to be used.
 *      An initialized, but unconfigured, module CAN'T DELIVER
 *      a proper result when a specific operation (encode, demultiplex)
 *      is requested. Request an operation in a initialized but unconfigured
 *      module will result in an undefined behaviour.
 * Parameters:
 *          self: pointer to module instance to initialize.
 *      features: select feature of this module to initialize.
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Postconditions:
 *      Given module is ready to be configured.
 *
 *
 * fini:
 *      finalize an initialized module, releasing all acquired resources.
 *      A finalized module MUST be re-initialized before any new usage.
 * Parameters:
 *      self: pointer to module instance to finalize.
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Preconditions:
 *      module was already initialized. To finalize a uninitialized module
 *      will cause an undefined behaviour.
 *      An unconfigured module can be finalized safely.
 * Postconditions:
 *      all resources acquired by given module are released.
 *
 *
 * configure:
 *      setup a module using module specific options and required data
 *      (via `vob' structure). It is requested to configure a module
 *      before to be used safely to perform any specific operation.
 *      Trying to configure a non-initialized module will cause an
 *      undefined behaviour.
 * Parameters:
 *      self: pointer to module instance to configure.
 *      options: string contaning module options.
 *               Syntax is fixed (see optstr),
 *               semantic is module-dependent.
 *      vob: pointer to vob structure.
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Preconditions:
 *      Given module was already initialized AND stopped.
 *      A module MUST be stop()ped before to be configured again, otherwise
 *      an undefined behaviour will occur (expect at least resource leaks).
 * Postconditions:
 *      Given module is ready to perform any supported operation.
 *
 *
 * stop:
 *      reset a module and prepare for reconfiguration or finalization.
 *      This means to flush buffers, close open files and so on,
 *      but NOT release the reseource needed by a module to work.
 *      Please note that this operation can do actions similar, but
 *      not equal, to `fini'. Also note that `stop' can be invoked
 *      zero or multiple times during the module lifetime, but
 *      `fini' WILL be invkoed one and only one time.
 * Parameters:
 *      self: pointer to module instance to stop.
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Preconditions:
 *      Given module was already initialized. Try to (re)stop
 *      an unitialized module will cause an undefined behaviour.
 *      It's safe to stop an unconfigured module.
 * Postconditions:
 *      Given module is ready to be reconfigured safely.
 *
 *
 * inspect:
 *      expose the current value of an a tunable option in a module,
 *      represented as a string.
 *      Every module MUST support two special options:
 *      'all': will return a packed, human-readable representation
 *             of ALL tunable parameters in a given module, or an
 *             empty string if module hasn't any tunable option.
 *             This string must be in the same form accepted by
 *             `configure' operation.
 *      'help': will return a formatted, human-readable string
 *              with module overview, tunable options and explanation.
 * Parameters:
 *      self: pointer to module instance to inspect.
 *      param: name of parameter to inspect
 *      value: when method succesfully returns, will point to a constant
 *             string (that MUST NOT be *free()d by calling code)
 *             containing the actual value of requested parameter.
 *             PLEASE NOTE that this value CAN change between
 *             invocations of this method.
 * Return value:
 *      0  succesfull. That means BOTH the request was honoured OR
 *         the requested parameter isn't known and was silently ignored.
 *      -1 INTERNAL error, reason will be tc_log*()'d out.
 * Preconditions:
 *      module was already initialized.
 *      Inspecting a uninitialized module will cause an
 *      undefined behaviour.
 *
 *
 * decode_{audio,video}:
 * encode_{audio,video}:
 *      decode or encode a given audio/video frame, storing
 *      (de)compressed data into another frame.
 *      Specific module loaded implements various codecs.
 * Parameters:
 *      self: pointer to module instance to use.
 *      inframe: pointer to {audio,video} frame data to decode/encode.
 *      outframe: pointer to {audio,videp} frame which will hold
 *                (un)compressed data. Must be != NULL
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for decoding/encoding will cause an undefined behaviour.
 *      outframe != NULL.
 *
 * SPECIAL NOTE FOR encode_audio operation:
 * if a NULL input frame pointer is given, but a VALID (not NULL)
 * output frame pointer is given as well, a flush operation will performed.
 * This means that encoder will emit all buffered audio data, in order
 * to complete an audio frame and avoid data truncation/loss in output.
 *
 *
 * filter_{audio,video}:
 *      apply an in-place transformation to a given audio/video frame.
 *      Specific module loaded determines the action performend on
 *      given frame.
 * Parameters:
 *      self: pointer to module instance to use.
 *      frame: pointer to {audio,video} frame data to elaborate.
 * Return Value:
 *      0  succesfull.
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*()
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for filter will cause an undefined behaviour.
 *
 *
 * multiplex:
 *      merge given encoded frames in output stream.
 * Parameters:
 *      self: pointer to module instance to use.
 *      vframe: pointer to video frame to multiplex.
 *              if NULL, don't multiplex video for this invokation.
 *      aframe: pointer to audio frame to multiplex
 *              if NULL, don't multiplex audio for this invokation.
 * Return value:
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*().
 *      >0 number of bytes writed for multiplexed frame(s). Can be
 *         (and usually is) different from the plain sum of sizes of
 *         encoded frames.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for multiplex will cause an undefined behaviour.
 *
 *
 * demultiplex:
 *      extract given encoded frames from input stream.
 * Parameters:
 *      self: pointer to module instance to use.
 *      vframe: if not NULL, extract next video frame from input stream
 *              and store it here.
 *      aframe: if not NULL, extract next audio frame from input strema
 *              and store it here.
 * Return value:
 *      -1 error occurred. A proper message should be sent to user using
 *         tc_log*().
 *      >0 number of bytes readed for demultiplexed frame(s). Can be
 *         (and usually is) different from the plain sum of sizes of
 *         encoded frames.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for demultiplex will cause an undefined behaviour.
 *
 */

#endif /* TCMODULE_DATA_H */
