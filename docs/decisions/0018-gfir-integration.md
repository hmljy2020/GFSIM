# GFIR 对接建议：module 结构、连接与执行逻辑

本文面向 GFIR 与 GFSIM 的开发合作者，记录双方对接采用的目标生成规范。范围包括 module 的接口声明、实例连接、内部 state/queue 与定长 table 声明，以及执行逻辑到 Work 和 Xfer 的映射。后续可以逐项补充更具体的操作规范。

文中的 GFIR 表达是说明所需信息的伪代码，不是对 GFIR 实际语法、属性名或 operand 布局的定义。GFSIM 保持独立；对接由生成器将这些信息转换为现有 C++ API。

## 1. 区分模块定义和实例连接

GFIR 中的 module 是一个 op。对接时需要区分两层信息，它们可以由具体 IR 设计放在定义、实例化或连接表达中：

| 层次 | 必须表达的信息 | 生成结果 |
| --- | --- | --- |
| 模块定义 | 输入 queue 的名称、payload 类型和可选性 | 输入成员、构造参数、`BindInput()` |
| 模块定义 | 输出 queue 的名称、payload 类型和可选性 | 输出成员、构造参数、`BindOutput()` |
| 模块定义 | module 引用的名称、目标类型、只读属性和可选性 | 具体类型的指针、构造参数、`BindModule()` |
| 实例连接 | 实例名、模块类型、各端口对应的物理 queue | 父模块创建实例时传入 queue 指针 |
| 实例连接 | 每个 module 引用实际指向的模块实例 | 父模块创建实例时传入目标模块指针 |

模块定义说明“需要哪些连接、类型是什么”，实例连接说明“实际接到谁”。生成器应使用稳定的 IR 顺序展开成员、构造参数和注册操作，保证生成结果可重复。

模块可以有零个或多个输入、零个或多个输出、零个或多个 module 引用。仅生成实际存在的接口，不增加占位端口。

## 2. 接口声明生成构造与绑定

queue 端口生成带 `INPUT` / `OUTPUT` 标记的非 owning `SimQueue<T>*`；module 引用生成具体目标类型的非 owning 指针。

构造函数通过 `BindInput()`、`BindOutput()`、`BindModule()` 登记连接。这些接口返回原指针，供成员初始化使用；不会创建或复制目标对象，也不会取得目标的所有权。

可选接口采用条件绑定，例如 `target ? BindModule(target) : nullptr`；必需接口直接绑定，空指针会被拒绝。只读 module 引用可以生成 `const Target*`。同一调用方对同一目标只登记一次，多种 method 共用这条引用。

## 3. 父 module 描述并构建内部连接

一个 module op 内部可以包含物理 queue、子 module 实例及它们的连接表达。这些信息生成父模块 `Build()` 中的构建代码：

1. 首先调用 `SetObjectCapacity(n)`，其中 n 为直接子模块数加本模块拥有的物理 queue 数。
2. 创建 queue，通过 `AddInnerQueue()` 注册所有权，并设置容量等配置。
3. 创建子模块，把已确定的 queue 和目标 module 指针作为构造参数传入。
4. 通过 `AddSubmodule()` 注册子模块所有权。
5. 完成需要延后处理的 module 引用连接。
6. 调用 `BuildInnerQueues()`，再调用 `BuildChildren()`。

这是一种递归生成方式：父模块只负责直接拥有的对象；子模块自己的 `Build()` 负责更深一层的对象。不能由父模块重复注册孙模块或重复构建孩子的内部 queue。

## 4. 简单例子

下面的父模块拥有两个子模块和一条 queue。Producer 向 queue 写入，Consumer 从 queue 读取；Producer 还保留 Consumer 的 module 引用，供代码显式调用其 method。

```text
module Producer:
  output out: queue<int>
  reference target: Consumer

module Consumer:
  input in: queue<int>

module Parent:
  queue q: int, capacity=8

  instance producer: Producer
  instance consumer: Consumer

  connect producer.out -> q
  connect q -> consumer.in
  connect producer.target -> consumer
```

### 4.1 生成的声明

以下代码只展示连接相关声明和实现。正式生成时，每个具体模块分别使用一个头文件、一个实现文件和一份相邻模块文档，并从仓库 `templates/SimModule.*.in` 展开。五个生命周期函数均须实现；下例省略 Work、Xfer、Reset、ReportStat 的函数体，不是独立的可运行仿真程序。

