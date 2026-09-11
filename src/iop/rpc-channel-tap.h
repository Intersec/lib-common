/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

#ifndef IS_LIB_COMMON_IOP_RPC_CHANNEL_TAP_H
#define IS_LIB_COMMON_IOP_RPC_CHANNEL_TAP_H

#include <lib-common/iop-rpc.h>

/* IOP spy tap (debug builds only).
 *
 * The existing IOP dissector only sees TCP traffic, so the local
 * parent<->child IChannels that run over UNIX sockets are invisible to a
 * plain Ethernet/TCP capture. This tap fills that gap: it mirrors the
 * cleartext IOP frames of a channel onto a per-channel, self-connected
 * loopback TCP pair whose two ends this process holds. Locally-sent (TX)
 * frames are written on one end, frames received from the peer (RX) on
 * the other, so a capture on the interface carrying IS_IC_TAP_IP shows both
 * directions and the existing dissector decodes it. Each end drains
 * (reads and discards) whatever the other wrote, so the tap never
 * back-pressures the real IChannel.
 *
 * The mechanism (`ic_tap_*`) is transport-agnostic; only the entry point
 * decides which channels to capture. Today the sole entry point is
 * ic_unix_tap_frame(), which captures UNIX-socket channels only (TCP IOP
 * is already visible to a plain capture). A future ic_<...>_tap_frame()
 * could reuse the same machinery to tap IC in general.
 *
 * Enabled at runtime by the IS_IC_TAP_IP env var (a loopback address; use a
 * distinct one per host so merged captures can be filtered per host).
 * Per-channel taps use ephemeral ports; the open-time log line maps those
 * ports back to the ichannel.
 *
 * The whole feature is compiled out under NDEBUG (release/production).
 */

#ifndef NDEBUG

/** True once the tap is enabled (IS_IC_TAP_IP was set at init). */
extern bool ic_tap_enabled_g;

/** Set up the tap module and read IS_IC_TAP_IP.
 *
 * Meant to be called once from the IChannel module initialization. */
void ic_tap_initialize(void);

/** Close every open tap and tear down the module. */
void ic_tap_shutdown(void);

/** Open a tap for a channel (reached via the ic_tap_open() macro).
 *
 * No-op unless the channel is a non-local UNIX-socket link without a tap
 * yet. TCP IOP is already visible to a plain capture, so it is skipped. */
void ic_unix_tap_open(const ichannel_t *nonnull ic);

/** Close a channel's tap (reached via the ic_tap_close() macro).
 *
 * No-op if the channel has no tap. Called on disconnect. */
void ic_unix_tap_close(const ichannel_t *nonnull ic);

/** Mirror one framed IC message (12-byte header + payload).
 *
 * The actual capture entry point; UNIX-only for now (no-op for non-UNIX,
 * local or disconnected channels). Call it through the ic_tap_frame()
 * macro below, which carries the enable fast-path. Best-effort and
 * non-blocking: it never gates the real IC send/recv path. */
void ic_unix_tap_frame(
    ichannel_t *nonnull ic, bool is_tx, const void *nonnull data, int len
);

/** Open a channel's tap on connect (generic entry).
 *
 * Skips the call entirely when the tap is off, else forwards to the
 * (currently UNIX-only) ic_unix_tap_open(). This is the name call sites
 * use, so the mechanism can later grow other entry points. */
#  define ic_tap_open(ic)                                                    \
      do {                                                                   \
          if (unlikely(ic_tap_enabled_g)) {                                  \
              ic_unix_tap_open((ic));                                        \
          }                                                                  \
      } while (0)

/** Close a channel's tap on disconnect (generic entry).
 *
 * Skips the call entirely when the tap is off, else forwards to the
 * (currently UNIX-only) ic_unix_tap_close(). This is the name call sites
 * use, so the mechanism can later grow other entry points. */
#  define ic_tap_close(ic)                                                   \
      do {                                                                   \
          if (unlikely(ic_tap_enabled_g)) {                                  \
              ic_unix_tap_close((ic));                                       \
          }                                                                  \
      } while (0)

/** Generic hot-path capture entry.
 *
 * Skips the call entirely when the tap is off, else forwards to the
 * (currently UNIX-only) capture function. This is the name call sites
 * use, so the mechanism can later grow other entry points. */
#  define ic_tap_frame(ic, is_tx, data, len)                                 \
      do {                                                                   \
          if (unlikely(ic_tap_enabled_g)) {                                  \
              ic_unix_tap_frame((ic), (is_tx), (data), (len));               \
          }                                                                  \
      } while (0)

#else /* NDEBUG: the tap is compiled out of release builds. */

#  define ic_tap_initialize() ((void)0)
#  define ic_tap_shutdown() ((void)0)
#  define ic_tap_open(ic) ((void)0)
#  define ic_tap_close(ic) ((void)0)
#  define ic_tap_frame(ic, is_tx, data, len) ((void)0)

#endif /* NDEBUG */

#endif /* IS_LIB_COMMON_IOP_RPC_CHANNEL_TAP_H */
