# Counter

Counter 每拍最多处理一个 requests 队列中的 Add，并通过 replies 返回同一个对象。两个端口都指向 CounterSystem 拥有的队列。输入为空或回复队列满时不消费请求。

Work 在回复队列接受对象之后填写 before（本拍的计数值），将加法结果暂存到 nextValue_，弹出输入并标记进展。Xfer 将 nextValue_ 提交给 value_，下一个请求就能读到更新后的计数。复位时当前值、待提交值和已接受请求数均清零。

accept 日志记录旧值、增量和新值，stall 日志记录回复队列已满。报告列出已接受的请求数和最终提交值。[逐拍示例和测试说明](CounterSystem.md)
