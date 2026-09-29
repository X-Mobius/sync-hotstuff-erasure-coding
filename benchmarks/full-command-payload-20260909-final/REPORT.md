# 完整命令正文 RS 传播：测试与交付报告

> 归档说明（2026-09-29）：本报告记录 2026-09-09 的实验；文中未提交、未推送仅指实验完成时的状态。最新发布状态以 Git 历史为准。

本报告仅使用本次指定结果目录的数据；早期哈希列表版本的 28.4%/52.2% 不适用于本次完整正文实现。

## 版本与范围

- 分支：`codex/full-command-payload`；基础提交 `49e14966d359f03e4bf2ec7206fa856bbf1d0b1e`，包含用户原有三份未提交 32 字节修复。
- 原修复备份：`/home/elysia/full-command-prechange-h6GICFn0/working.patch`。未修改 main，未推送或合并。
- baseline：原始提交 `c636b28c6f0cdff2092cd4b31add0219323b21f0` 的独立副本，协议源码无 RS、Re-propose、quorum 覆盖或丢包注入；只增加发送字节统计。
- 三种性能模式均为 7 个本地进程、quorum=4（f=3），RS 模式 k=4；单 Ubuntu VM。客户端使用同一确定性工作负载程序。
- 性能正文为原版固定线格式 `cid:u32 + sequence:u32 + REQSIZE bytes`；REQSIZE 分别 1024/16384/65536，完整命令额外有 8 字节标识。每种长度双方使用相同编译宏。
- batch=1/16/64；对应客户端窗口=16/64/128；每组 30 秒、重复 3 次；native、full-broadcast、RS 三种模式交替串行运行。
- full-broadcast 与 RS 都由 `HOTSTUFF_CLIENT_LEADER=0` 使用相同的仅 Leader 正文入口。协议通过 PaceMaker 查询 Leader，客户端暂未实现自动跟随 view change；较大集群 n>7 未经端到端验证。

## 正确性

| 模式 | f | 活动副本 | 轮数 | 每轮 confirmed | 哈希失败 | 全部活动副本存活轮数 |
|---|---:|---|---:|---|---:|---:|
| shadow | 2 | 0 1 2 3 4 5 6 | 3 | 576, 576, 576 | 0 | 3 |
| strict | 2 | 0 1 2 3 4 5 6 | 3 | 576, 576, 576 | 0 | 3 |
| strict | 2 | 0 3 4 5 6 | 3 | 576, 576, 576 | 0 | 3 |
| strict | 3 | 0 4 5 6 | 3 | 576, 576, 576 | 0 | 3 |
| full-broadcast | 2 | 0 1 2 3 4 5 6 | 3 | 592, 592, 592 | 0 | 3 |
| strict | 2 | 0 6 | 1 | 0 | 0 | 1 |

- `test_command_batch`：固定/动态命令往返，0/1/31/32/33 字节及 1/16/64 KiB 边界；944 组 RS 子集，包含缺少 systematic shard；截断、尾随、声明长度错误和单字节篡改拒绝。
- `test_full_proposal`：签名消息每个截断位置、每个字节翻转及尾随数据均拒绝；未认证头部不会写入区块存储。
- `test_full_command_state`：真实节点入口测试重复、逆序、同索引冲突、元数据冲突、旧 view、多个 origin、失败重构和有效分片重试。故意篡改的测试预期记录一次 payload hash failure，不计入正常场景失败数。
- 分片不足负向实验：只运行 0/6 两节点、k=3，重构、Vote、confirmed 均为 0，两个进程保持运行。
- 最终完整 ASan（包含协议和网络库对象，detect_leaks=1）三个测试均通过，未报告内存错误或泄漏。此前完整库检查曾发现 Salticidae libuv 句柄释放类型不匹配；已增加按实际类型释放的补丁，修复前日志保留。

## 性能对照

以下延迟为各轮均值/P50/P95 的跨轮平均；字节/confirmed 使用各轮字节合计除以确认数合计，不把无确认轮次当成零成本。

