package com.ghostlock.app.data.privilege

import android.content.Context
import android.content.pm.PackageManager
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ExecutorSelectionResult
import com.ghostlock.app.domain.privilege.PrivilegePort
import com.ghostlock.app.domain.privilege.PrivilegeState
import com.ghostlock.app.domain.settings.AppSelectionPort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import rikka.shizuku.Shizuku

/**
 * Android persistence and Shizuku adapter behind the privilege domain boundary.
 *
 * @param context Application-capable context used to read the existing preference and application uid.
 * @note Binder, permission, and preference listeners live for the application process; UI owns none of them.
 */
class AndroidPrivilegeAdapter(
    context: Context,
    private val selectionPort: AppSelectionPort,
) : PrivilegePort {
    private val applicationContext = context.applicationContext
    private val mutableState = MutableStateFlow(readState())
    private val binderReceivedListener = Shizuku.OnBinderReceivedListener(::refresh)
    private val binderDeadListener = Shizuku.OnBinderDeadListener(::refresh)
    private val permissionListener = Shizuku.OnRequestPermissionResultListener { _, _ -> refresh() }

    override val state: StateFlow<PrivilegeState> = mutableState.asStateFlow()

    init {
        runCatching { Shizuku.addBinderReceivedListenerSticky(binderReceivedListener) }
        runCatching { Shizuku.addBinderDeadListener(binderDeadListener) }
        runCatching { Shizuku.addRequestPermissionResultListener(permissionListener) }
    }

    /** Publishes one fresh binder/permission snapshot while preserving the persisted selection. */
    override fun refresh() {
        mutableState.value = readState()
    }

    /**
     * Persists App immediately; Shizuku is persisted only after permission is already granted.
     *
     * @param kind Requested executor family.
     * @return Selection result preserving the historical two-tap permission flow.
     */
    override suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult = when (kind) {
        AttackExecutorKind.APP -> {
            if (persist(AttackExecutorKind.APP)) {
                ExecutorSelectionResult.SELECTED
            } else {
                ExecutorSelectionResult.PERSISTENCE_FAILED
            }
        }

        AttackExecutorKind.SHIZUKU -> {
            val current = readState()
            when {
                current.shizukuGranted -> {
                    if (persist(AttackExecutorKind.SHIZUKU)) {
                        ExecutorSelectionResult.SELECTED
                    } else {
                        ExecutorSelectionResult.PERSISTENCE_FAILED
                    }
                }

                !current.shizukuAvailable || runCatching { Shizuku.isPreV11() }.getOrDefault(true) ->
                    ExecutorSelectionResult.UNAVAILABLE

                runCatching { Shizuku.requestPermission(REQUEST_CODE) }.isSuccess -> {
                    refresh()
                    ExecutorSelectionResult.PERMISSION_REQUESTED
                }

                else -> ExecutorSelectionResult.UNAVAILABLE
            }
        }
    }

    /** @return Current persisted choice plus live Shizuku facts. */
    private fun readState(): PrivilegeState {
        val available = runCatching { Shizuku.pingBinder() }.getOrDefault(false)
        val granted = available && runCatching {
            Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)
        return PrivilegeState(
            selectedExecutor = selectionPort.selection.value.selectedExecutor,
            appUid = applicationContext.applicationInfo.uid,
            shizukuAvailable = available,
            shizukuGranted = granted,
        )
    }

    /** Persists one stable string and publishes the resulting snapshot immediately. */
    private suspend fun persist(kind: AttackExecutorKind): Boolean = runCatching {
        selectionPort.submit { current -> current.copy(selectedExecutor = kind) }.await()
        refresh()
    }.isSuccess

    private companion object {
        const val REQUEST_CODE = 0
    }
}
