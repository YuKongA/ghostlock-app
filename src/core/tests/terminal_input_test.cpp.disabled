/* Contract test for neutral terminal input (ADR-0004 D2/D4): RootProgram is
 * host-safe with bounded argv; RootedChild and UmhForwardInput are TerminalInput. */

#include "terminal/rooted_child.hpp"
#include "terminal/terminal_input.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using ghostlock::terminal::RootedChild;
using ghostlock::terminal::RootProgram;
using ghostlock::terminal::RootProgramKind;
using ghostlock::terminal::TerminalInput;
using ghostlock::terminal::UmhForwardInput;

static_assert(std::is_trivially_copyable_v<RootProgram>);
static_assert(std::is_standard_layout_v<RootProgram>);
static_assert(std::is_base_of_v<TerminalInput, RootedChild>);
static_assert(std::is_base_of_v<TerminalInput, UmhForwardInput>);

int32_t main(void) {
    RootedChild child{};
    assert(child.root_program.kind == RootProgramKind::KernelSU);
    child.root_program.kind = RootProgramKind::FolkPatch;
    child.root_program.set_argv("/data/adb/folkpatch --boot");
    assert(child.root_program.argv_view() == "/data/adb/folkpatch --boot");

    /* Bounded copy truncates and stays NUL-terminated. */
    RootProgram big{};
    const std::string long_argv(RootProgram::kArgvCapacity * 2, 'x');
    big.set_argv(long_argv);
    assert(big.argv_view().size() == RootProgram::kArgvCapacity - 1);

    UmhForwardInput umh{};
    umh.root_program.kind = RootProgramKind::Custom;
    assert(umh.root_program.argv_view().empty());

    puts("terminal_input_test: ok");
    return 0;
}
