# 两个模块如何交换信息

客户端 Client 发送加法请求，Counter 更新计数并返回「加之前的值」。两条队列组成一条往返路径：

```text
Client  ── requests (delta) ────────► Counter
Client  ◄── replies (delta, before) ── Counter
```

从仓库根目录运行：

```sh
cmake -S . -B build
cmake --build build --target counter_example
./build/counter_example
```

客户端连续发出 +2、+3。每拍先调用 Counter.Work()，再调用 Client.Work()；队列在 Xfer 阶段推进。requests 可容纳两条消息，replies 只能容纳一条。

| 周期 | Counter | Client |
| --- | --- | --- |
| 1 | 请求队列空，等待 | 发送 +2 |
| 2 | 读到 +2，回复旧值 0，拍末提交新值 2 | 发送 +3 |
| 3 | 回复队列仍满，保留 +3 请求 | 读到第一条回复，核对旧值 0 |
| 4 | 读到 +3，回复旧值 2，拍末提交新值 5 | 等待 |
| 5 | 无新请求 | 核对旧值 2，结束仿真 |

[CounterSystem.cpp](CounterSystem.cpp) 只负责组装和调度。Build 注册四个直接对象（两个子模块、两条队列）。两个队列分别绑定一个生产者和一个消费者。父模块先推进队列，再调用子模块的 Xfer。Reset 清空队列并重置子模块。ReportStat 汇总各模块的报告。

这是当前单线程调度器上的消息示例。加法请求相当于一个计数更新提案，旧值通过回复返回。请求和回复都经过队列。队列 trace 可按默认频率逐拍观察 delta 和 before。

正常交换最多间隔一个等待周期，因此入口设置连续四拍无进展时报死锁。CTest 的 counter_exchange 覆盖逐拍流转、输出阻塞时的对象身份、恢复、复位、死锁和日志统计。
