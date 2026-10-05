#ifndef GHOSTLOCK_PLATFORM_VIVO_MACROS_H
#define GHOSTLOCK_PLATFORM_VIVO_MACROS_H

/* Build-time overridable VR.ko tag-B offset, consumed by the
 * platform::vivo per-task tag behavior (VrTaskTagPolicy). It stays a define
 * (not a constexpr) so a device with a different vr.ko layout can override it
 * with -DVR_TAG_B_OFF=...; the default matches the verified vivo 6.1 tree. Tag
 * A rides the zeroed thread_info.flags word, so it needs no separate offset. */
#ifndef VR_TAG_B_OFF
#define VR_TAG_B_OFF 0x2c
#endif

#endif
