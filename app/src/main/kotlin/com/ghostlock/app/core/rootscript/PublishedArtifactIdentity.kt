package com.ghostlock.app.core.rootscript

/**
 * Immutable identity of one closed, published ordinary file.
 *
 * @property device Device number obtained from the same opened file descriptor used for hashing.
 * @property inode Inode number obtained from that descriptor.
 * @property byteCount Bytes read from that descriptor.
 * @property sha256 Lowercase SHA-256 of those bytes.
 * @property mode Full stat mode, including regular-file type bits.
 * @note Physical capture and verification belong to the Android data layer.
 */
data class PublishedArtifactIdentity(
    val device: ULong,
    val inode: ULong,
    val byteCount: Long,
    val sha256: String,
    val mode: UInt,
)
