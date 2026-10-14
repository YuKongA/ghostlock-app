#pragma once
/** @file jni/session_frame.hpp
 *  PROPOSAL, NOT APPROVED. This file records a possible frame layout so the choice can be
 *  argued over concrete bytes. Nothing may parse or generate it until the layout is accepted;
 *  a new wire contract is design-level work and is not settled by writing it down.
 *
 *  The frame Kotlin sends: one byte array, a fixed header and a fixed body.
 *
 *  Why a frame at all: the parameter face of the six core functions is close to a hundred
 *  scalars and one table. Two jlongs cannot carry it, and a long argument list would move the
 *  knowledge of ordering into two places at once. A frame keeps it in one place -- this file --
 *  and lets the Kotlin side fill a buffer without knowing which call reads which field.
 *
 *  Layout: 16-byte header, then the body in the order below. All integers are little-endian
 *  and fixed width, so the two sides agree without relying on either language layout rules.
 *
 *    offset  size  field
 *    0       4     magic 0x474C4B46 ("GLKF")
 *    4       2     version, currently 1
 *    6       2     body_size in bytes
 *    8       4     route (0 pselect, 1 tcp)
 *    12      4     flags: bit0 compact_waiter
 *
 *  Body (u64 unless noted):
 *    0    page_base        8    fake_lock        16   fake_fops       24   fake_w0
 *    32   fake_task        40   fake_right       48   cred_copy_off   56   tcp_cred_copy_off
 *    64   w1_page_offset   72   slide_init_task  80   slide_root_task_group
 *    88   fake_left        96   fake_parent      104  pselect_custom_target
 *    112  fake_parent_delta                            112 bytes of addresses so far
 *    then u64: write_target
 *    then u32: write_mode, write_leaf, waiter_shift, cfi_route_attempts, compact_timeout_sec,
 *              compact_timeout_usec, route_nfds_plus_16, open_slack_32, select_timeout_sec,
 *              select_timeout_usec, consumer_drain_iterations, consumer_drain_usec, waiter_prio,
 *              delay_override_usec
 *    then the write description as a length-prefixed ASCII string
 *    then the delay table, eight u32 in the original order
 *
 *    then the two direct-map bounds, begin and end, each a u64
 *
 *  The bounds are the only addresses that are not part of the spray or the route: they come
 *  from the caller, and do_one_write refuses any target outside them before anything else.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::jni {
    /** The frame the Kotlin side fills and the boundary reads. */
    struct SessionFrame final {
        /** The four bytes that say this is one of ours. */
        static constexpr std::uint32_t kMagic = 0x474C4B46U;
        /** The only version this file knows how to read. */
        static constexpr std::uint16_t kVersion = 1U;
        /** Header bytes before the body. */
        static constexpr std::size_t kHeaderSize = 16U;
        /** Set in the flags word when the compact_waiter profile is selected. */
        static constexpr std::uint32_t kFlagCompactWaiter = 1U;
        /** How many entries the delay table carries. */
        static constexpr std::size_t kDelayEntries = 8U;
    };

    /** What reading a frame can go wrong with, named so a caller can act on it. */
    enum class FrameError : std::int32_t {
        /** The buffer was shorter than the header. */
        TooShort = 1,
        /** The magic did not match. */
        BadMagic = 2,
        /** The version is not the one this build reads. */
        BadVersion = 3,
        /** The body size in the header disagrees with the buffer. */
        BadBodySize = 4,
        /** The description string is not length-prefixed ASCII inside the body. */
        BadDescription = 5,
        /** The frame is well formed but a required value is zero. */
        MissingValue = 6,
    };
}  // namespace ghostlock::jni
