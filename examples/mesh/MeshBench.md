# 40 节点 Mesh 单线程示例

40 个节点组成 8 列 × 5 行的二维 mesh，边界不回绕。每个节点向其余 39 个节点各发一个包。当前基线在第 **132 拍**完成，收发各 **1,560 包**，共 **6,760 次节点间转发**。平均延迟为 **12.34 拍/包**，从成功注入本地队列计到 testbench 读出交付队列。

## 运行与逐包对照

在仓库根目录执行以下命令。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mesh_example
./build/mesh_example
```

要保存基线，可在运行时指定 TSV 输出文件。

```sh
./build/mesh_example --timings build/mesh-timings.tsv
```

每行记录一个包的 `packet_id`、`injected_cycle`、`delivered_cycle`，按 ID 排序，首行为列名。相同流量下，可直接比较两个版本导出的文件。包 0 从节点 0 发往节点 1，第 1 拍注入，第 2 拍进入链路，第 3 拍进入交付队列，第 4 拍由 testbench 验收。

## 拓扑与工作负载

顶层创建 40 个 `MeshNode` 实例，传入各自的编号和队列连接。同一对邻居的输出与输入共用同一个物理队列。生成器展开端口处理与顶层接线，节点逻辑集中在一个类中。边界方向传入空指针。[生成脚本](generate_mesh.py) · [节点实现](MeshNode.cpp)

普通 CMake 构建直接编译项目内的生成文件。修改生成规则或 `codegen/` 中的模板后，在仓库根目录执行：

```sh
python3 examples/mesh/generate_mesh.py
python3 examples/mesh/generate_mesh.py --check
```

`MeshBench` 派生自 `SimTestbench`。它拥有 40 个节点、134 条有向节点间链路和 80 条本地队列，共 254 个直接对象。所有队列容量为 2，延迟为 1 拍。[节点工作时序](MeshNode.md)

| 队列名 | 生产者 | 消费者 |
| --- | --- | --- |
| `link_A_to_B` | 节点 A | 相邻节点 B |
| `inject_A` | testbench | 节点 A 的本地输入 |
| `deliver_A` | 节点 A 的本地输出 | testbench |

节点编号为 `8*y + x`。每个源每拍最多注入一个包。从 0 开始的源内序号为 `seq`，目的地为 `(src + seq + 1) % 40`，包 ID 为 `39*src + seq`。本地队列满时保留序号，成功写入才记录注入周期。每个包由源分配一次，经共享指针逐跳转交。

每拍由 testbench 先尝试注入，再调用节点，最后从每个目的地最多取一个包。新写入的数据在拍末推进。交付时按实际端口验证源、目的、ID、唯一性以及曼哈顿跳数。注入、转发、交付成功均标记进展；验收全部 1,560 个包后结束。连续 32 拍无进展报告死锁。复位清空队列、收发记录和统计量。

## 观察与测试

报告包含收发包数、仿真周期、已交付包的总跳数、平均延迟及各节点的转发次数。默认关闭逐包日志；Debug 级别记录注入、阻塞、转发和验收。开启框架 trace 时，队列快照记录包 ID、源、目的和跳数，默认逐拍采样。

```sh
cmake --build build --target mesh_example_test
ctest --test-dir build -R '^mesh_example$' --output-on-failure
```

测试将拓扑导出到 `build/mesh.dot`，覆盖轮询争用、反压、永久阻塞和复位。它在正常与反向节点注册顺序下逐包比对时序，并核对所有包已交付、所有队列均已排空。