| 正文 KiB | batch | native ops/s | full-broadcast ops/s | RS ops/s | native P95 s | full-broadcast P95 s | RS P95 s |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 7.468 | 7.591 | 7.603 | 2.0223 | 2.0196 | 2.0275 |
| 1 | 16 | 29.784 | 30.185 | 29.963 | 2.0156 | 2.0121 | 2.0152 |
| 1 | 64 | 59.624 | 59.542 | 59.614 | 2.0275 | 2.0263 | 2.0275 |
| 16 | 1 | 7.499 | 7.461 | 7.524 | 2.0212 | 2.0190 | 2.0316 |
| 16 | 16 | 29.781 | 29.773 | 29.784 | 2.0184 | 2.0364 | 2.0282 |
| 16 | 64 | 59.576 | 59.673 | 59.604 | 2.0345 | 2.0784 | 2.0408 |
| 64 | 1 | 7.476 | 7.465 | 7.543 | 2.0226 | 2.0298 | 2.0414 |
| 64 | 16 | 29.800 | 29.810 | 29.806 | 2.0332 | 2.0928 | 2.0915 |
| 64 | 64 | 59.624 | 59.602 | 58.161 | 2.0939 | 2.1837 | 2.1973 |

Leader 压力的公平对照是 full-broadcast → RS；两者客户端都只向稳定 Leader 发送正文。下表字节均为实测网络发送字节/confirmed，降幅为 `(full-broadcast - RS) / full-broadcast`。

| 正文 KiB | batch | Leader Proposal B/cmd | Leader Proposal 降幅 | Leader 全出口 B/cmd | Leader 全出口降幅 | Re-propose B/cmd | 集群 B/cmd |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 10620.1 → 5764.7 | 45.72% | 11629.9 → 12539.0 | -7.82% | 0.0 → 40352.9 | 17687.9 → 53185.3 |
| 1 | 16 | 7094.3 → 2107.2 | 70.30% | 7240.6 → 4360.8 | 39.77% | 0.0 → 14750.7 | 8118.4 → 17882.3 |
| 1 | 64 | 6923.8 → 1931.2 | 72.11% | 7026.6 → 3965.3 | 43.57% | 0.0 → 13518.6 | 7643.5 → 16169.6 |
| 16 | 1 | 109375.3 → 30440.2 | 72.17% | 110386.1 → 61890.7 | 43.93% | 0.0 → 213081.6 | 116451.5 → 250594.1 |
| 16 | 16 | 105842.7 → 26793.8 | 74.69% | 105989.1 → 53733.9 | 49.30% | 0.0 → 187556.4 | 106867.2 → 215374.7 |
| 16 | 64 | 105666.7 → 26616.9 | 74.81% | 105769.5 → 53336.7 | 49.57% | 0.0 → 186318.6 | 106386.4 → 213655.2 |
| 64 | 1 | 425352.4 → 109356.1 | 74.29% | 426363.3 → 219722.2 | 48.47% | 0.0 → 765492.6 | 432428.8 → 881917.9 |
| 64 | 16 | 421819.8 → 105788.1 | 74.92% | 421966.2 → 211722.5 | 49.82% | 0.0 → 740516.4 | 422844.4 → 847329.0 |
| 64 | 64 | 421643.8 → 105782.9 | 74.91% | 421746.6 → 211669.5 | 49.81% | 0.0 → 735672.2 | 422361.9 → 842174.8 |

native baseline 仅用于描述原项目的真实端到端结构：客户端向所有副本广播正文，Leader Proposal 主要携带哈希。它不是 Leader 完整正文复制的基准，不能用于计算 RS 的 Leader 出口收益。

## 理论估算、应用线格式与实测

- 理论正文量（忽略签名、头部和控制消息）：n=7、k=4 时，full-broadcast 的 Leader 向 6 个远端副本发送约 `6P`；当前 RS Leader 定向发送 6 个约 `P/4` 的 shard，并把自己的 shard Re-propose 给 6 个副本，约为 `3P`，因此 Leader 正文总出口的渐近预期降幅约 50%。只看定向 Proposal 则约为 `1.5P`，相对 `6P` 的渐近降幅约 75%。
- 应用线格式按 `version/count/length/command bytes` 精确序列化；RS shard 向 32 字节边界取整。这个尺寸不含 Proposal、证书或网络帧头。
- 上表是实际 `wire_tx` 统计并按 confirmed 归一化，包含协议头、签名、在途命令及控制消息。因此小 batch 会偏离理论值，不能把理论估算与实测网络字节混写。

## 统计口径

