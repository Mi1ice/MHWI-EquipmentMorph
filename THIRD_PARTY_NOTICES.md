# 第三方说明

SafetyHook固定版本为0.7.0，依赖文件位于`research/safetyhook-local-v0.7.0/`，原始Boost Software License文本保留为`LICENSE.SafetyHook`。

Zydis固定的单文件构建源码位于同目录，原始MIT许可证文本保留为`LICENSE.Zydis`。依赖文件中的原作者声明保持原样；生成的运行时包也复制这两个许可证。

Blender、MHW Model Editor、Stracker's Loader和游戏本体需要另行安装，本仓库不重新分发这些软件或用户模型。MHW Model Editor适配器记录经验证的源码摘要；依赖版本号相同但摘要不同仍需重新验证。

本文件列出当前打包依赖，不替代项目主代码的许可证声明。

Tauri 控制窗口候选使用 Tauri 2 及其 global-shortcut、single-instance 插件（MIT 或 Apache-2.0），前端使用 React（MIT）。完整依赖版本由 Cargo.lock、package-lock.json 固定。`--desktop` 打包时从本地依赖源码收集包元数据和上游附带的许可证、版权通知，写入 `CONTROL_CENTER_THIRD_PARTY_NOTICES.txt`；包含锁定的构建及跨平台依赖，范围可能大于实际链接的 Windows 组件。WebView2、Windows 系统字体与构建工具需另行安装，本仓库不重新分发。
