include_guard(GLOBAL)

# Purpose: shared native logging infrastructure, compiled once into relocatable objects
# (.o) through an OBJECT library; every consuming DSO/executable links those objects in
# without a second copy of the code.
# Inputs: gl_log.c plus gl_log.h in this directory.
# Output: OBJECT library target `gl_common` (target name unchanged); build artefacts are
# objects such as `gl_common.dir/gl_log.c.o`, not a static/shared library file.
# Failure: compile errors fail the variant build.
# Preconditions: included by the top-level CMakeLists before any backend chunk.
# Phase: application native build.

add_library(gl_common OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/gl_log.c
)
# Keep the parent cpp/ root as the public include root so existing
# `#include "common/gl_log.h"` spellings in the backend chunks keep resolving.
target_include_directories(gl_common PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
)
target_link_libraries(gl_common PUBLIC log)
