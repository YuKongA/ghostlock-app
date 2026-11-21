package com.ghostlock.app.core.flow

/**
 * Reads the single root-script result bound to one accepted product run.
 *
 * Expected physical and GLR1 failures are returned as an existing [RootScriptDecodeOutcome]
 * and do not escape after the Flow has claimed terminal ownership.
 */
fun interface RootScriptResultReader {
    /** @return Complete, identity-mismatch, or malformed outcome for the bound result. */
    fun read(): RootScriptDecodeOutcome
}
