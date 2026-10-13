# Duomi Suno AI Bridge — 项目规范（简体中文公开版）

**简体中文** | [English](PROJECT_SPEC.en.md)

这是 GitHub 公开版。省略本机诊断日志、真实任务标识、账户信息及个人媒体。

## 已实现

- JUCE 9.0.3 / C++17，应用版本 0.0.4；VST3、独立程序，以及 macOS AU。
- Duomi 音乐生成、异步 MIDI 提取及已有任务继续查询。
- 钢琴窗、音频波形、播放控制与拖入 DAW 的 MIDI/WAV。
- 独立 BPM、小节/拍刻度、黑紫主题、生成 MIDI 过滤打击乐。
- 保留原 MIDI，额外生成连续相同和弦合并的钢琴柱式和弦。
- MIDI 选轨、云端乐器分轨及任务缓存。
- 宿主工程状态恢复；Windows DPAPI 本机 Key 保存。
- 每个实例独立的“简体中文 / English”选择，即时更新界面并随工程/独立程序状态保存。
  旧工程默认中文；不修改用户描述、路径、源媒体或服务端原始错误。

## 验证

Windows 0.0.4 Release：10 个 CTest 入口通过，包括真实 VST3 工厂、MIDI 输入总线、
立体声处理及状态往返检查。新增 LocalisationOffline 检查完整词条、动态提示、
中文路径/服务原文保留、旧/非法状态、语言切换、实例隔离、重开及工程恢复。
中英文界面快照已检查。

此前 0.0.2 Windows 安装包通过隔离安装、自定义目录升级及卸载；更早的 Windows
Live 试听/拖拽由项目所有者确认。此前 0.0.2 Mac Universal 编译、8 个 CTest 入口、
三个 bundle 的双架构和严格 ad-hoc 签名检查通过。ApiKeyStoreOffline 在 Mac 无断言，
因此当时有效跨平台测试为 7 个。pkg 生成、展开、本地 ZIP CRC 与 SHA256 一致。

首个 Mac 包：
Duomi-Suno-AI-Bridge-0.0.2-macOS-Universal-UNSIGNED.pkg
SHA256：8986907c2007620fef48cbc9983d4df5eb892520899a8a4bb8a48184d16c3ae3
[构建记录](https://github.com/yaumeselau-alt/duomi-suno-ai-bridge/actions/runs/38207700838)

该恢复运行复用 38187749423 已测试产物（源码 0018d984d137a79017f191de05d3622b5d30ca94），
事前核对产物 SHA256 和编译源文件一致性。**0.0.2 安装包不含新增语言选择**；
0.0.3 需要重新编译本次源码。Mac 实际安装、Live/AU 试听和拖拽仍需实机验收。

## 构建

JUCE 固定 be29c81492b6151c8ea8d14c840e1311963b3a83（9.0.3），克隆需 --recurse-submodules。
见 [打包说明](packaging/README.md)。macos-package.yml 使用标准 macos-15 runner
生成未公证 Universal pkg，不传入 Duomi API Key 或 Apple 证书。

## 已知限制

- Mac Key 持久化尚未实现；每次手动输入。
- M4A/Opus 分轨需单独配置 FFmpeg。
- 云端排队、服务端任务失败与本地等待超时不同；查询失败任务不会重新启动任务。
- Mac 正式签名公证需要所有者的 Developer ID 配置；Windows 安装包也未签名。
- 原生文件对话框系统按钮使用操作系统语言，服务端错误不自动翻译。

## 开发要求

HTTP、文件读写、解码及翻译均不得进入音频回调。禁止提交 Key、DPAPI 文件、
个人媒体和原始诊断日志。离线测试不得提交计费任务。选择轨道时保留原始 MIDI
与完整混音。代码改动同步更新规范及用户文档的中英文版。

## 0.0.4 Logo — 2026-10-13

新增黑紫 D / 音频柱 Logo，接入界面标题区、程序原生图标、Windows Setup/卸载/向导及 Mac 安装欢迎页。PNG/ICO、提示词与生成说明位于 assets。早期安装包不含 Logo；Mac 实际安装和宿主验证仍待完成。
