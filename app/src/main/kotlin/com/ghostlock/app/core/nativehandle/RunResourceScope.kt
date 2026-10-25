package com.ghostlock.app.core.nativehandle

import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlin.coroutines.AbstractCoroutineContextElement
import kotlin.coroutines.CoroutineContext

/** 不可由公开 API 构造的 run 身份；owned factory 必须显式接收它。 */
class RunScopeToken private constructor(
    internal val scopeId: Long,
    internal val cookie: Long,
) : AbstractCoroutineContextElement(Key) {
    companion object Key : CoroutineContext.Key<RunScopeToken> {
        internal fun create(scopeId: Long, cookie: Long): RunScopeToken =
            RunScopeToken(scopeId, cookie)
    }
}

/**
 * 一次 Flow/Lua run 的 Native owner 账本。
 *
 * 资源按登记逆序关闭。scope 关闭后不再接受新资源；quarantine 资源只从账本移除，
 * 不触发普通 Native destroy。
 */
class RunResourceScope : AutoCloseable {
    private val closed = AtomicBoolean(false)
    private val resources = ArrayList<OwnedNativeHandle>()
    val token = RunScopeToken.create(
        scopeId = nextIdentity.getAndIncrement(),
        cookie = nextIdentity.getAndIncrement(),
    )

    internal fun register(resource: OwnedNativeHandle) = synchronized(resources) {
        check(!closed.get()) { "run resource scope is closed" }
        resources += resource
    }

    internal fun unregister(resource: OwnedNativeHandle) = synchronized(resources) {
        resources.remove(resource)
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        val snapshot = synchronized(resources) {
            resources.asReversed().toList().also { resources.clear() }
        }
        var firstFailure: Throwable? = null
        snapshot.forEach { resource ->
            try {
                resource.closeFromScope()
            } catch (problem: Throwable) {
                if (firstFailure == null) firstFailure = problem
            }
        }
        firstFailure?.let { throw it }
    }

    companion object {
        private val nextIdentity = AtomicLong(1L)
    }
}
