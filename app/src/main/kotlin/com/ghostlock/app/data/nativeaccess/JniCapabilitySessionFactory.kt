package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.access.AccessSession
import com.ghostlock.app.core.access.PageCacheTarget

/** 打开 43284 能力会话；目标文件由调用方给出，不从内核版本推断。 */
class JniCapabilitySessionFactory(
    private val target: PageCacheTarget,
    private val initParams: PageCacheInit43284? = null,
    private val nativeFactory: JniAccessSessionFactory = JniAccessSessionFactory(),
) {
    fun open(backendId: String = "cve_2026_43284"): AccessResult<AccessSession> {
        val native = nativeFactory.open(com.ghostlock.app.core.AccessSessionRequest(backendId, ByteArray(0)))
        return when (native) {
            is AccessResult.Failure -> native
            is AccessResult.Success -> {
                val session = native.value as? JniAccessSession
                    ?: return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "unexpected session type")
                AccessResult.Success(JniCapabilitySession(session, target, initParams))
            }
        }
    }
}
