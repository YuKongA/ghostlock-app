package com.ghostlock.app.data.profile

/**
 * 设备画像：按内核 release 选中并解析**这一台机器**的那份 GLK profile。
 *
 * 选择的规则只有两条，且都是确定的：
 *  1. 资产目录里文件名等于 release 的那份（例如 5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf）；
 *  2. 没有精确档时退到 major.minor-androidNN-general.conf 通用档。
 * 两条都不命中就**具名失败**——绝不拿另一台机器的偏移跑这台机器。
 *
 * 它只回答"这份画像里写了什么"，不做任何判断（例如"这个 backend 能不能用"由步骤引擎决定）。
 */
/** 资产读取面：真机是 AssetManager，单测是内存表或文件。 */
fun interface AssetSource {
    /** @return 该资产的文本；不存在则 null。 */
    fun text(name: String): String?
}

/** @return 资产目录里的全部文件名（用于选档；真机由 AssetManager 列出）。 */
fun interface AssetLister {
    fun names(): List<String>
}

class DeviceProfile private constructor(
    /** 命中的资产文件名（证据里要写清用的是哪一份）。 */
    val assetName: String,
    /** 该档声明的 release。 */
    val release: String,
    /** 是否是通用档（精确档缺失时的退路，证据里同样要写清）。 */
    val generic: Boolean,
    private val config: HoconConfig,
) {
    /** available.<backend>.queue[].step 的名字（顺序即声明顺序）。 */
    fun declaredStepNames(backend: String): List<String> {
        val queue = config.arr("ghostlock.available." + backend + ".queue") ?: return emptyList()
        val names = mutableListOf<String>()
        for (item in queue.items) {
            val step = (item as? HoconValue.Obj)?.entries?.get("step") as? HoconValue.Str ?: continue
            names += step.text
        }
        return names
    }

    /** available.<backend>.route（路由名），缺失则 null。 */
    fun route(backend: String): String? = config.string("ghostlock.available." + backend + ".route")

    /** 该 backend 在 available 里声明了没有。 */
    fun declares(backend: String): Boolean = config.has("ghostlock.available." + backend)

    /** backend.<backend>.abi.offset.<name>：内核镜像偏移。 */
    fun imageOffset(backend: String, name: String): Long? =
        config.long("ghostlock.backend." + backend + ".abi.offset." + name)

    /** backend.<backend>.abi.task_struct.<name>：结构体字段偏移。 */
    fun structOffset(backend: String, name: String): Long? =
        config.long("ghostlock.backend." + backend + ".abi.task_struct." + name)

    /** 任意点路径（供证据与后续批次取用）。 */
    fun long(path: String): Long? = config.long(path)

    fun string(path: String): String? = config.string(path)

    /** 该画像里的 schema 版本（唯一在用的是 3）。 */
    fun schemaVersion(): Long? = config.long("ghostlock.schema_version")

    companion object {
        /**
         * 按 release 选档。
         *
         * @return (文件名, 是否通用档)；都不命中则 null。
         */
        fun chooseAsset(release: String, names: List<String>): Pair<String, Boolean>? {
            val exact = release + ".conf"
            if (names.contains(exact)) {
                return exact to false
            }
            /* 通用档：kernel-androidNN-general.conf，例如 5.15-android13-general.conf。 */
            val parts = release.split('-')
            /* 通用档用 major.minor（例如 5.15-android13-general.conf），不是完整内核版本。 */
            val kernel = parts.getOrNull(0)?.split('.')?.take(2)?.joinToString(".") ?: return null
            val android = parts.firstOrNull { it.startsWith("android") } ?: return null
            val generic = kernel + "-" + android + "-general.conf"
            return if (names.contains(generic)) generic to true else null
        }

        /**
         * 装载。
         *
         * @return 成功时给出画像；失败时给出**具名理由**（没有档 / 读不到 / 解析失败 / schema 不是 3）。
         */
        fun load(
            release: String,
            source: AssetSource,
            lister: AssetLister,
        ): Result<DeviceProfile> {
            val chosen = chooseAsset(release, lister.names())
                ?: return Result.failure(HoconException("profile-not-found:" + release))
            val text = source.text(chosen.first)
                ?: return Result.failure(HoconException("profile-unreadable:" + chosen.first))
            val config = try {
                Hocon.parse(text, include = { included -> source.text(included) })
            } catch (bad: HoconException) {
                return Result.failure(bad)
            }
            val declaredRelease = config.string("ghostlock.release") ?: chosen.first.removeSuffix(".conf")
            val schema = config.long("ghostlock.schema_version")
            if (schema != 3L) {
                return Result.failure(HoconException("profile-schema:" + schema))
            }
            return Result.success(
                DeviceProfile(chosen.first, declaredRelease, chosen.second, config),
            )
        }
    }
}
