# 2026-10-31开发基线

在eafaa1e历史上保留B01取消scratch清零、保存向量长度约束及19项安全回归，保留B03原锁内IsLive读取和15项回归，纳入已批准的C03数值规则及离线判定器。Stable恢复单个4096容量路径，不含B02大小helper／不内联布局。

B04 worker计量和B05新增Input阶段运行时接入不在当前源码中；历史coarse InputTiming保持原有实现。普通宿主构建不定义 `MHM_DISABLE_INPUT_TIMING`，因此**原coarse采样开启**；`MHM_ENABLE_C07_INPUT_PHASES` 和 `MHM_DISABLE_C07_TIMING` 不传入，新增阶段与worker计量实现已移出，不能把“未定义disable宏”解释为这些已不存在功能仍开启。此基线未提供恢复实验的构建开关。

现有MSVC构建脚本增加可选 `-OutputDirectory`，用于全新输出；`-Symbols`对宿主目标使用 `/Z7`，链接 `/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO` 并输出dll.pdb／tests.pdb。未指定参数时原输出位置及无符号构建方式保持。不得覆盖需保全的历史候选。

可选工具：`tools/C07Benchmark.hpp`／离线调用端及合成检查、`C07LoadDriver.hpp`／假API检查、`analyze_c07_performance.py`／合成检查保留源码，不接入默认构建、打包或运行。它们需要显式输入；依赖私人生成头与真实资产的两个驱动调用端保存在外层私人工作区，没有纳入产品树。没有游戏、部署或自动采集动作。

本基线不是历史实测A/B，也不继承其均值或尾部收益结论。软件验证及本机DLL/PDB身份由维护者外层收据记录，不在此宣称游戏验收、Blender全链或整体正式验收。完整开发入口与专项检查分别记账。
