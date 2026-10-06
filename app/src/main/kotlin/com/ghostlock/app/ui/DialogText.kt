package com.ghostlock.app.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource

/**
 * One line of the shared dialog: a resource when the dialog sets one, else the
 * plain text it carries, else NOTHING.
 *
 * A real device crashed here: the plugin parameter dialog left its message id at
 * `0` and [stringResource] threw `Resources$NotFoundException: String resource
 * ID #0x0`. Id 0 is not a resource, so it can never reach [Res]: `of` maps it to
 * "no line" and [Res] refuses it at construction. Both defences are deliberate —
 * the factory alone would still let a future caller build a raw 0.
 */
sealed interface DialogText {
    /** A resource the page resolves; never 0. */
    @JvmInline
    value class Res(@StringRes val resId: Int) : DialogText {
        init {
            require(resId != 0) { "0 is not a string resource; use DialogText.of" }
        }
    }

    /** Plain text the dialog carries ("0" is a perfectly good string here). */
    @JvmInline
    value class Plain(val text: String) : DialogText

    companion object {
        /**
         * The line for one dialog field: the resource when it is a real id, else
         * the plain text, else null (render nothing).
         */
        fun of(@StringRes resId: Int?, plain: String = ""): DialogText? = when {
            resId != null && resId != 0 -> Res(resId)
            plain.isNotEmpty() -> Plain(plain)
            else -> null
        }
    }
}

/** The words of one dialog line; a null line renders as nothing. */
@Composable
fun DialogText.text(): String = when (this) {
    is DialogText.Res -> stringResource(resId)
    is DialogText.Plain -> text
}