```cpp
#include <memory>
#include <string>
#include "SimModule.h"

class Consumer final : public gfsim::SimModule {
public:
    Consumer(std::string name, gfsim::SimQueue<int>* in);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    INPUT gfsim::SimQueue<int>* in_;
};

class Producer final : public gfsim::SimModule {
public:
    Producer(std::string name, gfsim::SimQueue<int>* out,
             Consumer* target);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    OUTPUT gfsim::SimQueue<int>* out_;
    Consumer* target_;
};

class Parent final : public gfsim::SimModule {
public:
    explicit Parent(gfsim::SimSystem* sim);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    INNER gfsim::SimQueue<int>* q_;
};
```

### 4.2 生成的构造和 Build

```cpp
Consumer::Consumer(std::string name, gfsim::SimQueue<int>* in)
    : SimModule(name), in_(BindInput(in)) {}

Producer::Producer(std::string name, gfsim::SimQueue<int>* out,
                   Consumer* target)
    : SimModule(name), out_(BindOutput(out)),
      target_(BindModule(target)) {}

Parent::Parent(gfsim::SimSystem* sim)
    : SimModule("parent", sim), q_(nullptr) {}

void Consumer::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void Producer::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void Parent::Build()
{
    SetObjectCapacity(3); // 两个直接子模块 + 一条物理 queue

    auto queue = std::make_shared<gfsim::SimQueue<int>>("q");
    queue->InitMaxSize(8);
    q_ = AddInnerQueue(queue);

    // 先构造引用目标，再把它传给调用方。
    auto consumer = std::make_shared<Consumer>("consumer", q_);
    auto producer = std::make_shared<Producer>(
        "producer", q_, consumer.get());

    // 注册次序独立于构造次序；这里保留 IR 的实例顺序。
    AddSubmodule(producer);
    AddSubmodule(consumer);

    BuildInnerQueues();
    BuildChildren();
}
```

`AddInnerQueue()` 和 `AddSubmodule()` 保存共享所有权，因此局部 `shared_ptr` 离开 Build 作用域后，对象仍然存活。`q_` 和子模块中的连接指针只用于访问对象。

这个例子在拓扑上形成三种关系：Parent 拥有两个孩子和 q；queue 边为 `producer -> q -> consumer`；module 引用边为 `producer -> consumer`。引用不会再创建一个 Consumer，也不会占用对象容量或新增调度任务。

## 5. 连接生成必须满足的约束

- 每个对象有稳定、非空的名称；所有对象及连接在系统 Build 完成后固定。
- 每条物理 queue 只创建一次，由一个父模块通过 `AddInnerQueue()` 持有；容量为 1..512。
- 每条物理 queue 恰好具有一个 `BindOutput()` 生产者和一个 `BindInput()` 消费者，不允许未连接的 queue。
- 转交同一 queue 指针不意味着层层重复绑定；绑定表示实际生产者或消费者角色。
- 只有根模块注册到 `SimSystem`。子模块和内部 queue 通过父模块注册。
- module 引用目标必须属于同一个系统的已注册模块集合，允许跨层级、跨根、自引用和循环引用。
- 对于循环 module 引用，生成器先创建实例，再通过生成的连接函数在构建期间调用 `BindModule()`。不能仅依赖构造函数参数顺序，也不能递归沿引用构建目标。
- module 引用只记录调用关系。实际 method 调用由代码显式执行，不会因绑定而自动调用，也不改变线程分配。

## 6. 内部组件声明与执行逻辑分别描述

state、queue 和 method 应分别描述：state 与 queue 是数据组件，method 是可以查询或操作这些组件的普通过程。模块可以拥有内部 queue，method 也可以操作 queue；具体操作必须遵守对应组件的可见性、端点绑定和并发访问约定。

GFIR 的 module op 同时提供结构和行为信息。生成器将声明展开成成员和构建代码，将每拍主动执行的行为展开成 Work，并根据组件声明生成阶段提交代码：

