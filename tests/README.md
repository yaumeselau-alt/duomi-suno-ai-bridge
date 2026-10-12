# 验证记录 — 简体中文

**简体中文** | [English](README.en.md)

> 最新增补（2026-10-12，0.0.3）：新增 LocalisationOffline；Windows Release 共 10 个 CTest 入口。语言检查覆盖完整词条、动态状态、中文路径/服务原文保留、旧工程默认值/非法字段、即时切换、实例隔离、窗口重开及宿主恢复。中英文界面 PNG 位于测试工作目录 language-preview。最新窗口为 720×844。后文为保留的分阶段历史。

> 历史状态（2026-10-10）：9 个 CTest 测试集通过，96 组离线场景及实际 VST3 二进制/状态接口检查通过。MIDI 选轨、音频乐器分离和缓存已实现并安装；真实分轨与键盘 WAV 已验证。当前边界见 PROJECT_SPEC.md §1.22。

## P2 验证

沿用现有 VS 2022 构建目录：

```powershell
cmake -S . -B build -DDUOMI_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`DuomiBridgeCheck` 默认只运行离线 mock，不访问网络。覆盖任务查询、二进制 / HTTPS URL / base64 MIDI、tempo map、错误鉴权、模型回退、超时、取消、任务恢复、后台线程退出和界面渲染。测试产物保留在 CTest 工作目录下 `duomi-test-results/<UUID>/`，包括标准 MIDI 和界面 PNG，便于检查。

2026-10-08 增补：真实接口是 GET JSON body 提交 MIDI 提取，返回新的 MIDI 任务 UUID，再用 feed 查询乐器/音符 JSON。新增测试覆盖异步任务、秒时间到标准 MIDI 的换算、力度及鼓通道、提取超时后恢复、不重复提交和保留音频链接。

正常界面入口：build/src/DuomiSunoAIBridge_artefacts/Debug/Standalone/Duomi Suno AI Bridge.exe。描述和独立 BPM 一起提交；Key 可主动加密保存。P3–P6、钢琴窗/波形拖拽、试听和主题均已实现。最新安装及用户待验收项目见 PROJECT_SPEC.md §1.22。

真实联调需明确授权发送密钥至 Duomi 并承担 API 费用，然后才运行：

```text
build/src/Debug/DuomiBridgeCheck.exe --live
build/src/Debug/DuomiBridgeCheck.exe --resume TASK_ID
build/src/Debug/DuomiBridgeCheck.exe --resume-midi MIDI_TASK_ID
```

Key 通过标准输入传入一行。不要写入源码、命令行参数、脚本文件或提交到 Git。`--live` 创建一次音乐生成任务（接口通常返回两首，本项目选择第一首）；`--resume` 跳过生成，查询已有音乐任务并请求 MIDI 提取，提取接口本身可能计费。`--resume-midi` 仅查询已受理的提取任务并在本地转换，不再次提交计费任务。UI 的「继续查询」在已经收到 MIDI 任务 ID 后使用后者路径。

generate/midi 请求超时或服务器错误时不会自动重发。取消仅停止本地等待，不能取消已提交的云端任务或退还已产生的费用。真实 MIDI 响应结构已验证：`feed.data.data.instruments[].notes[]` 的 `pitch/start/end/velocity`，时间以秒计、力度 0～1；输出格式 1 MIDI，960 PPQ，以 120 BPM 为秒与 ticks 的换算基准，不推断实际歌曲 BPM。

真实样本为 `build/live-validation/C Major Drift.mid`，843 个音符、185.427 秒。与真实 JSON 的全部 noteOn 音高/时间/力度比对结果在 `verification.json`。已完成的提取 ID 是 `<local-task-id>`，优先只读恢复，避免重复提交。

可选原生 HTTP 回环测试：

```powershell
python tests/native_http_check.py build/src/Debug/DuomiBridgeCheck.exe
```

该脚本仅使用 127.0.0.1 和虚构 Key，检查原生传输的 GET 请求体及完整模拟链路，不属于默认 CTest。当前机器上 Python 回环连接可用，但 WinINet 回环诊断失败（错误 1168），所以此项未通过，不能计入通过的离线场景数。真实原生 generate/feed 及 MIDI 结果查询/转换已通过；修复后的原生提取提交还需未来一次获准的新任务验证，重复提取本次音乐已被自动审批拒绝。

默认 MIDI 和逐请求日志目录：系统临时目录下 `DuomiSunoAIBridge`。日志包含 URL（Key 脱敏）、HTTP 状态和文本响应；二进制响应记录字节数。P2 暂不自动清理有效 MIDI，以便后续宿主拖入和检查。

接口文档：[音乐生成](https://api.wike.cc/doc/28)、[任务查询](https://api.wike.cc/doc/31)、[MIDI 提取](https://api.wike.cc/doc/103)。

## P3 MIDI 试听验证

`ctest` 现在包含 `DuomiClientOffline`（23 个场景）与 `MidiPreviewOffline`（9 组场景），全部离线。独立运行与真实文件导出：

```powershell
& "./build/src/Debug/DuomiBridgeCheck.exe" --preview-tests
& "./build/src/Debug/DuomiBridgeCheck.exe" --render-preview "build/live-validation/C Major Drift.mid" "build/live-validation/C Major Drift-preview.wav"
```

导出路径中的文件会覆盖，仅用于验证产物。渲染复用插件 Processor 的后台导入、通知和 processBlock，无需 Key、无网络请求。真实样本已输出 48 kHz / 双声道 / 16 bit WAV，时长 185.557 秒（含尾音），峰值 0.278，RMS 0.0471471；同名 PNG 为播放到约 3 秒时的编辑器快照，已目视检查。

播放测试覆盖采样率和块大小变化、精确事件边界、暂停冻结/恢复、停止与重播、快速命令、48 音符和弦的声部抢占、延音踏板收尾、替换/卸载 MIDI、非音频线程回收、mono/stereo、prepare/release、关闭编辑器后继续播放，以及后台文件导入和缺失文件。验证范围为离线渲染，没有实际监听本机音频设备或在 Live 中运行。

实机验收：启动上述 Standalone → “打开 MIDI”选择真实样本 → 播放，确认听到旋律/和声；暂停后进度停住，继续后保持音符；停止归零，结束后可重播。默认音色用于确认旋律，不是 GM 乐器或原始歌曲音色。完成该步骤后再进入 P4 拖拽验证。

## P4 拖拽与导出验证

当前界面为 640×684。生成或打开 MIDI 后，“拖 MIDI 到 DAW”和“导出 MIDI”可用。拖拽要求按住左键移动，单击不会启动；系统只允许复制。完成提示不代表 DAW 接收成功。导出通过原生另存为窗口选择位置，保存原 MIDI 字节，后台完成读取/验证/写入，覆盖已有文件前由窗口提示。

```powershell
& "./build/src/Debug/DuomiBridgeCheck.exe" --transfer-tests
& "./build/src/Debug/DuomiBridgeCheck.exe" --export-midi "build/live-validation/C Major Drift.mid" "build/live-validation/C Major Drift-export.mid"
```

CLI 导出会覆盖指定目标，限用于已确定的验证产物路径；UI 覆盖提示由原生另存为窗口负责。真实导出已逐字节比对：6669 字节，与原文件完全一致，原文件未改变。

CTest 增加 `MidiTransferOffline`，8 组场景覆盖 Unicode/空格路径、原样导出、覆盖/同路径、非法输入保护、目标错误、拖拽手势/复制标志/文件固定、失败恢复与对象生命周期。拖拽入口注入模拟传输，未发送真实 Windows 拖拽，不计为 Live 验收。三套测试（23 + 9 + 8）全部通过。

宿主验证尝试：本机 Live 12 Suite 已打开，控件树可读取，但窗口截图超时、坐标输入几何不可用；系统 VST3 目录复制也被 Windows 权限拒绝，未完成安装。实际扫描、加载及拖入 Clip 均未确认。可将 `build/src/DuomiSunoAIBridge_artefacts/Debug/VST3` 配为 Live 自定义 VST3 文件夹后验收；不要替换用户已有自定义目录而遗漏原插件。

验收步骤：在空白测试工程的 MIDI 轨道加载插件 → 打开 `C Major Drift.mid` → 试听与播放控制 → 把拖拽按钮拖到另一个空 MIDI 轨道/Clip 槽 → 确认 Clip、音符与源文件仍在 → 检查取消拖拽 → “导出 MIDI”保存后从资源管理器拖入。按 PROJECT_SPEC.md，P4 实机验收前不进入 P5。

## VST3 实际二进制宿主兼容检查

`Vst3BinaryBusCheck` 已加入 Windows CTest。它直接加载构建的 VST3 DLL，检查工厂实例化、主 MIDI 事件输入、立体声输出、总线激活和音频处理。此项能检测只测试共享 Processor 类时遗漏的 JUCE 包装层配置问题。

2026-10-08：旧安装版缺少 MIDI 事件输入（0 个），Live 报 `plugin has instrument category, but no valid event input bus` 并禁用插件。启用 `NEEDS_MIDI_INPUT TRUE` 后，新二进制有 1 个事件输入并通过检查。完整 Debug 构建与 4 个 CTest 测试集通过。本项不代替 Live UI、试听及拖拽验收；最新安装与宿主验证状态见 PROJECT_SPEC.md §1.11。

检查系统已安装的插件（先完成更新）：

```powershell
& './build/src/Debug/Vst3BusCheck.exe' 'C:\Program Files\Common Files\VST3\Duomi Suno AI Bridge.vst3\Contents\x86_64-win\Duomi Suno AI Bridge.vst3'
```
## P5 成品音频预览验证（2026-10-08）

P3/P4 已由用户确认实机验收完成（PROJECT_SPEC.md §1.12）；前文安装失败和待验收描述为历史记录。当前 UI 为 640×756，P5 实现与限制见 §1.13。

```powershell
ctest --test-dir build -C Debug --output-on-failure
& './build/src/Debug/DuomiBridgeCheck.exe' --audio-tests
& './build/src/Debug/DuomiBridgeCheck.exe' --render-audio 'E:/MyVst3Plugins/Duomi Suno AI Bridge/build/live-validation/C Major Drift-audio-preview.wav' 'build/live-validation/audio-local-check.wav'
```

`--render-audio` 接受本地绝对路径或现有 HTTPS 音频地址，只下载/解码音频，不调用付费生成接口；覆盖指定输出 WAV/PNG，仅用于验证产物。所有 5 个 CTest 测试集通过，新增音频 8 组测试覆盖重采样、控制、重定向、错误保留、取消/析构、导入/回收和 Processor 路由。

真实最终 MP3 已通过原生 C++ 下载与完整渲染：187.584 秒，峰值 0.655816，RMS 0.124052；结果为 `build/live-validation/C Major Drift-audio-preview.wav`，同名 PNG 已检查。临时旧地址返回 403，最终 CDN 地址成功。此验证未重新提交音乐生成或提取。

实机验收：在 Live 插件点击“打开音频”选择上述 WAV → 播放/暂停/继续/停止 → 切回 MIDI 确认两者不同时发声。已有生成结果时可点击“加载成品音频”。P5 实机听感待用户确认。
### 音频波形与定位（2026-10-09）

`--audio-tests` 现有 9 组场景，新增双声道峰值摘要、停止/播放/暂停/结束时定位、边界夹紧、连续定位、停止清除定位及失败保留波形。5 个 CTest 测试集全部通过。`--render-audio` 的快照会在完整渲染后定位到 35%，用于检查进度高亮和波形；`C Major Drift-waveform.png` 已目视检查。Live 新版加载验证与用户入口见 PROJECT_SPEC.md §1.14。
## P6 Key 存储与输入错误（2026-10-09）

`ctest` 新增 `ApiKeyStoreOffline`，共 6 个测试集通过。单独运行 `DuomiBridgeCheck.exe --key-tests`：7 组场景测试 DPAPI 加密/重开、替换/忘记、损坏/超大文件、非法输入、文件系统失败、线程生命周期及输入错误不联网。仅虚拟 Key，文件在隔离临时目录；不读取会话中提供的真实密钥来验证保存。

UI 已检查：`build/live-validation/C Major Drift-p6.png`。系统安装结果见 `install-p6-result.json`。实机验收：输入 Key → 保存 → 重开窗口检查恢复 → 忘记后重开应为空。Key 操作不会调用生成接口；完整进度与边界见 PROJECT_SPEC.md §1.15。
## 钢琴窗/音频波形区域拖拽（2026-10-09）

最新 UI 已移除拖拽按钮：按住钢琴卷帘拖出 MIDI，按住波形拖出 WAV；波形单击在松开时定位，外部拖拽不定位。备用目录按钮随试听模式切换。旧按钮相关说明为历史记录。

传递测试增至 11 组，音频测试增至 10 组；全部 6 个 CTest 测试集通过。新增区域手势/单击互斥/文件固定/回调生命周期测试，以及 WAV 格式、完整长度、无试听淡入、关闭后保留和磁盘错误测试。原生拖拽仍由测试注入，用户需实测 Live 接收片段。

音频后台准备的 24-bit WAV 保存在用户应用数据目录 `DuomiSunoAIBridge/AudioExports`，成功文件不会因关闭插件被删除。真实 WAV 参数见 `audio-drag-wav-verification.json`，两种界面快照见 `C Major Drift-direct-drag.png` / `C Major Drift-piano-drag.png`。安装与实机边界见 PROJECT_SPEC.md §1.16。

## BPM、黑紫界面及生成 MIDI 过滤（2026-10-09）

全部 6 个 CTest 测试集通过；客户端离线场景从 23 增至 31。新增检查 BPM 滚轮/双击输入、提交/取消/上下限、窗口重开保留及实际 mock 请求中的 prompt；检查 JSON is_drum/打击乐名称、混合轨道第 10 通道过滤、非标准通道打击乐名称、和弦/力度/控制器/弯音/tempo map 保留、鼓轨上的全局速度保留、SMPTE 和纯鼓失败不发布文件。无鼓的原始二进制仍逐字节保留；手动导入原文件保持不变。

复用本地 MIDI / 音频生成并目视检查两种主题快照，位于 build/live-validation/：

- C Major Drift-cyber-midi.png：843 音符的钢琴窗。
- C Major Drift-cyber-audio.png：187.584 秒音频的波形。
- 同名 WAV 为完整试听渲染结果，沿用已有 --render-preview / --render-audio 验证入口。

本次未调用付费 API；BPM 以关键词提交，不强制重定时或修改 Live 速度。未标记的自定义打击乐无法保证识别。安装结果及手工验收边界见 PROJECT_SPEC.md §1.17。

实机检查：在 Live 加载新版 → 音乐描述右侧 BPM 滚轮调整并双击输入 → 点击播放/暂停/停止图标 → 从钢琴窗拖 MIDI、从波形拖 WAV。若生成新任务，检查导入 DAW 的 MIDI 只含旋律/和弦；生成任务会按原 API 计费，离线测试不计费。


## 复查修复回归（2026-10-09）

- 新增格式 0/1 混合轨道名称含 Drums 的测试：保留旋律/三和弦，删除第 10 通道，tempo map 不变。
- 新增 JSON/二进制名称测试：Hi-Hat、Hi Hat、HIHAT 过滤，Steel Drums 和中文钢鼓保留。
- 新增后台线程启动失败注入：busy 清除、旧音频/波形/WAV 保留、旧音频可继续播放、重试恢复。
- Debug 全量构建及 6 个 CTest 测试集通过；安装版 Vst3BusCheck 通过。日志：build/Testing/Temporary/LastTest.log；安装：build/live-validation/install-review-result.json。
- 本轮未调用付费 API，未声称新版 Live 鼠标操作、扬声器试听或工程状态恢复已验收。


## 独立柱式和弦及工程恢复（2026-10-09）

新增 ChordExtractionOffline（3 组）和 SessionStateOffline（7 组），全部 8 个 CTest 测试集通过。对应命令为 DuomiBridgeCheck.exe --chord-tests / --session-tests；原有测试命令保留。AudioPreviewOffline 的界面尺寸断言更新为 640×844。

- 和弦测试检查已知进行与分解和弦变成统一起止的独立钢琴 MIDI，原文件不变，证据不足不强行造和弦，以及处理限制/取消和预览选择。
- 工程状态测试检查原 MIDI 文件消失后的嵌入恢复、BPM/描述/间隔/模式、工作线程提交状态、恢复尚未完成时再次保存、任务 ID 恢复不联网、无效输入、恢复覆盖、旧预览清除、缺失音频路径保留及 WAV 恢复后手动播放。
- 宿主状态变更通知经过测试；闲置轮询不会持续标脏。
- Vst3BusCheck 在真实 DLL 上调用 IComponent::getState / setState 并核对往返数据；此项与 Live 实际工程文件关闭重开区分记录。
- 全量 CTest 日志保存在 build/live-validation/chord-session-ctest.log；扩展后的二进制检查另行通过，安装版也已验证。

真实样本验证入口：DuomiBridgeCheck.exe --extract-chords 输入.mid 输出.mid，同时生成同名 UI PNG。此前产物为 build/live-validation/C Major Drift-block-chords.mid / .png，以及 C Major Drift-block-chords-preview.wav。172 段、592 音符，57 段待确认、14 段跳过；此计数不代表逐段和声人工验收。

手工验收：加载新版 → 打开 MIDI → 等待自动分析 → 切换“钢琴柱式和弦”试听和拖出；修改间隔后观察重建。然后设置描述/BPM、加载音频 → 保存 Live 工程 → 关闭并重开 → 检查输入、两份 MIDI、音频模式及停止状态。跨机器音频路径需手动重新关联，Key 不在工程中保存。详细用法见 CHORD_WORKFLOW.md。

## 小节刻度与和弦连续性（2026-10-09）

ChordExtractionOffline 从 3 组增至 9 组；全部 8 个 CTest 测试集、87 组离线场景及实际 VST3 二进制/状态接口检查通过。全量日志 build/live-validation/harmony-grid-ctest.log。

新增验证：连续同和弦只有一组长音符；两音/单音短缺口借邻窗证据衔接并标待确认；真正静音保留；C–C7–C 与根音变化不被错误平滑；模糊 C/sus2 候选减少跳变；长段单音不造和弦；候选分析阶段取消；BPM 换算、4 小节翻页、标签连续编号及长音频稀疏标签。修改分析间隔仍重建文件，但同和弦合并后不要求段数翻倍。

真实样本新产物为 build/live-validation/C Major Drift-harmony-smooth.mid / .png，119 段、413 音符、54 个待确认合并段、7 个跳过分析窗。完整 MIDI 渲染为 C Major Drift-harmony-smooth-preview.wav；成品音频刻度快照为 C Major Drift-bars-audio.png。两种界面已目视检查，系统安装版二进制检查通过；未把文件渲染当作实际扬声器试听或 Live 工程实机验收。
## MIDI / 音频轨道选择（2026-10-09–10）

新增 TrackSelectionOffline（DuomiBridgeCheck --track-tests），9 组场景：多轨/混合通道与 tempo/SMPTE/控制器、选轨/原始 MIDI 保留/工程恢复/固定和弦框、分轨提交/多任务兼容、超时与取消后恢复、结果不明不重复计费、本地分轨与混音独立路由、旧工程兼容及路径校验、实际父任务下整组分轨解析/两版本去重/HTTPS 代理，以及持久缓存重开与未知提交保护。总计 9 个测试集，96 组场景及实际 VST3 检查通过。日志 build/live-validation/track-selection-ctest.log。

发现并修复 Key 和音频后台 worker 结束时的 busy 竞态，避免紧接着保存/清空时错误返回；既有 Key/工程场景现通过。音频就绪时总时长可在首个 processBlock 前显示。

真实分轨命令 --separate-stems SOURCE_MUSIC_TASK_ID 使用本机 DPAPI Key，一次实际 gen_stem 提交，后续复用 stem-probe-state.json 中的父任务，切勿删除状态后反复运行。--stem-showcase 从既有结果选择 Keyboard，下载/解码 WAV，检查 12 类下拉框、截图并写入生产缓存；不新增分轨请求。真实结果、费用说明和本地 FFmpeg 依赖见 TRACK_WORKFLOW.md。仅键盘轨完成真实文件解码/整首渲染，未逐一监听其余轨道；系统安装版 ABI 检查不等于 Live 鼠标操作验收。