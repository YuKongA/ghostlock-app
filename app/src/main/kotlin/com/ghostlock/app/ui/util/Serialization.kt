package com.ghostlock.app.ui.util

import android.os.Parcel
import android.os.Parcelable
import kotlinx.serialization.KSerializer
import kotlinx.serialization.builtins.ByteArraySerializer
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import com.ghostlock.app.ui.screen.scriptrun.ScriptRunArg
import com.ghostlock.app.ui.viewmodel.TemplateViewModel

/* 刷机/模块仓库路由已随裁剪移除，对应的 FlashItSerializer / RepoModuleArgSerializer 一并删除
 * （2026-10-18 恢复移植）。 */
object ScriptRunArgSerializer : BaseParcelableSerializer<ScriptRunArg>(ScriptRunArg::class.java)
object TemplateInfoSerializer : BaseParcelableSerializer<TemplateViewModel.TemplateInfo>(TemplateViewModel.TemplateInfo::class.java)

open class BaseParcelableSerializer<T : Parcelable>(
    private val clazz: Class<T>
) : KSerializer<T> {

    private val delegate = ByteArraySerializer()
    override val descriptor = delegate.descriptor

    private val creator: Parcelable.Creator<T> by lazy {
        @Suppress("UNCHECKED_CAST")
        clazz.getField("CREATOR").get(null) as Parcelable.Creator<T>
    }

    override fun serialize(encoder: Encoder, value: T) {
        val parcel = Parcel.obtain()
        try {
            value.writeToParcel(parcel, 0)
            val bytes = parcel.marshall()
            encoder.encodeSerializableValue(delegate, bytes)
        } finally {
            parcel.recycle()
        }
    }

    override fun deserialize(decoder: Decoder): T {
        val bytes = decoder.decodeSerializableValue(delegate)
        val parcel = Parcel.obtain()
        try {
            parcel.unmarshall(bytes, 0, bytes.size)
            parcel.setDataPosition(0)
            return creator.createFromParcel(parcel)
        } finally {
            parcel.recycle()
        }
    }
}