| GFIR 内容 | GFSIM 生成结果 |
| --- | --- |
| state 名称、类型、初始值 | 私有 `SimState<T>` 成员及显式初始化 |
| 内部 queue 名称、payload 类型、容量和延迟等配置 | queue 访问成员，Build 中创建、配置并通过 `AddInnerQueue()` 持有 |
| 普通计算与临时值 | C++ 表达式、局部变量或辅助函数 |
| 条件、循环和过程调用 | 保留既定语义与操作顺序的 C++ 控制流和函数调用 |
| 每拍主动执行的逻辑 | `Work()` 中执行的逻辑或由它调用的局部函数 |
| state/queue 操作 | 相应组件的 `Read`、`Write`、`Front`、`Pop` 等 API 调用 |
| 对外 method 定义 | 普通成员函数，仅在调用点显式执行 |
| 组件的阶段提交 | `Xfer()` 中的 state 提交与 queue 推进 helper |

state 直接嵌入模块，不加入 Objects，不计入对象容量；owner 显式生成每个 state 的 Build、Xfer、Reset、ReportStat 调用，跳过其空 Work。queue 则计入对象容量，并遵守前述所有权和端点绑定规则。

method 可以访问 state，也可以操作 queue，不限定为 state 的读写接口。绑定 module 引用不会自动执行 method 的函数体；只有 Work 或它调用的过程明确调用该 method 时，函数体才在调用者线程执行。

## 7. Work 执行行为，Xfer 提交组件

Work 中执行普通计算、控制流、就绪判断、state 读写和合法 queue 操作。已 lower 的 rule 可以生成普通辅助函数，并由 Work 按 GFIR 给定的顺序显式调用。

Xfer 通常根据组件声明机械生成，不再复制 Work 的条件、循环或计算。Work 已经通过 state 的 written 标志、queue 的 staged 数据和待提交的 Pop 请求记录本拍修改，Xfer 只调用相应的提交或推进接口：

1. 每个本模块拥有的 state 调用一次 `Xfer()`。
2. 调用 `AdvanceInnerQueues()`，由框架处理本地及委派的 queue 推进，包括提交消费请求和按延迟配置发布写入数据。
3. 调用 `XferChildren()`。

父模块自身 Work 逻辑结束后调用一次 `WorkChildren()`。局部阻塞可以从辅助函数返回，但不能跳过 children 的执行。Xfer 不查询 queue 就绪条件，不访问其他 owner 的 state。串行和并行共用这一骨架，保留 Work 完成后的屏障及 Xfer 完成后的屏障。

### 7.1 行为转换示例

下面使用标量消息，展示一个读取输入、计算结果、写输出并更新计数的模块。输入输出 queue 由外部父模块拥有，示例模块自身没有子模块或内部 queue。

```text
module Transform:
  input in: queue<u32>
  output out: queue<u32>
  state count: u32 = 0

  work:
    if !in.empty && !out.full:
      x = in.front
      y = x + count.read
      out.write(y)
      in.pop
      count.write(count.read + 1)
```

以下为按模板展开的声明与实现，实际生成时分别放入 `Transform.h` 和 `Transform.cpp`，并生成相邻模块文档。这里展示完整生命周期，但不包含外部 queue、驱动器或仿真入口。

```cpp
#include <cstdint>
#include "SimModule.h"
#include "SimState.h"

class Transform final : public gfsim::SimModule {
public:
    Transform(std::string name, gfsim::SimQueue<std::uint32_t>* in,
              gfsim::SimQueue<std::uint32_t>* out);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    INPUT gfsim::SimQueue<std::uint32_t>* in_;
    OUTPUT gfsim::SimQueue<std::uint32_t>* out_;
    gfsim::SimState<std::uint32_t> count_;

    void Execute();
    PURE static std::uint32_t Compute(std::uint32_t x, std::uint32_t count)
    { return x + count; }
};

Transform::Transform(std::string name,
                     gfsim::SimQueue<std::uint32_t>* in,
                     gfsim::SimQueue<std::uint32_t>* out)
    : SimModule(name), in_(BindInput(in)), out_(BindOutput(out)),
      count_(name + ".count", 0) {}

void Transform::Build()
{
    SetObjectCapacity(0); // state 不计入容量，边界 queue 由父模块拥有
    count_.Build();
    BuildInnerQueues();
    BuildChildren();
}

void Transform::Execute()
{
    if (in_->Empty() || out_->Full()) {
        return;
    }

    const std::uint32_t y = Compute(in_->Peek(), count_.Read());
    out_->Write(y);
    in_->Pop(); // 提出消费请求，本拍可见队列不变
    count_.Write(count_.Read() + 1);

    GetSim()->MarkProgress(); // 只有实际完成传输才算进展
    Log().Debug(Name(), "transfer") << "output=" << y;
}

void Transform::Work()
{
    Execute();
    WorkChildren();
}

void Transform::Xfer()
{
    count_.Xfer();
    AdvanceInnerQueues();
    XferChildren();
}

void Transform::Reset()
{
    count_.Reset();
    ResetInnerQueues();
    ResetChildren();
}

void Transform::ReportStat()
{
    Report().ReportVal("Transfers since reset (u32, modulo 2^32)", count_.Read());
    count_.ReportStat();
    ReportChildren();
}
```

