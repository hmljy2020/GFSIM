# Client

Client 是 SimTestbench 子模块。它通过输出端口 requests 依次发送 +2 和 +3，通过输入端口 replies 检查回复。两条队列都归 CounterSystem 所有。

每次发送前先检查容量，每条请求只分配一个 Add 对象。收到回复时先核对请求的增量和旧值，再弹出该回复。RecordStimulus 和 RecordOutputChecked 标记实际进展，计数由基类在复位时清零。两条回复都验证通过后，基类请求结束仿真。

send 和 reply 日志列出每次交换的值；测试台报告自复位以来的发送数和验证数。[逐拍示例和测试说明](CounterSystem.md)
