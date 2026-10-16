# 非零形态键的GPU成本评估

先做低开销帧级对照，再做短时间队列捕获。目标是区分CPU重复校验、GPU实际工作增加，以及CPU/GPU同步；当前插件日志中的last_signal、replacements和uploads均不是GPU计时。

## 第一层：同场景帧级对照

使用Intel PresentMon采集MonsterHunterWorld.exe，按游戏主交换链分析，排除控制窗口和其它程序。保存原始CSV及工具版本，记录帧时间、CPU Busy/Wait、GPU Busy/Time/Wait、GPU Latency和Present模式；不同版本列名可能有Ms前缀。GPU Busy表示该进程至少一个GPU引擎执行工作的时间，GPU Time/Wait还需结合空闲间隔理解。指标定义与命令见[官方PresentMon说明](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)。

固定场景、视角、装备、分辨率、画质、窗口模式和帧率限制策略，保持游戏在前台。调整后回到游戏，等待至少5秒并确认applied稳定、active_slot符合预期且uploads停止增长，再采集每组30秒。采集期间不编译或运行CPU基准。顺序为：

1. 全零，确认原输入轻量路径已恢复。
2. 同一个键设为小非零值，例如0.01，确认私有槽已发布；这使外形接近零值，同时启用非零缓冲路径。
3. 同一个键设为1，观察明显形变是否另有GPU成本。
4. 归零并等待退役完成，复测零值。

每组至少重复3次，交替顺序，记录GPU频率、功耗、温度及后台状态，比较原始逐帧分布、中位数及高分位，而不只比较平均FPS。dev6和dev7分别执行相同流程，避免把版本、镜头、几何覆盖或前后台切换混为插件开销。

若零→小非零主要增加帧时间/CPU侧间隔，GPU Busy基本不变，支持CPU提交或同步方向；若GPU Busy也稳定增加，继续查GPU资源、渲染或同步。若只有小非零→1的GPU工作增加，几何覆盖、阴影、遮挡、LOD等是需要控制的解释。上述是对照推断，不是单指标的确定诊断。

CPU和GPU工作会重叠，不能用帧时间减去CPU Busy得到GPU耗时，也不能把GPU Busy/帧时间直接称为“本插件造成掉帧的GPU百分比”。GPU利用率和nvidia-smi是辅助状态，不提供逐帧归因。

## 第二层：短时间队列与资源捕获

用PIX Timing Capture，开启CPU采样、上下文切换、GPU timings与所需资源信息，对全零和稳定小非零各捕获短片段。对齐CPU提交、Direct/Compute/Copy队列、等待与空闲区间，检查原始/私有顶点缓冲的驻留、迁移和实际资源属性。PIX可关联CPU提交与GPU工作，详见[微软Timing Captures说明](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/timing-captures/pix-timing-captures)。

NVIDIA显卡也可使用Nsight Graphics GPU Trace检查GPU忙闲、队列、同步和硬件吞吐，见[官方GPU Trace说明](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html)。工具附加/捕获兼容性需要在实际游戏构建中验证，不能预先保证。捕获有开销，应与正常帧级采样分开。

重点排查私有缓冲是否改变实际堆/驻留位置、读取带宽、资源屏障、跨队列等待或复制行为；不能仅凭插件中的mode字段推断D3D12堆类型。完整GPU回放适合查单个Draw/Dispatch，持续性能归因优先参考实际运行的时序捕获与多组对照。

当前修改没有增加GPU调用或植入GPU计时，也没有自动附加捕获工具。获得CSV/时序捕获后再判断GPU贡献；缺失或不支持的GPU指标应报告为不可用，不能当作零耗时。
