# Duomi Suno AI Bridge — 简体中文

**简体中文** | [English](README.en.md)

在 Ableton Live 等 DAW 内生成音乐、提取 MIDI、试听并拖出 MIDI/WAV 的 JUCE 插件。

## 界面语言（0.0.3）

右上角可选择 **简体中文 / English**，切换立即生效。语言随宿主工程或独立程序状态保存，重开窗口保留；旧工程默认中文，各实例互不影响。音乐描述、文件名、MIDI/音频内容不被翻译或修改。原生文件窗口的系统按钮遵从操作系统语言，服务端返回的原始错误保留原文。

完整英文文档入口：[README.en.md](README.en.md)。各专题文档顶部均可切换中英文。

## 功能

- 输入音乐描述和 BPM，通过 Duomi 调用云端生成与 MIDI 提取。
- MIDI 钢琴窗与音频波形，支持播放、暂停、定位和拖入 DAW。
- MIDI 分轨选择、云端乐器分离、单独提取钢琴柱式和弦。
- 黑紫主题、工程状态恢复、Windows 本机 Key 加密保存。

需要自行输入 Duomi API Key。离线试听本地 MIDI/音频不需要账户；云端请求按 Duomi 账户计费。仓库与 CI 不保存真实密钥，不自动运行付费 API 测试。

## 获取与构建

    git clone --recurse-submodules https://github.com/yaumeselau-alt/duomi-suno-ai-bridge.git
    cd duomi-suno-ai-bridge

JUCE 固定为 9.0.3，提交 be29c81492b6151c8ea8d14c840e1311963b3a83，与已验证的本地源码一致。请勿把未初始化的子模块当作完整源码。

Windows 10/11 x64：安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.22+ 和 Inno Setup 6.7+，然后：

    powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/build-package.ps1

Mac：安装完整 Xcode 和 CMake 3.22+，然后：

    bash packaging/macos/build-package.sh

目标 macOS 11+ Universal（Apple Silicon + Intel），包含 VST3、AU 和独立程序。

## GitHub 自动构建 Mac 安装包

下面是 **0.0.2 历史安装包，不含新增语言选择**；0.0.3 请使用本次源码构建对应的产物。

2026-10-12 已生成首个 Universal 测试安装包（VST3 / AU / 独立程序）：
[成功构建与下载](https://github.com/yaumeselau-alt/duomi-suno-ai-bridge/actions/runs/38207700838) → Artifacts → **Duomi-Suno-AI-Bridge-macOS-Universal**。
解压后使用 dist/Duomi-Suno-AI-Bridge-0.0.2-macOS-Universal-UNSIGNED.pkg。本次云端文件保留至 2026-10-26；过期后可按下面步骤重新构建。

包的 SHA256：8986907c2007620fef48cbc9983d4df5eb892520899a8a4bb8a48184d16c3ae3。

仓库 Actions → **macOS Universal installer** → **Run workflow**。
首次推送源代码也会触发构建；流程编译两个架构、运行离线测试并生成 pkg。
完成后在该次运行的 Artifacts 下载 **Duomi-Suno-AI-Bridge-macOS-Universal**。
诊断日志另存为 macOS-build-diagnostics。

该流程不使用 Duomi API Key 或 Apple 证书。默认生成未公证的测试安装包，系统可能阻止安装；正式签名与公证见 packaging/README.md。使用标准 macos-15 runner，单次最多 45 分钟，不自动修改账户的计费或预算设置。

## 当前限制

- Mac 已通过 Universal 编译、跨平台离线测试、双架构和 ad-hoc 签名校验、pkg 生成与展开检查；尚未在 Mac Live/AU 宿主进行试听和拖拽验收。
- Mac 尚未实现 Key 保存，需手动输入；Windows DPAPI 文件不能移植。
- 部分分轨是 M4A/Opus，需另外安装并配置 FFmpeg。安装包不捆绑第三方 FFmpeg。
- 云端分轨可能排队超时；继续查询不会重新启动已经失败的任务。
- 安装前请保存工程并关闭 DAW。卸载保留个人素材、任务缓存与已保存 Key。

## 文档与依赖

- [安装说明](packaging/INSTALL.txt)
- [打包与签名](packaging/README.md)
- [轨道与分轨](TRACK_WORKFLOW.md)
- [柱式和弦](CHORD_WORKFLOW.md)
- [测试](tests/README.md)
- [项目进度](PROJECT_SPEC.md)

项目从 tomoyanonymous/juce_cmake_vscode_example 模板发展而来。第三方许可与依赖说明见 packaging/THIRD_PARTY_NOTICES.txt 和 JUCE 源码许可文件。
