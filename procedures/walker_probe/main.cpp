// glk_walker - minimal trigger probe for the libc++ sentry hook (aarch64 Android).
//
// Purpose
//   Make this process execute the PLATFORM libc++ at /system/lib64/libc++.so and
//   reach std::ostream, so it "steps on" the hook branch that sits at file
//   offset 0xB30AC of that library:
//       0xB30A8: 0xD503245F   bti c   (guard, kept)
//       0xB30AC: 0x14012D83   B +0x4B60C -> file 0xFE6B8 (payload entry)
//   The patched word belongs to
//       _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_
//       (std::__1::basic_ostream<char, char_traits<char>>::sentry::sentry)
//   Every std::ostream operation constructs a sentry internally, so the first
//   std::cout call in this process reaches the patched code.
//
// Why the declarations below instead of <iostream>
//   The device's platform libc++ uses inline namespace std::__1; the NDK's
//   libc++ headers emit std::__ndk1 (NDK ships _LIBCPP_ABI_NAMESPACE __ndk1 in
//   its __config_site, and the NDK libc++_shared.so exports 0 __1 symbols).
//   The two runtimes are deliberately not symbol-compatible, so an ordinary
//   NDK-compiled C++ programme can never bind to /system/lib64/libc++.so.
//   This probe therefore declares the minimal platform-ABI surface by hand and
//   links against a link-time copy of the device's libc++.so (see build.sh).
//   It IS std::cout - the platform's own object - just declared at ABI level.
//
// What this program does
//   One line through std::cout (put/flush on the platform's basic_ostream),
//   bracketed by C stdio diagnostics on stderr. The bracket lines go through
//   bionic libc only - they cannot step on the hook - and they show how far
//   execution got if the process dies.
//
// Build
//   ./build.sh   (needs the device's libc++.so as a link-time input; see README.md)
//
// Run on device (adb shell, user `shell`)
//   adb push <OUT_DIR>/glk_walker /data/local/tmp/glk_walker
//   adb shell 'chmod 755 /data/local/tmp/glk_walker && /data/local/tmp/glk_walker; echo exit=$?'
//
// Expected outcomes
//   exit=139 (SIGSEGV) with only "[walker] begin" on stderr -> stepped and the
//   process died inside the payload (the crash_dump64 behavior).
//   exit=0 with all three lines -> the hooked code was reached but the payload
//   let the process continue (or, if nothing at all printed, it was never
//   reached - check that DT_NEEDED really is libc++.so).

#include <stdio.h>
#include <unistd.h>

// Minimal declarations of the platform libc++ ABI (std::__1, Android 15).
// Only the surface this probe calls; no libc++ headers are included.
namespace std {
inline namespace __1 {

template <class CharT>
struct char_traits;

template <class CharT, class Traits>
class basic_ostream {
  public:
    basic_ostream& put(CharT c);
    basic_ostream& flush();
};

extern basic_ostream<char, char_traits<char> > cout;

class ios_base {
  public:
    class Init {
      public:
        Init();
        ~Init();
    };
};

}  // namespace __1
}  // namespace std

// Same job as <iostream>'s per-translation-unit __ioinit: initialise the
// standard streams through the platform runtime.
static std::ios_base::Init g_stream_init;

int main() {
    fprintf(stderr, "[walker] begin pid=%d\n", static_cast<int>(getpid()));

    /* The trigger: first std::ostream use in this process. */
    const char *line = "[walker] ostream line\n";
    for (const char *p = line; *p != 0; ++p) {
        std::cout.put(*p);
    }
    std::cout.flush();

    fprintf(stderr, "[walker] after ostream\n");
    return 0;
}
