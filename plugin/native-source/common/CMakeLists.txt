include_guard(GLOBAL)

# Source-only native common authority. Concrete plugin packages and host contract tests compile
# these objects directly; this directory never produces a shared/static runtime library.
add_library(gl_common OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/common/gl_log.c
    ${CMAKE_CURRENT_LIST_DIR}/common/run_handoff.c
)
target_include_directories(gl_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(gl_common PUBLIC log)
