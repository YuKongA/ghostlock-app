package com.ghostlock.app.boot

import android.content.Context
import android.widget.Toast
import com.ghostlock.app.R

object BootAutoRootFeedback {
    fun showSuccessToast(context: Context) {
        Toast.makeText(
            context.applicationContext,
            context.getString(R.string.boot_run_success_toast),
            Toast.LENGTH_LONG,
        ).show()
    }
}
