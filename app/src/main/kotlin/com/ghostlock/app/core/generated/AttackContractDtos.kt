@file:Suppress("LongParameterList")

package com.ghostlock.app.core.generated

/**
 *  JNI 参数对象（**手写维护**，2026-10-17 用户裁定：不再由构建生成）。
 *
 *  与 core/src/jni/attack_contract_fields.hpp 一一对应：改字段时两边一起改。
 *  顺序即 JNI/native reader 的读取顺序，不要重排。
 */
data class AttackContractField(
    val name: String,
    val kind: String,
    val fixedBytes: Int,
    val allocationMaxBytes: Int,
)

/** JNI 参数对象：字段与 native 的 Init43499 读取顺序一致。 */
class Init43499(
    @JvmField val routeId: Long,
    @JvmField val mainCpu: Int,
    @JvmField val consumerCpu: Int,
    @JvmField val waiterOffset: Long,
    @JvmField val taskOffset: Long,
    @JvmField val lockOffset: Long,
    @JvmField val bufferSize: Long,
    @JvmField val maxAttempts: Long,
    @JvmField val armSequence: Long,
    @JvmField val armHold: Long,
    @JvmField val parkMs: Long,
    @JvmField val settleUs: Long,
    @JvmField val postRequeueSettleUs: Long,
    @JvmField val postAdjustSettleUs: Long,
    @JvmField val consumerDelayUs: Long,
    @JvmField val fallbackLockUs: Long,
    @JvmField val burstCalls: Long,
    @JvmField val maxCalls: Long,
    @JvmField val niceModulus: Int,
    @JvmField val policy: Int,
    @JvmField val multicastPresent: Boolean,
    @JvmField val armRounds: Long,
    @JvmField val stampWaiterShift: Long,
    @JvmField val tcpPresent: Boolean,
    @JvmField val zeroCopyMapBytes: Long,
    @JvmField val selectPresent: Boolean,
    @JvmField val waiterShift: Long,
    @JvmField val fdCount: Long,
    @JvmField val perfSlots: Long,
    @JvmField val perfType0: Long,
    @JvmField val perfConfig0: Long,
    @JvmField val perfType1: Long,
    @JvmField val perfConfig1: Long,
    @JvmField val perfType2: Long,
    @JvmField val perfConfig2: Long,
    @JvmField val perfType3: Long,
    @JvmField val perfConfig3: Long,
    @JvmField val ringPages: Long,
    @JvmField val ringWakeup: Long,
    @JvmField val spraySize: Long,
    @JvmField val sprayCount: Long,
    @JvmField val pumpIters: Long,
    @JvmField val routeSelected: Long,
    @JvmField val fakeTask: Long,
    @JvmField val fakeLock: Long
)

/** JNI 参数对象：字段与 native 的 KernelLayout43499 读取顺序一致。 */
class KernelLayout43499(
    @JvmField val lock: Long,
    @JvmField val waiter: Long,
    @JvmField val fakeTask: Long,
    @JvmField val credCopy: Long,
    @JvmField val fopsTable: Long,
    @JvmField val owner: Long,
    @JvmField val waitLock: Long,
    @JvmField val waitList: Long,
    @JvmField val listLink: Long,
    @JvmField val headWord: Long,
    @JvmField val headNext: Long,
    @JvmField val headPrev: Long,
    @JvmField val pc: Long,
    @JvmField val right: Long,
    @JvmField val left: Long,
    @JvmField val task: Long,
    @JvmField val waiterLock: Long,
    @JvmField val wakeState: Long,
    @JvmField val waiterPrioOffset: Long,
    @JvmField val deadline: Long,
    @JvmField val wwCtx: Long,
    @JvmField val usage: Long,
    @JvmField val taskPrioOffset: Long,
    @JvmField val normalPrio: Long,
    @JvmField val piLock: Long,
    @JvmField val piWaiters: Long,
    @JvmField val piTopTask: Long,
    @JvmField val piBlockedOn: Long,
    @JvmField val group: Long,
    @JvmField val taskUsage: Long,
    @JvmField val taskPrio: Long,
    @JvmField val waiterPrio: Long,
    @JvmField val initTask: Long,
    @JvmField val rootTaskGroup: Long,
    @JvmField val initCred: Long,
    @JvmField val securityHookHeads: Long,
    @JvmField val pointerBytes: Int
)