计数表示自 Reset 以来成功传输的条目数，按 u32 回绕；阻塞拍不计数。该模块本身没有有限工作量或终止条件，由外部 testbench 确认预期输出后请求结束，并选择适合完整模型的无进展阈值。这里没有为执行逻辑引入额外模块、queue 或独立调度任务。

## 8. 直接转换的语义前提

从 GFIR 到 GFSIM 可以采用直接映射，但 IR 或前置 lowering 必须先明确操作语义、控制流顺序和事务边界。生成器不从连接图推断这些行为，也不自行插入仲裁来解决模型中的并发写冲突。

| 操作 | GFIR 对接采用的目标语义 |
| --- | --- |
| `state.Read()` | 返回 current；同拍 Write 后仍读取旧值 |
| `state.Write(value)` | 写 pending；owner Xfer 提交，下一拍可见；顺序多次写入取最后一次 |
| `queue.Front()` / `Peek()` | 查看拍初可见输入；同拍提出 Pop 后仍看到原队首 |
| `queue.Write(value)` | 写入 staged 区；在阶段推进中按延迟配置变为可见 |
| `queue.Pop()` | Work 中仅记录消费请求，不改变本拍可见队列；Xfer 阶段推进时提交删除 |
| `queue.Read()` | 返回可见队首并提出消费请求，不在 Work 中立即删除 |
| `queue.Empty()` / `Size()` | 查询拍初可见队列，不受本拍 Pop 或 Write 请求影响 |
| 普通 method 调用 | 在调用者线程同步执行，内部操作沿用对应组件的语义 |

Work 只对 state/queue 提出修改，本拍的 current 状态和可见队列保持不变。Pop 调用仍生成在 Work 中，实际删除由组件在系统 Xfer 阶段提交，不需要把调用点的控制流搬到模块 Xfer。新入队数据还须满足 queue 的延迟配置，不保证所有 Write 都在紧接着的下一拍可见。

例如，假设没有新的写入或其他修改：

```text
本拍开始：q = [A, B]
调用方 1：q.Pop()       → 记录消费请求，可见队列仍为 [A, B]
调用方 2：q.Front()     → 仍读到 A
Xfer 提交消费请求       → q = [B]
下一拍：q.Front()       → 读到 B
```

若拍初只有 A，本拍 Pop 后 `Empty()` 仍为 false，下一拍才变为 true。这描述的是同一 queue 的查询可见性，不表示允许多个独立消费者同时提交 Pop。其他模块通过 method 查询 queue 时同样遵守这份可见性约定；具体允许的并发查询接口还须满足组件访问契约。

Pop 释放的容量不能用于本拍写入；生产者本拍成功的 Write 仍会消耗写入额度，因此 `Full()` 可以随本拍 Write 改变。稳定的可见队列不意味着所有 API 返回值都固定，也不意味着对 shared_ptr 指向的 payload 自动生成深拷贝快照。

生成行为还须保留以下约束：

