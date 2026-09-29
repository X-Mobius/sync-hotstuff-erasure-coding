#!/usr/bin/env python3
import argparse,csv,collections,json,pathlib,statistics,subprocess

def write_csv(path,rows):
    if not rows:return
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)

ap=argparse.ArgumentParser();ap.add_argument('performance',type=pathlib.Path);ap.add_argument('correctness',type=pathlib.Path);ap.add_argument('out',type=pathlib.Path);args=ap.parse_args()
args.out.mkdir(parents=True,exist_ok=True)
raw=[]
for p in sorted(args.performance.glob('payload-*/raw.csv')):
    raw+=list(csv.DictReader(p.open()))
write_csv(args.out/'raw.csv',raw)
dedup={}
for row in raw:
    key=tuple(row[k] for k in ['mode','payload_bytes','batch','repeat','nfaulty','active'])
    if key in dedup:raise SystemExit('Duplicate official performance run; resolve provenance before reporting')
    dedup[key]=row
rows=list(dedup.values());write_csv(args.out/'deduplicated.csv',rows)
groups=collections.defaultdict(list)
for row in rows:groups[(row['mode'],int(row['payload_bytes']),int(row['batch']))].append(row)
aggregate=[]
for (mode,payload,batch),rs in sorted(groups.items()):
    count=sum(int(x['confirmed']) for x in rs)
    item=dict(mode=mode,payload_bytes=payload,batch=batch,runs=len(rs),confirmed_sum=count,
        throughput_mean=statistics.mean(float(x['throughput']) for x in rs),
        throughput_sd=statistics.stdev(float(x['throughput']) for x in rs) if len(rs)>1 else 0,
        mean_latency=statistics.mean(float(x['mean_latency']) for x in rs),
        p50_mean=statistics.mean(float(x['p50']) for x in rs),p95_mean=statistics.mean(float(x['p95']) for x in rs),
        hash_failures=sum(int(x['hash_failures']) for x in rs),
        all_alive_runs=sum(int(x['all_replicas_alive']) for x in rs),
        continuous_runs=sum(int(x['continuous_progress']) for x in rs),
        all_replicas_reconstructing_runs=sum(int(x.get('all_replicas_reconstructing',1)) for x in rs),
        unsafe_votes=sum(int(x['unsafe_votes']) for x in rs),
        reconstructed=sum(int(x['reconstructed']) for x in rs))
    for metric in ['leader_proposal_bytes','reproposal_bytes','leader_all_wire_bytes','cluster_all_wire_bytes','client_body_bytes','client_all_wire_bytes','end_to_end_bytes']:
        item[metric+'_sum']=sum(int(x[metric]) for x in rs)
        item[metric+'_per_confirmed']=item[metric+'_sum']/count if count else 0
    for metric in ['rs_encode_us','rs_decode_us','rs_decode_verify_us','cpu_seconds','cpu_peak_percent','replica_rss_peak_bytes']:
        item[metric+'_mean']=statistics.mean(float(x[metric]) for x in rs)
    aggregate.append(item)
write_csv(args.out/'aggregate.csv',aggregate)
cor=list(csv.DictReader((args.correctness/'raw.csv').open()));write_csv(args.out/'correctness.csv',cor)
cg=collections.defaultdict(list)
for x in cor:cg[(x['mode'],x['nfaulty'],x['active'])].append(x)
lines=['# 完整命令正文 RS 传播：测试与交付报告','',
'本报告仅使用本次指定结果目录的数据；早期哈希列表版本的 28.4%/52.2% 不适用于本次完整正文实现。','',
'## 版本与范围','',
'- 分支：`codex/full-command-payload`；基础提交 `49e14966d359f03e4bf2ec7206fa856bbf1d0b1e`，包含用户原有三份未提交 32 字节修复。',
'- 原修复备份：`/home/elysia/full-command-prechange-h6GICFn0/working.patch`。未修改 main，未推送或合并。',
'- baseline：原始提交 `c636b28c6f0cdff2092cd4b31add0219323b21f0` 的独立副本，协议源码无 RS、Re-propose、quorum 覆盖或丢包注入；只增加发送字节统计。',
'- 三种性能模式均为 7 个本地进程、quorum=4（f=3），RS 模式 k=4；单 Ubuntu VM。客户端使用同一确定性工作负载程序。',
'- 性能正文为原版固定线格式 `cid:u32 + sequence:u32 + REQSIZE bytes`；REQSIZE 分别 1024/16384/65536，完整命令额外有 8 字节标识。每种长度双方使用相同编译宏。',
'- batch=1/16/64；对应客户端窗口=16/64/128；每组 30 秒、重复 3 次；native、full-broadcast、RS 三种模式交替串行运行。',
'- full-broadcast 与 RS 都由 `HOTSTUFF_CLIENT_LEADER=0` 使用相同的仅 Leader 正文入口。协议通过 PaceMaker 查询 Leader，客户端暂未实现自动跟随 view change；较大集群 n>7 未经端到端验证。','',
'## 正确性','',
'| 模式 | f | 活动副本 | 轮数 | 每轮 confirmed | 哈希失败 | 全部活动副本存活轮数 |','|---|---:|---|---:|---|---:|---:|']
for (mode,f,active),rs in cg.items():
    lines.append(f"| {mode} | {f} | {active} | {len(rs)} | {', '.join(x['confirmed'] for x in rs)} | {sum(int(x['hash_failures']) for x in rs)} | {sum(int(x['all_replicas_alive']) for x in rs)} |")
