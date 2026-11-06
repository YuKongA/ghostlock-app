package com.ghostlock.app

import android.app.Application
import android.content.Context
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.domain.repository.GhostlockRepository

/** Application composition root. It is the only place that binds data implementations to domain ports. */
class GhostlockApplication : Application() {
    fun createRepository(context: Context = this): GhostlockRepository =
        AndroidGhostlockRepository(context)
}
