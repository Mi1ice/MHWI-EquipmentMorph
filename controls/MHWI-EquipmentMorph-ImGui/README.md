# Dear ImGui 游戏内面板：第二阶段

当前只实现 DX12 游戏内窗口、Ctrl＋Shift＋M 显隐、关闭按钮和键盘／鼠标捕获。窗口有一个临时输入框用于验证输入，尚未接入装备选择、形态键列表或权重控制。外观使用 ImGui 默认样式，留待后续优化。

## 部署与使用

需要 Windows x64、DX12 模式和现有 Stracker's Loader。先正常退出游戏，再将构建输出中的 `MHWEquipmentMorphImGui.dll` 复制到游戏的 `nativePC/plugins/`。不要覆盖核心 `MHWEquipmentMorphConfiguredPrototype.dll` 或 `runtime.ini`。许可证随源码／候选包保留，不需要复制到游戏目录。

启动游戏后，在游戏获得焦点时按 **Ctrl＋Shift＋M**。界面直接绘制到游戏的 DX12 交换链，不启动外部 EXE，也不创建另一个控制窗口。显示时捕获键盘与鼠标；隐藏、点击面板 ×、失去焦点或最小化时恢复游戏输入。隐藏时跳过 ImGui 帧构建与 GPU 提交。

快捷键在窗口线程注册，游戏失去焦点时解除注册，避免依赖输入法处理后的普通按键消息。日志记录 `hotkey_registered` 和 `hotkey_error`；注册成功时分别为 true 和 0。原窗口消息路径作为注册失败时的备用。输入法相关处理依据 [ImmGetVirtualKey 的时序限制](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetvirtualkey)。

这是独立的可选 DLL。本阶段不调用形态键 API，不打开控制来源，不读写权重。下一阶段将通过核心公开 API 加入装备下拉选择与“形态键名／权重滑块／权重输入框／当前已应用值”四列；届时隐藏保留权重、× 释放面板贡献。

回滚：正常退出游戏，移除 `nativePC/plugins/MHWEquipmentMorphImGui.dll`；核心、配置和装备数据继续保留。

## 构建与隔离验证

在仓库根目录执行：

```powershell
./tools/build_mhwi_equipmentmorph_imgui.ps1
./tools/test_mhwi_equipmentmorph_imgui.ps1
```

默认输出 `.build/current-validation/imgui/`，可用 `-OutputDirectory` 指定目录。使用现有 MSVC／Windows SDK 工具链，不依赖 Node、Rust 或 WebView2。脚本只构建可选面板与测试程序，不安装到游戏，不重建核心。

`EquipmentMorphImGuiFixture.exe` 是单独的 DX12 WARP 软件测试程序，加载相邻 GUI DLL，在独立窗口／渲染线程间验证显隐、输入与 ResizeBuffers。它不加载游戏或装备数据。可在同目录查看 `fixture-state.json` 和 `nativePC/plugins/MHWEquipmentMorph/logs/imgui-<PID>.ndjson`。

2026-10-19 初版隔离验证覆盖了先加载插件再创建交换链的显隐、输入及缩放。2026-10-20 用户实机发现无法呼出，日志持续 `window_ready=false`；旧 DLL 在先创建交换链、后加载插件的程序中复现相同故障。修复后，两种加载顺序、同设备无关 Direct Queue 干扰、Reset 丢弃旧命令记录、ResizeBuffers 后重识别、隐藏时不绘制的回归通过。后加载的交互窗口也完成输入数字后显隐及失焦恢复的检查。

测试脚本只启动独立软件程序并保留逐例日志与 `results.json`，不启动游戏。`--late-load` 让测试程序在创建交换链并呈现数帧之后加载 DLL；`--smoke` 运行尺寸变化测试并自动正常退出。测试机没有 DX12 Debug Layer，因此不将 `d3d12_errors=0` 视为 Debug Layer 验证通过。修复候选的实机兼容性仍待用户确认。

## 当前边界与诊断

- 只支持 DX12，安装后重新启动游戏；支持插件加载时交换链已存在，不向运行中的游戏追加注入。
- 优先使用创建回调提供的队列；错过创建时，在 Present 中识别已有窗口与后台缓冲，再通过最终转换到 PRESENT 的命令列表及其实际提交队列确认后台缓冲渲染队列，防止误取无关队列。Reset 清除旧记录，缩放前释放探测引用，缩放后重新识别。特殊跨队列呈现／交换链包装兼容性仍待实机确认。
- 使用独立命令列表、描述符堆和 fence；普通绘制遇到未完成槽位时跳过，不等待 GPU。队列识别完成后，探测回调直接转发。
- 捕获 Win32 消息、DirectInput 键盘／鼠标、常用按键查询、单条 Raw Input 与光标控制。手柄和 GetRawInputBuffer 尚未接入；实机需验证人物／镜头确实停止响应鼠标与键盘。
- 日志位于 `nativePC/plugins/MHWEquipmentMorph/logs/imgui-<PID>.ndjson`。正常为 `boot=1`、`error=none`、`window_ready=true`、`queue_ready=true`；显示时 `visible`／`capture` 为 true，`drawn` 增长。`binding_route` 为 `creation` 或 `backbuffer_submission`；等待后台缓冲提交时为 `waiting_for_backbuffer_submission`。`captured_creations`、`backbuffer_transitions`、`matched_submissions` 用于区分创建路径与后加载探测进度。
- 使用系统字体 `msyh.ttc`，不重新分发字体。依赖固定为 Dear ImGui v1.93.0，提交及源文件摘要见 [UPSTREAM.json](../../research/imgui-v1.93.0/UPSTREAM.json)，原版源码保持不变。

当前属于未实机验收的开发候选，不改变正式 0.5.0 发行或 dev12 核心验收。
