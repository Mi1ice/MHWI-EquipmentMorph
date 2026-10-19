# 0.5.1-dev11 输入计时与私有资源诊断

本页保留dev11引入诊断时的设计、测试步骤及边界。dev11和dev12之后均已通过用户功能测试，本轮性能优化结束；当前结论以[dev12基线](DEV12_BASELINE.md)和[项目状态](PROJECT_STATUS.md)为准。下文“待测试”和后续分析建议属于当时计划。

dev10已由用户确认同场景功能正常，帧率变化不明显；GPU评估尚未进行。dev11保留现有形变、精度、发布、退役与重绑路径，新增诊断，以决定下一步应继续减少CPU回调工作，还是检查GPU读取私有缓冲区的成本。它不预设帧率收益，待独立实机测试。

## 测试方式

正常退出游戏和控制窗口后使用本候选包安装入口，保留已有公共runtime.ini。包顶层runtime.ini来自仓库config/runtime.ini，是空配置模板；现有游戏配置及MOD旁的形态键包继续使用。重新启动游戏后，在同一场景、相同镜头和画质下，按0→0.01→1→0各停留约30秒；数值需处于当前键允许范围内，每段记录帧率和大致起止时间。中途不要换场景，固定非零时停止拖动。

之后检查连续调节、反向、释放及保持控制的001→其它套装→001重绑。日志仍位于插件公共MHWEquipmentMorph/logs目录的本次runtime日志中。无需额外安装抓帧工具，本版不自动采集GPU时间。

## 输入计时

新增input_timing_window事件，工作线程约每秒提取一次汇总，在释放gate后格式化和写入。回调内没有文件写入、额外锁或动态分配，所有调用只增加固定分桶计数；每线程、每种输入以伪随机方式按约1/64概率采样，未抽中的回调不读取计时时钟。并非严格每64次固定采一次。

- kind：1为compute输入，2为draw输入，3为dispatch。
- part：0～4使用公共部位枚举，5表示未归属回调；route为original、guarded或other。other也包括未启用、非本地主角等提前返回。
- calls是窗口内完成的回调数；samples是有效计时样本数，invalid_samples表示无效时钟样本。跨窗口的回调计入完成时的窗口。
- wait_qpc包含首次及再次取得gate前的区段；held_qpc包含锁内校验与提交区段；read_qpc包含释放gate后读取本次快照的区段。它们包含边界处少量诊断、解锁、调用及调度开销，不是精确的操作系统锁持有统计。
- total_qpc等于有效样本的三个区段之和；max_total_qpc、max_wait_qpc是样本最大值，不是百分位。所有字段都是窗口内累计值，使用qpc_frequency换算秒数，平均微秒为累计值×1000000/qpc_frequency/samples。
- saturated=true表示聚合数值饱和，该窗口不用于定量比较。samples为0时不能计算平均值。

计时是经过的墙钟时间，包含线程被抢占；等待和处理来自同一批样本，可先比较其比例。不能将不同线程样本总和直接视作单帧CPU耗时、CPU忙碌率或GPU占比，也不能把native cycle数量当作显示帧数。每秒快照、格式化和原有日志刷新本身也有成本。

## 私有缓冲信息

新增private_buffer_properties事件，仅在每个自有缓冲首次构造、原有合约验证和初始化等待完成之后执行，在发布给输入路径之前记录。缓存重绑不重复查询，不查询游戏借用的原始资源，不增加GPU提交、资源复制、Map、屏障或额外等待。

先验证接口使用的vtable来自已核验D3D12Core模块，再QueryInterface获取临时ID3D12Resource引用，读取GetDesc与GetHeapProperties并释放临时引用；自有原引用保持不变。代理或不支持的接口、资源属性访问失败只记录不可用，不使绑定失败。无法再次验证安全释放的异常接口会保留临时引用，避免调用未经验证的地址。

日志记录description的dimension、width、alignment、layout、flags，以及heap的type、CPUPageProperty、MemoryPoolPreference、节点掩码和flags。type=1为DEFAULT、2为UPLOAD、3为READBACK、4为CUSTOM；具体属性需结合设备架构解释。原生mode=0或mode=1不能代替heap查询结果。

resource_state和residency保持null，gpu_timing_available固定false：本次查询不能证明当前屏障状态、驻留状态或实际GPU耗时。保留query_hresult、heap_hresult与status，失败时对应对象为null，不把失败的零初始化字段当成有效结果。GetHeapProperties不支持reserved资源时可能返回E_INVALIDARG。

微软接口说明：[GetDesc](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getdesc)、[GetHeapProperties](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getheapproperties)。

## 本地验证与边界

新增30项原生回归，完整原生共607项。覆盖真实生产Input的零/非零读取次数、抽样时钟调用次数、重试区段累加、无效时钟、聚合饱和、窗口重置、锁外日志、分桶隔离、临时COM引用平衡、不可用接口、heap失败和属性访问异常。原有并发输入与重绑回归保留。生成的诊断JSON另经解析，核对64位极值与null字段。

同机、每线程12000次、单线程及四线程、五轮交替开启/关闭诊断的合成回调测试：非零路径耗时中位数差异约−0.5%～+1.3%；零输入增加约0.001～0.004微秒/调用。此结果不含实际游戏调度、日志I/O和GPU，不能据此承诺实机开销或帧率变化。非零路径内存读取次数仍为38/39/41，零路径仍为0/0/1。

若非零日志主要增加held/read，可据此继续细化对应CPU校验；若CPU回调不足以解释同场景差异，再用独立GPU捕获评估私有缓冲读取、重复pass与同步停顿。DEFAULT或UPLOAD的heap标签只是线索，不能单独证明瓶颈，更不能据此直接替换资源分配和同步协议。
