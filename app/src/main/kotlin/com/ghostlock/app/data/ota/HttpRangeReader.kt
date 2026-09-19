package com.ghostlock.app.data.ota

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * Reads byte ranges of a remote file over HTTP Range requests using standard HttpURLConnection.
 */
class HttpRangeReader(
    private val connectTimeoutMs: Int = 15000,
    private val readTimeoutMs: Int = 30000,
    private val userAgent: String = "Dalvik/2.1.0 (Linux; W; Android 17; Android Build/CP2A.260605.016)"
) {

    private fun openConnectionWithRedirects(
        urlStr: String, headers: Map<String, String>
    ): HttpURLConnection {
        var currentUrl = urlStr
        var redirects = 0
        while (redirects < 6) {
            val url = URL(currentUrl)
            val conn = url.openConnection() as HttpURLConnection
            conn.connectTimeout = connectTimeoutMs
            conn.readTimeout = readTimeoutMs
            conn.instanceFollowRedirects = true
            conn.setRequestProperty("User-Agent", userAgent)
            for ((k, v) in headers) {
                conn.setRequestProperty(k, v)
            }
            conn.connect()
            val code = conn.responseCode
            if (code in 301..303 || code in 307..308) {
                val location = conn.getHeaderField("Location")
                conn.disconnect()
                if (!location.isNullOrBlank()) {
                    currentUrl = if (location.startsWith(
                            "http://", ignoreCase = true
                        ) || location.startsWith("https://", ignoreCase = true)
                    ) {
                        location
                    } else {
                        URL(url, location).toString()
                    }
                    redirects++
                    continue
                }
            }
            return conn
        }
        throw IOException("Too many redirects: $urlStr")
    }

    suspend fun fileLength(url: String): Long? = withContext(Dispatchers.IO) {
        return@withContext try {
            val conn = openConnectionWithRedirects(url, mapOf("Range" to "bytes=0-0"))
            try {
                val contentRange = conn.getHeaderField("Content-Range")
                if (contentRange != null) {
                    val slash = contentRange.lastIndexOf('/')
                    if (slash != -1) {
                        contentRange.substring(slash + 1).trim().toLongOrNull()?.let { if (it > 0) return@withContext it }
                    }
                }
                val length = conn.contentLengthLong
                if (length > 0) return@withContext length
                null
            } finally {
                conn.disconnect()
            }
        } catch (e: CancellationException) {
            throw e
        } catch (_: Exception) {
            null
        }
    }

    suspend fun read(url: String, start: Long, size: Int): ByteArray? = withContext(Dispatchers.IO) {
        if (size == 0) return@withContext ByteArray(0)
        if (size < 0 || start < 0) return@withContext null

        return@withContext try {
            val end = start + size - 1
            val conn = openConnectionWithRedirects(url, mapOf("Range" to "bytes=$start-$end"))
            try {
                val code = conn.responseCode
                if (code == 200 && start > 0) {
                    // Server ignored the Range header and sent from offset 0
                    return@withContext null
                }
                if (code !in 200..299) {
                    return@withContext null
                }
                val stream = conn.inputStream
                val buffer = ByteArray(size)
                var totalRead = 0
                while (totalRead < size) {
                    val count = stream.read(buffer, totalRead, size - totalRead)
                    if (count == -1) break
                    totalRead += count
                }
                if (totalRead == size) buffer else buffer.copyOf(totalRead)
            } finally {
                conn.disconnect()
            }
        } catch (e: CancellationException) {
            throw e
        } catch (_: Exception) {
            null
        }
    }
}
