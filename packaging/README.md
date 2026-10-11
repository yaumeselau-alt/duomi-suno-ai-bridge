# 安装包构建

Windows 产物：dist/Duomi-Suno-AI-Bridge-0.0.2-Windows-x64-Setup.exe。

Mac 交付材料：dist/Duomi-Suno-AI-Bridge-0.0.2-Mac-Source.zip。这是源码构建包，不是 macOS 安装包。随包包含当前项目、测试、JUCE 源码及依赖说明，未包含个人 Key、工程、音频或日志。

## Windows

安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.22+、Inno Setup 6.7+ 后执行：

    powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/build-package.ps1

可用 -IsccPath 指定 ISCC.exe。脚本构建 Release x64、静态 MSVC 运行库，运行离线测试，再生成安装包；任一步失败立即停止。编译产物和临时目录位于 build-package，最终安装包位于 dist。-SkipBuild 只用于已有同配置构建；仍执行测试。

默认安装 VST3 到系统目录，独立程序到 Program Files。支持自定义 VST3 目录、组件选择、系统卸载。安装前保存并关闭 DAW，安装器不强制关闭程序。API Key 与生成文件属于用户资料，卸载不删除。

Configure-FFmpeg.cmd 选择另外安装的可信 ffmpeg.exe；本包没有捆绑 FFmpeg，不依赖开发机的私有路径。完整操作说明见 INSTALL.txt。

## macOS

在有完整 Xcode 的 Mac 上安装 CMake 3.22+，解压源码包，进入其根目录：

    bash packaging/macos/build-package.sh

脚本以 Xcode 构建 Release，最低系统 macOS 11.0，同时编译 arm64 和 x86_64，运行当前平台 CTest，再核查三个 bundle 的双架构和签名。成功后生成 dist 下的 Universal pkg，内容为 VST3、AU 和独立 .app。

没有证书时生成 UNSIGNED 测试包（内部二进制有 ad-hoc 签名），不代表通过 Gatekeeper 或可公开分发。正式签名可设置：

    export MACOS_APP_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
    export MACOS_INSTALLER_SIGN_IDENTITY='Developer ID Installer: Your Name (TEAMID)'
    export MACOS_NOTARY_PROFILE='your-existing-notarytool-keychain-profile'
    bash packaging/macos/build-package.sh

证书与 notarytool 登录资料由持有人在 Mac 钥匙串中配置，脚本不包含、不上传这些资料。指定 profile 后才向 Apple 提交待公证安装包，成功后 staple 并验证。只签名不公证的包仍可能被系统拦截；脚本不关闭 Gatekeeper。

安装位置：

- /Library/Audio/Plug-Ins/VST3/Duomi Suno AI Bridge.vst3
- /Library/Audio/Plug-Ins/Components/Duomi Suno AI Bridge.component
- /Applications/Duomi Suno AI Bridge.app

目前尚未在 Mac 构建与实机验收；脚本的 Bash 语法检查不等于构建通过。Mac 的 Key 保存当前没有实现，每次打开需手动输入；Windows 的 DPAPI 文件不能移植。当前 ApiKeyStore 测试主体仅 Windows 有效，Mac 测试计数不能当成 Key 功能验证。还需实测 AU 扫描、VST3 加载、试听与拖拽。

M4A/Opus 分轨需另装 FFmpeg，再执行：

    bash packaging/macos/configure-ffmpeg.sh /absolute/path/to/ffmpeg

JUCE 在 Mac 的用户配置目录是 ~/Library/DuomiSunoAIBridge；工具只写该用户的 ffmpeg-path.txt。

## 源码包与校验

开发机有 Python 3 时执行：

    python packaging/make-source-kit.py

只打包明确列出的源码目录和构建材料；不把整个工作区或 build/live-validation 加入压缩包。生成源码清单和 SHA256SUMS.txt。Windows 安装包生成后再运行此脚本，即可更新两份交付物的校验值。

第三方许可原文及依赖清单随包保留，见 THIRD_PARTY_NOTICES.txt 和 lib/JUCE。当前交付供私人测试，未替项目选择商业或开源发布许可。

参考：
- [Inno Setup 官方下载校验](https://jrsoftware.org/isdl-verify.php)
- [Apple 安装包、签名和公证说明](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution)


## 已完成验证（2026-10-11）

Windows Release 的 9 个 CTest 测试集通过。安装包在工作区隔离目录完成实际安装，安装后的 VST3 通过工厂、MIDI 输入总线、立体声输出、音频处理及工程状态往返检查；独立程序启动成功。卸载删除了测试安装的二进制，保留无关文件。实际系统 VST3 未被本轮安装测试替换。未声称已在全新 Windows 或本轮在 Live 内完成试听验收。

PE 依赖检查确认不需要额外 MSVC 运行库 DLL。macOS 两份 Bash 脚本通过语法检查；源码 ZIP 完成 CRC、逐文件 SHA256 核验。Mac 尚无编译/安装验证。Windows 安装包目前未签名。

## GitHub Actions

已提供 .github/workflows/macos-package.yml：在 main 的相关源码变更或手动触发时使用标准 macos-15 runner，最多运行 45 分钟。checkout 与 upload-artifact 均固定到官方提交；仓库 token 仅需 contents:read，不传入 Duomi Key 或 Apple 证书。编译成功后保留 Universal 未公证 pkg 14 天，诊断日志保留 7 天。

克隆仓库需要 --recurse-submodules；JUCE gitlink 固定 be29c81492b6151c8ea8d14c840e1311963b3a83（9.0.3）。本地 5347 个官方文件已逐一匹配该提交。Actions 使用 GitHub 账户的可用额度；本流程不修改计费设置。
