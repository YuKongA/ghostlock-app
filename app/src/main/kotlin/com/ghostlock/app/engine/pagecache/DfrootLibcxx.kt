package com.ghostlock.app.engine.pagecache

import java.util.Base64

/** DFRoot 的 libcxx.S 二进制（468B）；布局与旧 472B blob 不同。 */
object DfrootLibcxx {
    const val SIZE = 468
    const val STAGE_SIZE = 464
    const val ENTRY_OFFSET = 0x90
    const val FIRST_INST_OFFSET = 0x1C8
    const val JUMP_BACK_OFFSET = 0x1CC
    const val KO_TARGET_OFFSET = 0x88
    const val KO_TARGET_CAPACITY = 64

    private const val B64 = "L2Rldi9kZgB1OnI6dmVuZG9yX21vZHByb2JlOnMwAC9wcm9jL3NlbGYvYXR0ci9leGVjAC92ZW5kb3IvYmluL2luc21vZAAvdmVuZG9yL2xpYjY0L2xpYmJpbmRlcmRlYnVnLnNvAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEcAAAAAAAAA4Ae/qeIPv6nkF7+p5h+/qegnv6n6b7+p/He/qf4PH/jIFYDSAQAA1IAHADVIFoDSAQAA1B8EAHEBBwBUYAyAkoH5/xACGIDSAgGg8uMDH6oIB4DSAQAA1AAG+Df/QwDRSBaA0gEAANTgCwC5iBuA0uQjAJHjAx+q4gMfquEDH6ogAoDSACSg8gEAANTgCwC5/0MAkSAEADX/gwDRIAKA0uEDH6riAx+q4wMfquQDH6qIG4DSAQAA1KACADVgDICSYfb/cCIAgNICAaDyCAeA0gEAANQh9f8Q4gKA0ggIgNIBAADUAfb/EOEDAPlB9v9w4QcA+f8LAPlg9f8Q4QMAkeIDH6qoG4DSAQAA1CAAgNKoC4DSAQAA1P4HQfj8d8Go+m/BqOgnwajmH8Go5BfBqOIPwajgB8Go/Xu+qQEAABTQAQAA"

    fun bytes(): ByteArray = Base64.getDecoder().decode(B64)
}