/** JNI 参数对象：字段与 native 的 CredentialLayout43499 读取顺序一致。 */
class CredentialLayout43499(
    @JvmField val copySize: Long,
    @JvmField val usageOffset: Long,
    @JvmField val usageValue: Long,
    @JvmField val capsOffset: Long,
    @JvmField val capsCount: Long,
    @JvmField val capsValue: Long,
    @JvmField val refOffset0: Long,
    @JvmField val refOffset1: Long,
    @JvmField val refOffset2: Long,
    @JvmField val refOffset3: Long,
    @JvmField val refImage0: Long,
    @JvmField val refImage1: Long,
    @JvmField val refImage2: Long,
    @JvmField val refImage3: Long,
    @JvmField val refCount: Int
)

/** JNI 参数对象：字段与 native 的 TargetOffsets43499 读取顺序一致。 */
class TargetOffsets43499(
    @JvmField val selinuxEnforcingImageOffset: Long,
    @JvmField val selinuxEnforcingImageValue: Long,
    @JvmField val selinuxEnforcingImagePresent: Boolean,
    @JvmField val selinuxEnforcingImageWidth: Int,
    @JvmField val taskCredOffset: Long,
    @JvmField val taskCredValue: Long,
    @JvmField val taskCredPresent: Boolean,
    @JvmField val taskCredWidth: Int,
    @JvmField val taskRealCredOffset: Long,
    @JvmField val taskRealCredValue: Long,
    @JvmField val taskRealCredPresent: Boolean,
    @JvmField val taskRealCredWidth: Int,
    @JvmField val taskAtomicFlagsOffset: Long,
    @JvmField val taskAtomicFlagsValue: Long,
    @JvmField val taskAtomicFlagsPresent: Boolean,
    @JvmField val taskAtomicFlagsWidth: Int,
    @JvmField val taskSeccompOffset: Long,
    @JvmField val taskSeccompValue: Long,
    @JvmField val taskSeccompPresent: Boolean,
    @JvmField val taskSeccompWidth: Int
)

/** JNI 参数对象：字段与 native 的 Prepare43499 读取顺序一致。 */
class Prepare43499(
    @JvmField val collisions: Long,
    @JvmField val threads: Long,
    @JvmField val futexTableSize: Long,
    @JvmField val roundWaitUs: Long,
    @JvmField val wakeRounds: Long,
    @JvmField val possibleCpus: Long,
    @JvmField val mmSlabOrder: Long,
    @JvmField val prepareMaxAttempts: Long,
    @JvmField val prepareTimeoutMs: Long,
    @JvmField val kernelSnitchTimeoutMs: Long,
    @JvmField val routeWaitMs: Long,
    @JvmField val routeDoneTimeoutMs: Long,
    @JvmField val setupSettleUs: Long,
    @JvmField val statePollIntervalUs: Long,
    @JvmField val headerBytes: Long,
    @JvmField val totalPokes: Long,
    @JvmField val pageSize: Long,
    @JvmField val regionSize: Long,
    @JvmField val objectSize: Long,
    @JvmField val mmStructSize: Long,
    @JvmField val regionThreads: Long,
    @JvmField val mainCpu: Int,
    @JvmField val pollRounds: Long,
    @JvmField val preGroups: Long,
    @JvmField val preShapedGroups: Long,
    @JvmField val sprayGroups: Long,
    @JvmField val postGroups: Long,
    @JvmField val leakGroups: Long,
    @JvmField val wordBytes: Long,
    @JvmField val handleBytes: Long,
    @JvmField val coarseStride: Long,
    @JvmField val slabStride: Long,
    @JvmField val mmObjectsPerSlab: Long,
    @JvmField val shaped: Boolean,
    @JvmField val socketBuffer: Long,
    @JvmField val sendSize: Long,
    @JvmField val proofSeed: Long,
    @JvmField val proofCount: Long,
    @JvmField val counterOffset: Long
)

/** JNI 参数对象：字段与 native 的 Discover43499 读取顺序一致。 */
class Discover43499(
    @JvmField val collisions: Long,
    @JvmField val threads: Long,
    @JvmField val futexTableSize: Long,
    @JvmField val roundWaitUs: Long,
    @JvmField val wakeRounds: Long,
    @JvmField val possibleCpus: Long,
    @JvmField val mmSlabOrder: Long,
    @JvmField val headerBytes: Long,
    @JvmField val totalPokes: Long,
    @JvmField val pageSize: Long,
    @JvmField val regionSize: Long,
    @JvmField val objectSize: Long,
    @JvmField val mmStructSize: Long,
    @JvmField val regionThreads: Long,
    @JvmField val mainCpu: Int,
    @JvmField val pollRounds: Long,
    @JvmField val preGroups: Long,
    @JvmField val preShapedGroups: Long,
    @JvmField val sprayGroups: Long,
    @JvmField val postGroups: Long,
    @JvmField val leakGroups: Long,
    @JvmField val wordBytes: Long,
    @JvmField val handleBytes: Long,
    @JvmField val rangeStart: Long,
    @JvmField val rangeEnd: Long,
    @JvmField val counterOffset: Long,
    @JvmField val sweepTags: Long,
    @JvmField val patienceRounds: Long,
    @JvmField val coarseStride: Long,
    @JvmField val slabStride: Long,
    @JvmField val prepareGeneration: Long
)

