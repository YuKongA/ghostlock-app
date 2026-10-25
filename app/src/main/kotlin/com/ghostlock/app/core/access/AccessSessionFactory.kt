package com.ghostlock.app.core.access

/**
 * backend 绑定的 AccessSession 工厂。
 *
 * 配置、profile、target 绑定在构造工厂时确定；工厂不接收 request/handle，
 * 也不在 open() 里发布能力。open() 只创建会话与 native session slot
 * （INITIALIZING），不调用 backend initialize；backend 初始化只由
 * [AccessSession.initialize] 完成。之后才能从 [AccessSession.capabilities]
 * 取借用视图。
 */
fun interface AccessSessionFactory {
    /** 建立一次会话；失败不得留下半发布的 session 或能力视图。 */
    suspend fun open(): AccessResult<AccessSession>
}