lines += ['',
'- `test_command_batch`：固定/动态命令往返，0/1/31/32/33 字节及 1/16/64 KiB 边界；944 组 RS 子集，包含缺少 systematic shard；截断、尾随、声明长度错误和单字节篡改拒绝。',
'- `test_full_proposal`：签名消息每个截断位置、每个字节翻转及尾随数据均拒绝；未认证头部不会写入区块存储。',
'- `test_full_command_state`：真实节点入口测试重复、逆序、同索引冲突、元数据冲突、旧 view、多个 origin、失败重构和有效分片重试。故意篡改的测试预期记录一次 payload hash failure，不计入正常场景失败数。',
'- 分片不足负向实验：只运行 0/6 两节点、k=3，重构、Vote、confirmed 均为 0，两个进程保持运行。',
'- 最终完整 ASan（包含协议和网络库对象，detect_leaks=1）三个测试均通过，未报告内存错误或泄漏。此前完整库检查曾发现 Salticidae libuv 句柄释放类型不匹配；已增加按实际类型释放的补丁，修复前日志保留。','',
'## 性能对照','',
'以下延迟为各轮均值/P50/P95 的跨轮平均；字节/confirmed 使用各轮字节合计除以确认数合计，不把无确认轮次当成零成本。','',
'| 正文 KiB | batch | native ops/s | full-broadcast ops/s | RS ops/s | native P95 s | full-broadcast P95 s | RS P95 s |','|---:|---:|---:|---:|---:|---:|---:|---:|']
lookup={(x['mode'],x['payload_bytes'],x['batch']):x for x in aggregate}
for payload in [1024,16384,65536]:
    for batch in [1,16,64]:
        native=lookup.get(('baseline',payload,batch));broadcast=lookup.get(('full-broadcast',payload,batch));rs=lookup.get(('strict',payload,batch))
        if not native or not broadcast or not rs:continue
        lines.append(f"| {payload//1024} | {batch} | {native['throughput_mean']:.3f} | {broadcast['throughput_mean']:.3f} | {rs['throughput_mean']:.3f} | {native['p95_mean']:.4f} | {broadcast['p95_mean']:.4f} | {rs['p95_mean']:.4f} |")
lines+=['','Leader 压力的公平对照是 full-broadcast → RS；两者客户端都只向稳定 Leader 发送正文。下表字节均为实测网络发送字节/confirmed，降幅为 `(full-broadcast - RS) / full-broadcast`。','','| 正文 KiB | batch | Leader Proposal B/cmd | Leader Proposal 降幅 | Leader 全出口 B/cmd | Leader 全出口降幅 | Re-propose B/cmd | 集群 B/cmd |','|---:|---:|---:|---:|---:|---:|---:|---:|']
for payload in [1024,16384,65536]:
    for batch in [1,16,64]:
        broadcast=lookup.get(('full-broadcast',payload,batch));rs=lookup.get(('strict',payload,batch))
        if not broadcast or not rs:continue
        bp=broadcast['leader_proposal_bytes_per_confirmed'];rp=rs['leader_proposal_bytes_per_confirmed']
        bl=broadcast['leader_all_wire_bytes_per_confirmed'];rl=rs['leader_all_wire_bytes_per_confirmed']
        pred=100*(bp-rp)/bp if bp else 0;lred=100*(bl-rl)/bl if bl else 0
        lines.append(f"| {payload//1024} | {batch} | {bp:.1f} → {rp:.1f} | {pred:.2f}% | {bl:.1f} → {rl:.1f} | {lred:.2f}% | {broadcast['reproposal_bytes_per_confirmed']:.1f} → {rs['reproposal_bytes_per_confirmed']:.1f} | {broadcast['cluster_all_wire_bytes_per_confirmed']:.1f} → {rs['cluster_all_wire_bytes_per_confirmed']:.1f} |")