/** JNI 参数对象：字段与 native 的 WordWrite43499 读取顺序一致。 */
class WordWrite43499(
    @JvmField val address: Long,
    @JvmField val value: Long,
    @JvmField val form: Int,
    @JvmField val offset: Long,
    @JvmField val offsetPresent: Boolean,
    @JvmField val prepareGeneration: Long,
    @JvmField val discoveryGeneration: Long,
    @JvmField val victimGeneration: Long,
    @JvmField val victimGenerationPresent: Boolean
)

/** JNI 参数对象：字段与 native 的 FinishVictim43499 读取顺序一致。 */
class FinishVictim43499(
    @JvmField val action: Int,
    @JvmField val victimGeneration: Long
)

/** JNI 参数对象：字段与 native 的 Init43284 读取顺序一致。 */
class Init43284(
    @JvmField val spi: Int,
    @JvmField val encapPort: Int,
    @JvmField val senderPort: Int,
    @JvmField val icvLength: Int,
    @JvmField val aesKey: ByteArray,
    @JvmField val hmacKey: ByteArray,
    @JvmField val initialSequence: Int,
    @JvmField val sequencePresent: Boolean
)

/** JNI 参数对象：字段与 native 的 ResolveTarget43284 读取顺序一致。 */
class ResolveTarget43284(
    @JvmField val role: Int,
    @JvmField val path: ByteArray,
    @JvmField val accessMode: Int
)

/** JNI 参数对象：字段与 native 的 VerifyTarget43284 读取顺序一致。 */
class VerifyTarget43284(
    @JvmField val ticket: Long,
    @JvmField val expectedDigest: ByteArray,
    @JvmField val size: Long
)

/** JNI 参数对象：字段与 native 的 WriteBlock43284 读取顺序一致。 */
class WriteBlock43284(
    @JvmField val ticket: Long,
    @JvmField val offset: Long,
    @JvmField val block: ByteArray
)

/** JNI 参数对象：字段与 native 的 ReadBlock43284 读取顺序一致。 */
class ReadBlock43284(
    @JvmField val ticket: Long,
    @JvmField val offset: Long
)

/** JNI 参数对象：字段与 native 的 ModuleCheck43284 读取顺序一致。 */
class ModuleCheck43284(
    @JvmField val moduleImage: ByteArray,
    @JvmField val kernelVermagic: ByteArray
)

/** JNI 参数对象：字段与 native 的 HookPlanRequest43284 读取顺序一致。 */
class HookPlanRequest43284(
    @JvmField val hookImage: ByteArray,
    @JvmField val hookSymbol: ByteArray,
    @JvmField val guardPolicy: Int
)

/** JNI 参数对象：字段与 native 的 Wait43284 读取顺序一致。 */
class Wait43284(
    @JvmField val budgetMs: Long,
    @JvmField val pollMs: Long,
    @JvmField val successMarker: ByteArray,
    @JvmField val failureMarker: ByteArray,
    @JvmField val inFlightMarker: ByteArray,
    @JvmField val moduleMarker: ByteArray,
    @JvmField val moduleName: ByteArray
)

/** JNI 参数对象：字段与 native 的 Trigger43284 读取顺序一致。 */
class Trigger43284(
    @JvmField val delayMs: Long
)

/** JNI 参数对象：字段与 native 的 OpenTarget43284 读取顺序一致。 */
class OpenTarget43284(
    @JvmField val targetPath: ByteArray,
    @JvmField val openedTarget: Long
)

/** JNI 参数对象：字段与 native 的 CloseTarget43284 读取顺序一致。 */
class CloseTarget43284(
    @JvmField val closingTarget: Long
)

/** JNI 参数对象：字段与 native 的 ConnectEsp43284 读取顺序一致。 */
class ConnectEsp43284(
    @JvmField val espTarget: Long,
    @JvmField val espSocket: Long
)

/** JNI 参数对象：字段与 native 的 CloseSocket43284 读取顺序一致。 */
class CloseSocket43284(
    @JvmField val closingSocket: Long
)

/** JNI 参数对象：字段与 native 的 MakeSurface43284 读取顺序一致。 */
class MakeSurface43284(
    @JvmField val surfaceTarget: Long,
    @JvmField val surfaceSocket: Long,
    @JvmField val madeSurface: Long
)

/** JNI 参数对象：字段与 native 的 DestroySurface43284 读取顺序一致。 */
class DestroySurface43284(
    @JvmField val destroyedSurface: Long
)