- 队列事务先检查全部输入、输出容量和其他阻塞条件，再查看输入、计算、提出全部输出写入请求，最后提出输入 Pop 请求。输出阻塞时不得提出消费请求或修改输入消息。
- 同一输出需要写 N 次时，预先检查 `Full(N - 1)`；有多个事务时按 IR 明确的顺序和条件执行。
- 示例中的 u32 直接按值传递。类或结构体 queue payload 使用 `SimQueue<T>`，消息由源头创建 `shared_ptr<T>`，后续转发共享句柄，不为交接复制整个 payload。
- 不同 worker 可以写不同 state 实例，同一实例的并发写由模型避免；结构体 state 的不同成员也不能视为独立写入单元。
- method 操作 queue 时仍受单生产者、单消费者和现有并行访问规则约束。module 引用不替代 queue 的端点绑定。
- 每条 queue 每拍最多一次 Pop/Read；第二次请求（包括混合调用）在修改状态前由 `GFSIM_ASSERT` 报错。没有批量消费或消费游标；不能通过 `while (!Empty()) Pop()` 在同一 Work 中逐项排空拍初队列，因为 Empty 不随请求改变。
- 其他 worker 在 Work 中可以通过 method 只读查询 Front/Peek/Empty/Size；这些查询不访问消费标志。可见结构稳定不意味着 payload 不可变；并发查询共享 payload 时必须禁止并发修改，成功写入输出也不解除这项约束。
- Front/Peek 引用在提出消费请求后仍有效，但不得跨 queue 推进、flush 或 Reset 保留；需要延长消息生命期时单独持有共享句柄。
- FlushIf 是即时维护接口，只能在系统静止、独占访问且没有待消费请求时调用；有效 RID 的调用在谓词执行和修改前断言无待消费请求，RID 参数校验仍保留。原始容器不得在 Work 中修改可见结构，涉及 staged 数据的查询仍遵守独占访问约定。
- queue 推进先提交消费、再刷新写入额度，最后按延迟规则发布 staged 数据；stall 只阻止延迟推进和发布。完成条件使用已完成事务计数或下一次 Work 查询，不把队列检查移入模块 Xfer。参见 [延迟消费决策](0019-delayed-queue-consumption.md)。
- 普通计算的位宽、符号、溢出及其他数值语义须由 lowering 与目标 C++ 类型或辅助函数匹配，不能直接依赖 C++ 未定义行为。

更具体的 GFIR op 编码、method 签名表达及特殊操作的 lowering 可以继续补充。当前已确定的转换方式是：直接映射声明、操作和控制流，并补齐固定的生命周期骨架。

## 9. module 内部的定长 table

GFIR 的 table 可以直接声明在 module op 内，用于承载一组同类型的 module 或基础组件。生成器将它展开为该模块的固定长度数组成员，例如 `std::array<T, N>`。数组只是组织和索引元素的容器，不必生成新的 SimModule、SimObj 或独立调度任务。

需要区分两种用途：

- 内部声明：本模块拥有 N 个元素，在内部通过索引访问。
- 连接引用：其他模块接收指向这张 table 的非 owning 指针，在运行时按索引选择目标并调用 method 或基础组件 API。

同一张内部 table 可以同时用于本地访问和对外连接，不要求为了传递引用再创建一个容器模块。长度 N 由 IR 在生成时确定，元素身份和连接在 Build 后固定；访问索引可以由每拍输入或状态动态计算。

### 9.1 声明示例与元素管理

```text
module Bank:
  table servers: Server[4]
  table counters: state<u32>[4], initial=0
  table buffers: queue<Packet>[4], capacity=8

  work:
    i = 根据本拍输入计算，保证 0 <= i < 4
    counters[i].write(counters[i].read + 1)

module Client:
  reference targets: table<Server, 4>

  work:
    i = 根据本拍输入计算，保证 0 <= i < 4
    value = targets[i].Query()

父层连接：client.targets -> bank.servers
```

这段伪代码只说明 table 声明和引用形状；Server 的构建参数、buffers 各元素的生产者与消费者仍须由 IR 给出，不能留下未连接的物理 queue。

| 元素类型 | 建议的内部成员形式 | 所有权、登记与生命周期 |
| --- | --- | --- |
| module | `std::array<Server*, N>`，保存访问指针 | Build 中逐个创建并 `AddSubmodule()`；父模块集合保存所有权，标准 children helper 负责生命周期 |
| state | `std::array<SimState<T>, N>`，直接嵌入实例 | 每个元素显式命名和初始化；owner 逐个调用 Build、Xfer、Reset、ReportStat，不注册到对象图 |
| queue | `std::array<SimQueue<T>*, N>`，保存访问指针 | Build 中逐个创建并 `AddInnerQueue()`；每条 queue 独立绑定端点，通过框架统一推进 |

例如，以下为 state 数组的成员声明片段，直接按元素初始化，不要求 SimState 或 T 具有默认构造函数：

```cpp
#include <array>

using CounterTable = std::array<gfsim::SimState<std::uint32_t>, 2>;
CounterTable counters_{{
    {"bank.counters[0]", 0},
    {"bank.counters[1]", 0}
}};
```