- `wire_tx` 在统一网络发送入口记录成功入队消息的序列化长度，包含 Salticidae 帧头；覆盖 Proposal、Re-propose、Vote、QC/Notify、控制消息、客户端请求/订阅和确认响应。不是抓包统计，不含 TCP/IP、ACK、重传、TLS 记录或以太网开销。
- Leader Proposal：副本网络 opcode 0 的发送字节；Leader 全消息出口：Leader 进程所有网络发送；集群总发送：所有副本进程所有网络发送，含发给客户端的确认。
- 客户端正文：完整 CommandDummy 字节，不含帧头；客户端总发送含帧头和哈希订阅。端到端总发送=集群总发送+客户端总发送，避免把客户端正文再加一次。
- Re-propose 单独统计副本网络 opcode 7；客户端网络 opcode 7 是不同网络上的哈希订阅，不计入 Re-propose。
- 包含 3 秒节点启动窗口的控制消息；吞吐分母是实际客户端运行时间。保留末尾在途命令，因此 bytes/confirmed 含未完成请求的发送成本，双方口径一致。
- CPU 是活动副本 `/proc` 累计 CPU 秒及约 100 ms 采样峰值（可超过 100%）；内存是活动副本 RSS 合计采样峰值，不是系统内核记录的精确峰值，也不含客户端/操作系统。
- RS encode/decode 是单独 RS 调用微秒累计；decode_verify 另含恢复、哈希、命令解析与缓存校验。所有具体数值保留在原始与汇总 CSV。

## 调用链

`CommandDummy → MsgReqCmd → HotStuffApp::client_request_cmd_handler → remember_command → exec_command(cmd_hash) → cmd_pending → PaceMaker::beat → HotStuffCore::on_propose → make_command_batch → rs_encode_shards → 签名 Proposal → do_send_erasure_proposals → propose_handler → accept_body_fragment → broadcast_reproposal → repropose_handler → 收齐 k 片 → rs_reconstruct_data → payload SHA-256 → decode_command_batch → 每条 CommandDummy 完整解析/规范字节比较/SHA-256 → 原始 Block 重建及哈希/QC 检查 → reconstructed_commands → on_deliver_blk（同步交付并重试乱序依赖） → on_receive_proposal → _vote → QC/Commit timer → check_commit → do_decide → Finality → 客户端唯一副本确认计数。`

Leader 同样收集分片并恢复后投票。Strict 中其他副本只接收 `MsgWatchCmd(cmd_hash)` 订阅；订阅不含正文。旧普通 Proposal、整块请求和整块响应不能进入新路径；`_vote` 与 Commit 均检查恢复标记。

full-broadcast 分支不调用 RS encode/decode：Leader 生成同一规范 batch，以 flag=3 分别签名并定向发送；Follower 解析、逐命令校验并重建原 Block。Leader 自身的完整正文通过本地事件队列延迟验证，避免在 `on_propose` 内同步恢复/投票造成重入；该自验证不产生网络发送或 Re-propose。

## 消息与序列化

- Proposal flag=0/1 保留历史普通/哈希分片路径；flag=2 表示完整正文 RS；flag=3 表示仅实验使用的完整正文复制。公共头包含 proposer、n/k、shard index/size、payload size、command count、origin block hash、view、height、payload SHA-256 和原 Block 元数据（parents、按序 cmd_hash、QC、qc_ref_hash、extra）。
- 数据体是独立 raw shard 或完整 batch。Leader 的 secp256k1 证书分别以 0x42（RS）和 0x43（full-broadcast）域分隔，绑定公共头、原 Block 以及当前正文；flag=3 只有本地同时启用 full、Strict 和 `HOTSTUFF_BENCHMARK_FULL_BROADCAST=1` 才接受，不能由远端消息开启，也不产生 Re-propose。
- batch：`version:u32le=1 | count:u32le | repeated(length:u32le | serialized_command_bytes)`。SHA-256 固定 32 字节，无 `sizeof(uint256_t)` 或 `sizeof(CommandDummy)` 协议长度。
- 新增动态 Dummy 格式供变长测试使用：`cid:u32le | sequence:u32le | payload_length:u32le | payload`；由 `HOTSTUFF_COMMAND_PAYLOAD_BYTES` 显式选择。性能对照使用双方原有固定格式，保留兼容性。
- 拒绝超长、计数不符、声明/实际长度不符、未知版本、尾随数据、非规范命令字节、命令哈希不符、payload 哈希不符、Block 哈希/QC 不符或签名不符。

