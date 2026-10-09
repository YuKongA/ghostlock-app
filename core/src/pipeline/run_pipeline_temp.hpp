#pragma once
/** @file pipeline/run_pipeline_temp.hpp
 *  The temporary entry that drives one whole path on a device. TEMPORARY FILE.
 *
 *  Why it exists: the composition that will normally drive a path -- fed by the script layer and the
 *  plan it compiles -- does not exist yet, and the goal asks that the device be exercised through any
 *  temporary means. This is that means. It is registered for removal in
 *  docs/analysis/device-probe-43284-checklist.md, together with the condition: once the device result
 *  is archived, this file and its call in the entry are deleted and a grep for the temporary name
 *  returns nothing.
 *
 *  It adds no mechanism. Every step below is a call into a piece that already existed, in the order
 *  their own comments prescribe: the document is read and admitted, the frame supplies the key
 *  material, the doors open the target, the window is a page cache write, the step set is walked by
 *  the runner, and the terminal finishes. What it decides is only the order and the reporting.
 *
 *  It reports by name at every step and prints no key material.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::pipeline {
    /**
     *  Drive one page cache write, from the document on stdin to whatever the device answers.
     *
     *  @return An exit code: 0 when the whole path completed, 3 when there was no session frame (not
     *          this backend's run), and named failures above that for the step that refused.
     *  @note   The document must already have been read into @p document_bytes by the entry, which
     *          is where it was admitted; the frame follows it on the same stream.
     */
    [[nodiscard]] int run_pagecache_path_temp(const std::uint8_t *document_bytes,
                                              std::size_t document_length) noexcept;
}  // namespace ghostlock::pipeline