实际生成时将该成员放入 owner 的 private 区域，并按 owner 实例名称生成稳定的元素名称。owner 的 Build 在声明对象容量后遍历 state 元素调用 Build，Xfer 遍历调用 Xfer，Reset 和 ReportStat 同理。state 数组没有额外的生命周期入口，也不能仅初始化第一个元素而遗漏其余元素。

对象容量按物理元素计数：上例 Bank 若直接拥有 4 个 Server、4 条 queue 和 4 个 state，则容量为 8；table 容器本身和 state 元素不计入容量。数组之外的其他直接子模块或 queue 仍需另行计入。已通过 children/queue helper 管理的元素不能再手工逐个执行相同阶段，避免重复调度。

### 9.2 通过数组指针表达连接

对于 module 和 queue，内部访问数组本身就可以作为连接视图传给其他模块，不需要复制目标对象。对于直接嵌入的 state 数组，可以传入该数组的指针并按索引访问实例，也可以在需要统一指针形式时生成固定的元素指针数组。

下面使用第 4 节已有的 Consumer 类型，展示 module table 的引用声明和绑定。与前面的连接示例一样，这段代码仅展示连接部分，省略生命周期函数体；正式生成仍使用模块模板及独立文件。

```cpp
using ConsumerTable = std::array<Consumer*, 4>;

class TableClient final : public gfsim::SimModule {
public:
    TableClient(std::string name, const ConsumerTable* targets)
        : SimModule(name), targets_(targets)
    {
        GFSIM_ASSERT(targets_ != nullptr);
        for (Consumer* target : *targets_) {
            BindModule(target);
        }
    }

    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    const ConsumerTable* const targets_;

    Consumer* Select(std::size_t index) const
    {
        GFSIM_ASSERT(index < targets_->size());
        return (*targets_)[index];
    }
};
```

Work 中可以调用 `Select(i)->SomeMethod(...)`，其中 SomeMethod 是目标类型实际声明的方法。`const ConsumerTable*` 禁止调用方改写数组中的连接，但并不把 Consumer 对象变成只读；若只允许查询，应使用元素类型为 `const Consumer*` 的数组。

当前 BindModule 接受单个 module 指针，所以生成器逐个登记所有可能被索引到的目标。上例假定四个元素是不同的有效实例；若 IR 允许多个位置引用同一实例，生成器应去重登记。对外只连接一张 table，不意味着对象图中只登记一个不透明目标。

state 数组引用不调用 BindModule，也不新增 state 注册。queue 数组按输入或输出用途逐元素调用 BindInput/BindOutput，仍须保证每条物理 queue 的唯一生产者与消费者；数组连接不能增加隐藏的生产端或消费端。

内部 table 和元素必须在使用它们的引用存活期间保持有效。不能传出 Build 中局部数组的地址。若元素在 owner Build 中才创建，应在填充完成后连接调用方；跨层级或循环引用可采用构建期显式连接，不能对尚为空的候选元素提前 BindModule。

### 9.3 动态索引与并行约束

固定的是数组长度、元素身份和连接，不是每次访问的索引。每次访问须保证索引合法，生成器可在不能静态证明时生成 `GFSIM_ASSERT`。仿真期间不能 resize、增删元素或更换元素指针；Reset 恢复元素内容，不重建连接。

数组不会改变元素的执行语义：method 仍在调用者线程执行，state 由 owner 提交，queue 按系统安排推进，并遵守第 8 节的目标可见性语义。调用方持有数组指针不承担元素的 Build、Xfer 或 Reset。

不同 worker 访问不同元素可以沿用现有并发约定；动态索引落到同一个元素时，必须检查该操作是否允许并发。例如两个 worker 对 `counters[i]` 和 `counters[j]` Write，在 i 等于 j 时仍是同一个 state 的并发写冲突。定长容器和 BindModule 都不提供锁或仲裁，模型负责避免非法冲突。

table 不新增拓扑节点或划分任务的层级。module/queue 元素沿用现有图节点和所有权关系，state 元素不进入图；module table 的引用展开成到各候选目标的 method 边，不记录运行时索引。当前 worker 仍按选定父模块的直接子树分配，不会因为数组引用自动把调用方和目标放在一起。

## 参考

- [模块模板及 API](../API/SimModule.md)
- [queue API](../API/SimQueue.md)
- [module 引用与普通 method](0017-module-method-bindings.md)
- [嵌入式 state](0016-embedded-state.md)
- [拓扑图 API](../API/SimGraph.md)
