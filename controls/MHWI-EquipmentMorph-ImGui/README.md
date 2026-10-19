# Dear ImGui 游戏内面板：第二阶段

当前只实现 DX12 游戏内窗口、Ctrl＋Shift＋M 显隐、关闭按钮和键盘／鼠标捕获。窗口有一个临时输入框用于验证输入，尚未接入装备选择、形态键列表或权重控制。外观使用 ImGui 默认样式，留待后续优化。

## 部署与使用

需要 Windows x64、DX12 模式和现有 Stracker's Loader。先正常退出游戏，再将构建输出中的 `MHWEquipmentMorphImGui.dll` 复制到游戏的 `nativePC/plugins/`。不要覆盖核心 `MHWEquipmentMorphConfiguredPrototype.dll` 或 `runtime.ini`。许可证随源码／候选包保留，不需要复制到游戏目录。

启动游戏后，在游戏获得焦点时按 **Ctrl＋Shift＋M**。界面直接绘制到游戏的 DX12 交换链，不启动外部 EXE，也不创建另一个控制窗口。显示时捕获键盘与鼠标；隐藏、点击面板 ×、失去焦点或最小化时恢复游戏输入。隐藏时跳过 ImGui 帧构建与 GPU 提交。

这是独立的可选 DLL。本阶段不调用形态键 API，不打开控制来源，不读写权重。下一阶段将通过核心公开 API 加入装备下拉选择与“形态键名／权重滑块／权重输入框／当前已应用值”四列；届时隐藏保留权重、× 释放面板贡献。

回滚：正常退出游戏，移除 `nativePC/plugins/MHWEquipmentMorphImGui.dll`；核心、配置和装备数据继续保留。

## 构建与隔离验证

在仓库根目录执行：

```powershell
./tools/build_mhwi_equipmentmorph_imgui.ps1
```

默认输出 `.build/current-validation/imgui/`，可用 `-OutputDirectory` 指定目录。使用现有 MSVC／Windows SDK 工具链，不依赖 Node、Rust 或 WebView2。脚本只构建可选面板与测试程序，不安装到游戏，不重建核心。

`EquipmentMorphImGuiFixture.exe` 是单独的 DX12 WARP 软件测试程序，加载相邻 GUI DLL，在独立窗口／渲染线程间验证显隐、输入与 ResizeBuffers。它不加载游戏或装备数据。可在同目录查看 `fixture-state.json` 和 `nativePC/plugins/MHWEquipmentMorph/logs/imgui-<PID>.ndjson`。

2026-10-19 隔离验证通过：快捷键反复显隐、输入框接收字符且宿主 W 消息不增加、隐藏后宿主重新接收 W、面板 × 后可重新呼出、最大化重建资源、最小化释放捕获后可重新打开；隐藏期间绘制计数保持不变。测试机没有 DX12 Debug Layer，因此不将 `d3d12_errors=0` 视为 Debug Layer 验证通过。实机兼容性仍待用户确认。

## 当前边界与诊断

- 只支持 DX12，需在交换链创建之前加载；安装后重新启动游戏，不能向运行中的游戏追加注入。
- 通过创建交换链时的真实 Direct Queue 提交界面，使用独立命令列表、描述符堆和 fence；普通绘制遇到未完成槽位时跳过，不等待 GPU。
- 捕获 Win32 消息、DirectInput 键盘／鼠标、常用按键查询、单条 Raw Input 与光标控制。手柄和 GetRawInputBuffer 尚未接入；实机需验证人物／镜头确实停止响应鼠标与键盘。
- 日志位于 `nativePC/plugins/MHWEquipmentMorph/logs/imgui-<PID>.ndjson`。`boot=1`、`error=none`、`window_ready=true` 表示已完成初始化并识别窗口；显示时 `visible`／`capture` 为 true，`drawn` 增长。`window_ready=false` 表示尚未捕获可用交换链／窗口。
- 使用系统字体 `msyh.ttc`，不重新分发字体。依赖固定为 Dear ImGui v1.93.0，提交及源文件摘要见 [UPSTREAM.json](../../research/imgui-v1.93.0/UPSTREAM.json)，原版源码保持不变。

当前属于未实机验收的开发候选，不改变正式 0.5.0 发行或 dev12 核心验收。