## 状态与工程边界

- 按 origin hash 分池；最多 64 个待恢复 batch、合计预留 shard 容量 64 MiB；30 秒超时，每 250 ms 清理并多轮排空已具备依赖的区块链，避免一层一层重试造成副本永久落后；旧 view 丢弃。
- 上限：4096 命令、单命令 1 MiB、batch 8 MiB、n≤32、k≤n、shard≤8 MiB；QC 位图最多 32 副本，分配前检查。正文缓存最多 8192 条/128 MiB，提交后释放。
- 已完成池最多 512 个 origin，按 HQC 高度保留约 128 层。缓存/池达到限额会拒绝后续输入；尚无生产级背压、持久化、长期离线同步和跨 view 正文重发方案。
- 签名阻止中继篡改诚实 Leader 的分片，但不是可验证 RS 编码证明；恶意 Leader 仍可签发相互不一致的分片。最终哈希检测错误，不定位错误分片，也不自动搜索全部可恢复子集。
- 离线实验是 crash/offline，不等同于拜占庭恶意分片容错证明；同机 7 进程实验不能替代多机带宽瓶颈、恶意 Leader、动态 view 和长期压力测试。
- 全互联 Re-propose 把正文成本转移到副本网络，集群总流量可能高于 full-broadcast。native baseline 原来由客户端把正文发给全部副本，因此比较 Leader 出口时不能假设原版 Leader 曾广播完整正文。不得把客户端正文发送减少表述为集群总通信减少。

## 可复现命令

```bash
cd ~/librightstuff
cmake . && make -j2
./test/test_command_batch
./test/test_full_proposal
./test/test_full_command_state
./test/test_reproposal_rs
bash scripts/run_full_command_asan.sh
python3 scripts/full_command_matrix.py --phase correctness --seconds 20 --repetitions 3 --out ~/NEW-UNUSED-correctness
python3 scripts/full_command_matrix.py --phase negative --out ~/NEW-UNUSED-negative
bash scripts/run_full_command_performance.sh
```

性能脚本需要 `~/full-command-baseline-path.txt` 指向由原始提交创建的统计专用 baseline；准备程序和依赖补丁包含在交付文件中。不要复用已有输出目录。

## 简历口径

可写：实现完整命令 batch 的 Reed-Solomon 定向分片、签名 Re-propose、阈值恢复、逐命令与区块哈希校验，以及 Leader/副本统一的 Vote 前置约束；在 7 节点下关闭 2/3 个 systematic shard 节点，分别连续 3 轮推进共识，正常与缺节点场景哈希失败为 0。

可写：设计同入口的 full-payload replication 公平对照；在 7 节点、64 KiB 命令、batch=64 的 3×30 秒实验中，RS 相比完整复制将 Leader Proposal 字节/confirmed 降低 74.91%、Leader 全消息出口降低 49.81%，同时如实记录全互联 Re-propose 使集群发送字节上升约 99.39%，吞吐降低约 2.42%。

不得沿用哈希列表版本的出口降幅，不得宣称已降低集群总通信量、完成多机验证或容忍恶意 Leader。

## 建议提交顺序

1. Fix 32-byte uint256 wire serialization
2. Propagate full command batches with authenticated RS shards
3. Fix Salticidae libuv handle destruction
4. Add full-command benchmark accounting
5. Add full-payload replication comparison

## 原始证据

- 性能原始目录：`/home/elysia/full-command-performance-IXOhtXw9`
- 正确性原始目录：`/home/elysia/full-command-final-correctness2-lQIme6RR`
- 分片不足负向目录：`/home/elysia/full-command-final-negative-MnxnLSeE`
- 最终 ASan 目录：`/home/elysia/full-command-asan-uNAo2QIY`
- 最终汇总原始目录：`/home/elysia/full-command-final-report-Uyv7AlMz`
- 修改前父仓库和子模块备份：`/home/elysia/librightstuff-backups/full-broadcast-20260909-095100`
- `raw.csv`：正式性能原始行；`deduplicated.csv`：唯一实验键；`aggregate.csv`：跨轮汇总；`correctness.csv`：正确性矩阵。
- 源码变更和依赖补丁分别保存在交付目录；当前仍未提交，建议审查后分别提交原有 32 字节修复、完整正文协议/测试、依赖内存修复及统计脚本。
