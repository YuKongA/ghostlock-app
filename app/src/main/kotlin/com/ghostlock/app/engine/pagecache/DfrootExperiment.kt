package com.ghostlock.app.engine.pagecache

/** 实验开关：默认全关，生产路径不读 /data/local/tmp marker。 */
object DfrootExperiment {
    const val ENABLED = true
    const val STATIC_HOOK = false
    const val USE_INIT_MODULE = false
    const val USE_EXIT_PAYLOAD = false
    const val BYPASS_TID = false
    const val NO_TRIGGER = false
    const val PAYLOAD_PROBE = false
    const val RESTORE_LEGACY = false
}