/** JNI 参数对象：字段与 native 的 MakeLanding43284 读取顺序一致。 */
class MakeLanding43284(
    @JvmField val landingSurface: Long,
    @JvmField val madeLanding: Long
)

/** JNI 参数对象：字段与 native 的 DestroyLanding43284 读取顺序一致。 */
class DestroyLanding43284(
    @JvmField val destroyedLanding: Long
)

/** JNI 参数对象：字段与 native 的 AccessParams43284 读取顺序一致。 */
class AccessParams43284(
    @JvmField val accessCarrierPath: ByteArray,
    @JvmField val accessDevCarrierPath: ByteArray,
    @JvmField val accessPatchTargetPath: ByteArray,
    @JvmField val accessCarrierBase: Long,
    @JvmField val accessOffset: Long,
    @JvmField val accessOffsetSet: Boolean,
    @JvmField val accessHookImagePath: ByteArray,
    @JvmField val accessKernelRelease: ByteArray,
    @JvmField val accessKernelPreempt: Boolean,
    @JvmField val accessKernelModversions: Boolean,
    @JvmField val accessKernelModuleForceUnload: Boolean,
    @JvmField val accessHookPlanSet: Boolean,
    @JvmField val accessTriggerDelayMs: Long,
    @JvmField val accessWaitMs: Long,
    @JvmField val accessSuccessMarkerPath: ByteArray,
    @JvmField val accessFailureMarkerPath: ByteArray,
    @JvmField val accessInFlightMarkerPath: ByteArray,
    @JvmField val accessModuleMarkerPath: ByteArray
)

/** JNI 参数对象：字段与 native 的 WriteImage43284 读取顺序一致。 */
class WriteImage43284(
    @JvmField val imageAccessId: Long,
    @JvmField val imageOffset: Long,
    @JvmField val imageWidth: Long,
    @JvmField val imageBytes: ByteArray,
    @JvmField val imageBudgetMs: Long
)

/** JNI 参数对象：字段与 native 的 WriteHook43284 读取顺序一致。 */
class WriteHook43284(
    @JvmField val hookAccessId: Long,
    @JvmField val hookWidth: Long
)

/** JNI 参数对象：字段与 native 的 HookPlan43284 读取顺序一致。 */
class HookPlan43284(
    @JvmField val hookPlanFileOffset: Long,
    @JvmField val hookPlanVaddr: Long,
    @JvmField val hookPlanGuardInstruction: Long,
    @JvmField val hookPlanDisplacedInstruction: Long,
    @JvmField val hookPlanPayloadFileOffset: Long,
    @JvmField val hookPlanPayloadVaddr: Long,
    @JvmField val hookPlanPayloadMaxBytes: Long,
    @JvmField val hookPlanEntryOffset: Long,
    @JvmField val hookPlanDisplacedOffset: Long
)

/** JNI 参数对象：字段与 native 的 Access43499Abi 读取顺序一致。 */
class Access43499Abi(
    @JvmField val abiPayloadOffset0: Long,
    @JvmField val abiPayloadOffset1: Long,
    @JvmField val abiPayloadOffset2: Long,
    @JvmField val abiPayloadOffset3: Long,
    @JvmField val abiPayloadOffset4: Long,
    @JvmField val abiKernelPhys0: Long,
    @JvmField val abiKernelPhys1: Long,
    @JvmField val abiImageOffset0: Long,
    @JvmField val abiImageOffset1: Long,
    @JvmField val abiImageOffset2: Long
)