lines+=['','native baseline 仅用于描述原项目的真实端到端结构：客户端向所有副本广播正文，Leader Proposal 主要携带哈希。它不是 Leader 完整正文复制的基准，不能用于计算 RS 的 Leader 出口收益。','',
'## 统计口径','',
'- `wire_tx` 在统一网络发送入口记录成功入队消息的序列化长度，包含 Salticidae 帧头；覆盖 Proposal、Re-propose、Vote、QC/Notify、控制消息、客户端请求/订阅和确认响应。不是抓包统计，不含 TCP/IP、ACK、重传、TLS 记录或以太网开销。',
'- Leader Proposal：副本网络 opcode 0 的发送字节；Leader 全消息出口：Leader 进程所有网络发送；集群总发送：所有副本进程所有网络发送，含发给客户端的确认。',
'- 客户端正文：完整 CommandDummy 字节，不含帧头；客户端总发送含帧头和哈希订阅。端到端总发送=集群总发送+客户端总发送，避免把客户端正文再加一次。',
'- Re-propose 单独统计副本网络 opcode 7；客户端网络 opcode 7 是不同网络上的哈希订阅，不计入 Re-propose。',
'- 包含 3 秒节点启动窗口的控制消息；吞吐分母是实际客户端运行时间。保留末尾在途命令，因此 bytes/confirmed 含未完成请求的发送成本，双方口径一致。',
'- CPU 是活动副本 `/proc` 累计 CPU 秒及约 100 ms 采样峰值（可超过 100%）；内存是活动副本 RSS 合计采样峰值，不是系统内核记录的精确峰值，也不含客户端/操作系统。',
'- RS encode/decode 是单独 RS 调用微秒累计；decode_verify 另含恢复、哈希、命令解析与缓存校验。所有具体数值保留在原始与汇总 CSV。','',
'## 调用链','',
'`CommandDummy → MsgReqCmd → HotStuffApp::client_request_cmd_handler → remember_command → exec_command(cmd_hash) → cmd_pending → PaceMaker::beat → HotStuffCore::on_propose → make_command_batch → rs_encode_shards → 签名 Proposal → do_send_erasure_proposals → propose_handler → accept_body_fragment → broadcast_reproposal → repropose_handler → 收齐 k 片 → rs_reconstruct_data → payload SHA-256 → decode_command_batch → 每条 CommandDummy 完整解析/规范字节比较/SHA-256 → 原始 Block 重建及哈希/QC 检查 → reconstructed_commands → on_deliver_blk（同步交付并重试乱序依赖） → on_receive_proposal → _vote → QC/Commit timer → check_commit → do_decide → Finality → 客户端唯一副本确认计数。`','',
'Leader 同样收集分片并恢复后投票。Strict 中其他副本只接收 `MsgWatchCmd(cmd_hash)` 订阅；订阅不含正文。旧普通 Proposal、整块请求和整块响应不能进入新路径；`_vote` 与 Commit 均检查恢复标记。','',
'## 消息与序列化','',
'- Proposal flag=0/1 保留历史普通/哈希分片路径；flag=2 表示完整正文 RS；flag=3 表示仅实验使用的完整正文复制。公共头包含 proposer、n/k、shard index/size、payload size、command count、origin block hash、view、height、payload SHA-256 和原 Block 元数据（parents、按序 cmd_hash、QC、qc_ref_hash、extra）。',
'- 数据体是独立 raw shard 或完整 batch。Leader 的 secp256k1 证书分别以 0x42（RS）和 0x43（full-broadcast）域分隔，绑定公共头、原 Block 以及当前正文；flag=3 只有本地同时启用 full、Strict 和 `HOTSTUFF_BENCHMARK_FULL_BROADCAST=1` 才接受，不能由远端消息开启，也不产生 Re-propose。',
'- batch：`version:u32le=1 | count:u32le | repeated(length:u32le | serialized_command_bytes)`。SHA-256 固定 32 字节，无 `sizeof(uint256_t)` 或 `sizeof(CommandDummy)` 协议长度。',
'- 新增动态 Dummy 格式供变长测试使用：`cid:u32le | sequence:u32le | payload_length:u32le | payload`；由 `HOTSTUFF_COMMAND_PAYLOAD_BYTES` 显式选择。性能对照使用双方原有固定格式，保留兼容性。',
'- 拒绝超长、计数不符、声明/实际长度不符、未知版本、尾随数据、非规范命令字节、命令哈希不符、payload 哈希不符、Block 哈希/QC 不符或签名不符。','',
'## 状态与工程边界','',
'- 按 origin hash 分池；最多 64 个待恢复 batch、合计预留 shard 容量 64 MiB；30 秒超时，每 250 ms 清理并多轮排空已具备依赖的区块链，避免一层一层重试造成副本永久落后；旧 view 丢弃。',
'- 上限：4096 命令、单命令 1 MiB、batch 8 MiB、n≤32、k≤n、shard≤8 MiB；QC 位图最多 32 副本，分配前检查。正文缓存最多 8192 条/128 MiB，提交后释放。',
'- 已完成池最多 512 个 origin，按 HQC 高度保留约 128 层。缓存/池达到限额会拒绝后续输入；尚无生产级背压、持久化、长期离线同步和跨 view 正文重发方案。',
'- 签名阻止中继篡改诚实 Leader 的分片，但不是可验证 RS 编码证明；恶意 Leader 仍可签发相互不一致的分片。最终哈希检测错误，不定位错误分片，也不自动搜索全部可恢复子集。',
'- 离线实验是 crash/offline，不等同于拜占庭恶意分片容错证明；同机 7 进程实验不能替代多机带宽瓶颈、恶意 Leader、动态 view 和长期压力测试。',
'- 全互联 Re-propose 把正文成本转移到副本网络，集群总流量可能高于 full-broadcast。native baseline 原来由客户端把正文发给全部副本，因此比较 Leader 出口时不能假设原版 Leader 曾广播完整正文。不得把客户端正文发送减少表述为集群总通信减少。','',
'## 可复现命令','',
'```bash',
'cd ~/librightstuff',
'cmake . && make -j2',
'./test/test_command_batch',
'./test/test_full_proposal',
'./test/test_full_command_state',
'./test/test_reproposal_rs',
'bash scripts/run_full_command_asan.sh',
'python3 scripts/full_command_matrix.py --phase correctness --seconds 20 --repetitions 3 --out ~/NEW-UNUSED-correctness',
'python3 scripts/full_command_matrix.py --phase negative --out ~/NEW-UNUSED-negative',
'bash scripts/run_full_command_performance.sh',
'```','',
'性能脚本需要 `~/full-command-baseline-path.txt` 指向由原始提交创建的统计专用 baseline；准备程序和依赖补丁包含在交付文件中。不要复用已有输出目录。','',
'## 简历口径','',
'可写：实现完整命令 batch 的 Reed-Solomon 定向分片、签名 Re-propose、阈值恢复、逐命令与区块哈希校验，以及 Leader/副本统一的 Vote 前置约束；在 7 节点下关闭 2/3 个 systematic shard 节点，分别连续 3 轮推进共识，正常与缺节点场景哈希失败为 0。',
'性能表述必须以本报告实际对照为准；不得沿用哈希列表版本的出口降幅，不得宣称已降低集群总通信量或容忍恶意 Leader。','',
'## 原始证据','',f'- 性能原始目录：`{args.performance}`',f'- 正确性原始目录：`{args.correctness}`',
'- `raw.csv`：正式性能原始行；`deduplicated.csv`：唯一实验键；`aggregate.csv`：跨轮汇总；`correctness.csv`：正确性矩阵。',
'- 源码变更和依赖补丁分别保存在交付目录；当前仍未提交，建议审查后分别提交原有 32 字节修复、完整正文协议/测试、依赖内存修复及统计脚本。','']
(args.out/'REPORT.md').write_text('\n'.join(lines))
print(json.dumps({'performance_runs':len(rows),'groups':len(aggregate),'correctness_runs':len(cor),'report':str(args.out/'REPORT.md')}))
