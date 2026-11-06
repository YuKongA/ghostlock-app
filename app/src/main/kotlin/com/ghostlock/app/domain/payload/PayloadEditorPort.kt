package com.ghostlock.app.domain.payload

/** Payload editor boundary layered on the same canonical repository authority. */
interface PayloadEditorPort : PayloadRepository {
    /** @return Current non-persisted root-manager environment projection. */
    fun rootManagerOptions(): List<PayloadRootManagerOption>
}