/** 字段布局（诊断/对拍用），与 native model 的形状一致。 */
object AttackContractLayouts {
    val groups: Map<String, List<AttackContractField>> = linkedMapOf(
        "Init43499" to listOf(
            AttackContractField("routeId", "long", 0, 0),
            AttackContractField("mainCpu", "int", 0, 0),
            AttackContractField("consumerCpu", "int", 0, 0),
            AttackContractField("waiterOffset", "long", 0, 0),
            AttackContractField("taskOffset", "long", 0, 0),
            AttackContractField("lockOffset", "long", 0, 0),
            AttackContractField("bufferSize", "long", 0, 0),
            AttackContractField("maxAttempts", "long", 0, 0),
            AttackContractField("armSequence", "long", 0, 0),
            AttackContractField("armHold", "long", 0, 0),
            AttackContractField("parkMs", "long", 0, 0),
            AttackContractField("settleUs", "long", 0, 0),
            AttackContractField("postRequeueSettleUs", "long", 0, 0),
            AttackContractField("postAdjustSettleUs", "long", 0, 0),
            AttackContractField("consumerDelayUs", "long", 0, 0),
            AttackContractField("fallbackLockUs", "long", 0, 0),
            AttackContractField("burstCalls", "long", 0, 0),
            AttackContractField("maxCalls", "long", 0, 0),
            AttackContractField("niceModulus", "int", 0, 0),
            AttackContractField("policy", "int", 0, 0),
            AttackContractField("multicastPresent", "boolean", 0, 0),
            AttackContractField("armRounds", "long", 0, 0),
            AttackContractField("stampWaiterShift", "long", 0, 0),
            AttackContractField("tcpPresent", "boolean", 0, 0),
            AttackContractField("zeroCopyMapBytes", "long", 0, 0),
            AttackContractField("selectPresent", "boolean", 0, 0),
            AttackContractField("waiterShift", "long", 0, 0),
            AttackContractField("fdCount", "long", 0, 0),
            AttackContractField("perfSlots", "long", 0, 0),
            AttackContractField("perfType0", "long", 0, 0),
            AttackContractField("perfConfig0", "long", 0, 0),
            AttackContractField("perfType1", "long", 0, 0),
            AttackContractField("perfConfig1", "long", 0, 0),
            AttackContractField("perfType2", "long", 0, 0),
            AttackContractField("perfConfig2", "long", 0, 0),
            AttackContractField("perfType3", "long", 0, 0),
            AttackContractField("perfConfig3", "long", 0, 0),
            AttackContractField("ringPages", "long", 0, 0),
            AttackContractField("ringWakeup", "long", 0, 0),
            AttackContractField("spraySize", "long", 0, 0),
            AttackContractField("sprayCount", "long", 0, 0),
            AttackContractField("pumpIters", "long", 0, 0),
            AttackContractField("routeSelected", "long", 0, 0),
            AttackContractField("fakeTask", "long", 0, 0),
            AttackContractField("fakeLock", "long", 0, 0),
        ),
        "KernelLayout43499" to listOf(
            AttackContractField("lock", "long", 0, 0),
            AttackContractField("waiter", "long", 0, 0),
            AttackContractField("fakeTask", "long", 0, 0),
            AttackContractField("credCopy", "long", 0, 0),
            AttackContractField("fopsTable", "long", 0, 0),
            AttackContractField("owner", "long", 0, 0),
            AttackContractField("waitLock", "long", 0, 0),
            AttackContractField("waitList", "long", 0, 0),
            AttackContractField("listLink", "long", 0, 0),
            AttackContractField("headWord", "long", 0, 0),
            AttackContractField("headNext", "long", 0, 0),
            AttackContractField("headPrev", "long", 0, 0),
            AttackContractField("pc", "long", 0, 0),
            AttackContractField("right", "long", 0, 0),
            AttackContractField("left", "long", 0, 0),
            AttackContractField("task", "long", 0, 0),
            AttackContractField("waiterLock", "long", 0, 0),
            AttackContractField("wakeState", "long", 0, 0),
            AttackContractField("waiterPrioOffset", "long", 0, 0),
            AttackContractField("deadline", "long", 0, 0),
            AttackContractField("wwCtx", "long", 0, 0),
            AttackContractField("usage", "long", 0, 0),
            AttackContractField("taskPrioOffset", "long", 0, 0),
            AttackContractField("normalPrio", "long", 0, 0),
            AttackContractField("piLock", "long", 0, 0),
            AttackContractField("piWaiters", "long", 0, 0),
            AttackContractField("piTopTask", "long", 0, 0),
            AttackContractField("piBlockedOn", "long", 0, 0),
            AttackContractField("group", "long", 0, 0),
            AttackContractField("taskUsage", "long", 0, 0),
            AttackContractField("taskPrio", "long", 0, 0),
            AttackContractField("waiterPrio", "long", 0, 0),
            AttackContractField("initTask", "long", 0, 0),
            AttackContractField("rootTaskGroup", "long", 0, 0),
            AttackContractField("initCred", "long", 0, 0),
            AttackContractField("securityHookHeads", "long", 0, 0),
            AttackContractField("pointerBytes", "int", 0, 0),
        ),
        "CredentialLayout43499" to listOf(
            AttackContractField("copySize", "long", 0, 0),
            AttackContractField("usageOffset", "long", 0, 0),
            AttackContractField("usageValue", "long", 0, 0),
            AttackContractField("capsOffset", "long", 0, 0),
            AttackContractField("capsCount", "long", 0, 0),
            AttackContractField("capsValue", "long", 0, 0),
            AttackContractField("refOffset0", "long", 0, 0),
            AttackContractField("refOffset1", "long", 0, 0),
            AttackContractField("refOffset2", "long", 0, 0),
            AttackContractField("refOffset3", "long", 0, 0),
            AttackContractField("refImage0", "long", 0, 0),
            AttackContractField("refImage1", "long", 0, 0),
            AttackContractField("refImage2", "long", 0, 0),
            AttackContractField("refImage3", "long", 0, 0),
            AttackContractField("refCount", "int", 0, 0),
        ),
        "TargetOffsets43499" to listOf(
            AttackContractField("selinuxEnforcingImageOffset", "long", 0, 0),
            AttackContractField("selinuxEnforcingImageValue", "long", 0, 0),
            AttackContractField("selinuxEnforcingImagePresent", "boolean", 0, 0),
            AttackContractField("selinuxEnforcingImageWidth", "int", 0, 0),
            AttackContractField("taskCredOffset", "long", 0, 0),
            AttackContractField("taskCredValue", "long", 0, 0),
            AttackContractField("taskCredPresent", "boolean", 0, 0),
            AttackContractField("taskCredWidth", "int", 0, 0),
            AttackContractField("taskRealCredOffset", "long", 0, 0),
            AttackContractField("taskRealCredValue", "long", 0, 0),
            AttackContractField("taskRealCredPresent", "boolean", 0, 0),
            AttackContractField("taskRealCredWidth", "int", 0, 0),
            AttackContractField("taskAtomicFlagsOffset", "long", 0, 0),
            AttackContractField("taskAtomicFlagsValue", "long", 0, 0),
            AttackContractField("taskAtomicFlagsPresent", "boolean", 0, 0),
            AttackContractField("taskAtomicFlagsWidth", "int", 0, 0),
            AttackContractField("taskSeccompOffset", "long", 0, 0),
            AttackContractField("taskSeccompValue", "long", 0, 0),
            AttackContractField("taskSeccompPresent", "boolean", 0, 0),
            AttackContractField("taskSeccompWidth", "int", 0, 0),
        ),
        "Prepare43499" to listOf(
            AttackContractField("collisions", "long", 0, 0),
            AttackContractField("threads", "long", 0, 0),
            AttackContractField("futexTableSize", "long", 0, 0),
            AttackContractField("roundWaitUs", "long", 0, 0),
            AttackContractField("wakeRounds", "long", 0, 0),
            AttackContractField("possibleCpus", "long", 0, 0),
            AttackContractField("mmSlabOrder", "long", 0, 0),
            AttackContractField("prepareMaxAttempts", "long", 0, 0),
            AttackContractField("prepareTimeoutMs", "long", 0, 0),
            AttackContractField("kernelSnitchTimeoutMs", "long", 0, 0),
            AttackContractField("routeWaitMs", "long", 0, 0),
            AttackContractField("routeDoneTimeoutMs", "long", 0, 0),
            AttackContractField("setupSettleUs", "long", 0, 0),
            AttackContractField("statePollIntervalUs", "long", 0, 0),
            AttackContractField("headerBytes", "long", 0, 0),
            AttackContractField("totalPokes", "long", 0, 0),
            AttackContractField("pageSize", "long", 0, 0),
            AttackContractField("regionSize", "long", 0, 0),
            AttackContractField("objectSize", "long", 0, 0),
            AttackContractField("mmStructSize", "long", 0, 0),
            AttackContractField("regionThreads", "long", 0, 0),
            AttackContractField("mainCpu", "int", 0, 0),
            AttackContractField("pollRounds", "long", 0, 0),
            AttackContractField("preGroups", "long", 0, 0),
            AttackContractField("preShapedGroups", "long", 0, 0),
            AttackContractField("sprayGroups", "long", 0, 0),
            AttackContractField("postGroups", "long", 0, 0),
            AttackContractField("leakGroups", "long", 0, 0),
            AttackContractField("wordBytes", "long", 0, 0),
            AttackContractField("handleBytes", "long", 0, 0),
            AttackContractField("coarseStride", "long", 0, 0),
            AttackContractField("slabStride", "long", 0, 0),
            AttackContractField("mmObjectsPerSlab", "long", 0, 0),
            AttackContractField("shaped", "boolean", 0, 0),
            AttackContractField("socketBuffer", "long", 0, 0),
            AttackContractField("sendSize", "long", 0, 0),
            AttackContractField("proofSeed", "long", 0, 0),
            AttackContractField("proofCount", "long", 0, 0),
            AttackContractField("counterOffset", "long", 0, 0),
        ),
        "Discover43499" to listOf(
            AttackContractField("collisions", "long", 0, 0),
            AttackContractField("threads", "long", 0, 0),
            AttackContractField("futexTableSize", "long", 0, 0),
            AttackContractField("roundWaitUs", "long", 0, 0),
            AttackContractField("wakeRounds", "long", 0, 0),
            AttackContractField("possibleCpus", "long", 0, 0),
            AttackContractField("mmSlabOrder", "long", 0, 0),
            AttackContractField("headerBytes", "long", 0, 0),
            AttackContractField("totalPokes", "long", 0, 0),
            AttackContractField("pageSize", "long", 0, 0),
            AttackContractField("regionSize", "long", 0, 0),
            AttackContractField("objectSize", "long", 0, 0),
            AttackContractField("mmStructSize", "long", 0, 0),
            AttackContractField("regionThreads", "long", 0, 0),
            AttackContractField("mainCpu", "int", 0, 0),
            AttackContractField("pollRounds", "long", 0, 0),
            AttackContractField("preGroups", "long", 0, 0),
            AttackContractField("preShapedGroups", "long", 0, 0),
            AttackContractField("sprayGroups", "long", 0, 0),
            AttackContractField("postGroups", "long", 0, 0),
            AttackContractField("leakGroups", "long", 0, 0),
            AttackContractField("wordBytes", "long", 0, 0),
            AttackContractField("handleBytes", "long", 0, 0),
            AttackContractField("rangeStart", "long", 0, 0),
            AttackContractField("rangeEnd", "long", 0, 0),
            AttackContractField("counterOffset", "long", 0, 0),
            AttackContractField("sweepTags", "long", 0, 0),
            AttackContractField("patienceRounds", "long", 0, 0),
            AttackContractField("coarseStride", "long", 0, 0),
            AttackContractField("slabStride", "long", 0, 0),
            AttackContractField("prepareGeneration", "long", 0, 0),
        ),
        "WordWrite43499" to listOf(
            AttackContractField("address", "long", 0, 0),
            AttackContractField("value", "long", 0, 0),
            AttackContractField("form", "int", 0, 0),
            AttackContractField("offset", "long", 0, 0),
            AttackContractField("offsetPresent", "boolean", 0, 0),
            AttackContractField("prepareGeneration", "long", 0, 0),
            AttackContractField("discoveryGeneration", "long", 0, 0),
            AttackContractField("victimGeneration", "long", 0, 0),
            AttackContractField("victimGenerationPresent", "boolean", 0, 0),
        ),
        "FinishVictim43499" to listOf(
            AttackContractField("action", "int", 0, 0),
            AttackContractField("victimGeneration", "long", 0, 0),
        ),
        "Init43284" to listOf(
            AttackContractField("spi", "int", 0, 0),
            AttackContractField("encapPort", "int", 0, 0),
            AttackContractField("senderPort", "int", 0, 0),
            AttackContractField("icvLength", "int", 0, 0),
            AttackContractField("aesKey", "secret32", 0, 0),
            AttackContractField("hmacKey", "secret32", 0, 0),
            AttackContractField("initialSequence", "int", 0, 0),
            AttackContractField("sequencePresent", "boolean", 0, 0),
        ),
        "ResolveTarget43284" to listOf(
            AttackContractField("role", "int", 0, 0),
            AttackContractField("path", "bytes", 4096, 4096),
            AttackContractField("accessMode", "int", 0, 0),
        ),
        "VerifyTarget43284" to listOf(
            AttackContractField("ticket", "long", 0, 0),
            AttackContractField("expectedDigest", "bytes32", 0, 0),
            AttackContractField("size", "long", 0, 0),
        ),
        "WriteBlock43284" to listOf(
            AttackContractField("ticket", "long", 0, 0),
            AttackContractField("offset", "long", 0, 0),
            AttackContractField("block", "bytes16", 0, 0),
        ),
        "ReadBlock43284" to listOf(
            AttackContractField("ticket", "long", 0, 0),
            AttackContractField("offset", "long", 0, 0),
        ),
        "ModuleCheck43284" to listOf(
            AttackContractField("moduleImage", "bytes", 67108864, 67108864),
            AttackContractField("kernelVermagic", "bytes", 4096, 4096),
        ),
        "HookPlanRequest43284" to listOf(
            AttackContractField("hookImage", "bytes", 67108864, 67108864),
            AttackContractField("hookSymbol", "bytes", 512, 512),
            AttackContractField("guardPolicy", "int", 0, 0),
        ),
        "Wait43284" to listOf(
            AttackContractField("budgetMs", "long", 0, 0),
            AttackContractField("pollMs", "long", 0, 0),
            AttackContractField("successMarker", "bytes", 4096, 4096),
            AttackContractField("failureMarker", "bytes", 4096, 4096),
            AttackContractField("inFlightMarker", "bytes", 4096, 4096),
            AttackContractField("moduleMarker", "bytes", 4096, 4096),
            AttackContractField("moduleName", "bytes", 256, 256),
        ),
        "Trigger43284" to listOf(
            AttackContractField("delayMs", "long", 0, 0),
        ),
        "OpenTarget43284" to listOf(
            AttackContractField("targetPath", "bytes", 4096, 4096),
            AttackContractField("openedTarget", "long", 0, 0),
        ),
        "CloseTarget43284" to listOf(
            AttackContractField("closingTarget", "long", 0, 0),
        ),
        "ConnectEsp43284" to listOf(
            AttackContractField("espTarget", "long", 0, 0),
            AttackContractField("espSocket", "long", 0, 0),
        ),
        "CloseSocket43284" to listOf(
            AttackContractField("closingSocket", "long", 0, 0),
        ),
        "MakeSurface43284" to listOf(
            AttackContractField("surfaceTarget", "long", 0, 0),
            AttackContractField("surfaceSocket", "long", 0, 0),
            AttackContractField("madeSurface", "long", 0, 0),
        ),
        "DestroySurface43284" to listOf(
            AttackContractField("destroyedSurface", "long", 0, 0),
        ),
        "MakeLanding43284" to listOf(
            AttackContractField("landingSurface", "long", 0, 0),
            AttackContractField("madeLanding", "long", 0, 0),
        ),
        "DestroyLanding43284" to listOf(
            AttackContractField("destroyedLanding", "long", 0, 0),
        ),
        "AccessParams43284" to listOf(
            AttackContractField("accessCarrierPath", "bytes", 4096, 4096),
            AttackContractField("accessDevCarrierPath", "bytes", 4096, 4096),
            AttackContractField("accessPatchTargetPath", "bytes", 4096, 4096),
            AttackContractField("accessCarrierBase", "long", 0, 0),
            AttackContractField("accessOffset", "long", 0, 0),
            AttackContractField("accessOffsetSet", "boolean", 0, 0),
            AttackContractField("accessHookImagePath", "bytes", 4096, 4096),
            AttackContractField("accessKernelRelease", "bytes", 256, 256),
            AttackContractField("accessKernelPreempt", "boolean", 0, 0),
            AttackContractField("accessKernelModversions", "boolean", 0, 0),
            AttackContractField("accessKernelModuleForceUnload", "boolean", 0, 0),
            AttackContractField("accessHookPlanSet", "boolean", 0, 0),
            AttackContractField("accessTriggerDelayMs", "long", 0, 0),
            AttackContractField("accessWaitMs", "long", 0, 0),
            AttackContractField("accessSuccessMarkerPath", "bytes", 4096, 4096),
            AttackContractField("accessFailureMarkerPath", "bytes", 4096, 4096),
            AttackContractField("accessInFlightMarkerPath", "bytes", 4096, 4096),
            AttackContractField("accessModuleMarkerPath", "bytes", 4096, 4096),
        ),
        "WriteImage43284" to listOf(
            AttackContractField("imageAccessId", "long", 0, 0),
            AttackContractField("imageOffset", "long", 0, 0),
            AttackContractField("imageWidth", "long", 0, 0),
            AttackContractField("imageBytes", "bytes", 67108864, 67108864),
            AttackContractField("imageBudgetMs", "long", 0, 0),
        ),
        "WriteHook43284" to listOf(
            AttackContractField("hookAccessId", "long", 0, 0),
            AttackContractField("hookWidth", "long", 0, 0),
        ),
        "HookPlan43284" to listOf(
            AttackContractField("hookPlanFileOffset", "long", 0, 0),
            AttackContractField("hookPlanVaddr", "long", 0, 0),
            AttackContractField("hookPlanGuardInstruction", "long", 0, 0),
            AttackContractField("hookPlanDisplacedInstruction", "long", 0, 0),
            AttackContractField("hookPlanPayloadFileOffset", "long", 0, 0),
            AttackContractField("hookPlanPayloadVaddr", "long", 0, 0),
            AttackContractField("hookPlanPayloadMaxBytes", "long", 0, 0),
            AttackContractField("hookPlanEntryOffset", "long", 0, 0),
            AttackContractField("hookPlanDisplacedOffset", "long", 0, 0),
        ),
        "Access43499Abi" to listOf(
            AttackContractField("abiPayloadOffset0", "long", 0, 0),
            AttackContractField("abiPayloadOffset1", "long", 0, 0),
            AttackContractField("abiPayloadOffset2", "long", 0, 0),
            AttackContractField("abiPayloadOffset3", "long", 0, 0),
            AttackContractField("abiPayloadOffset4", "long", 0, 0),
            AttackContractField("abiKernelPhys0", "long", 0, 0),
            AttackContractField("abiKernelPhys1", "long", 0, 0),
            AttackContractField("abiImageOffset0", "long", 0, 0),
            AttackContractField("abiImageOffset1", "long", 0, 0),
            AttackContractField("abiImageOffset2", "long", 0, 0),
        )
    )
}
