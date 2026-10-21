# GitHub 构建、验证与发布

工作流：`.github/workflows/release.yml`。覆盖当前已接入的 macOS、Android、iOS、HarmonyOS；项目尚无 Windows/Linux 桌面 Runner，不产出这两端应用。

## 触发规则

| 事件 | 行为 |
| --- | --- |
| push 到 `dev/dev-v*` 分支 | 完整构建所有平台、运行检查和测试、验证发布包并生成校验和；只上传 Actions artifacts |
| push 到 `release-*` 分支 | 同上，全部成功后创建预发布 Release；标签为 `<分支名>-build-<run_id>`，固定到此次构建的提交 |
| push 任意 tag，例如 `v0.1.0`、`release-0.1.0` | 同上，全部成功后以原 tag 创建正式 Release |
| Actions → Build and release → Run workflow | 在所选 ref 上验证整套流程，只上传 artifacts，不发布 |

`dev/dev-v*` 匹配如 `dev/dev-v0.1.0` 的分支，不匹配 `dev-foo`、`dev/dev-foo` 或更深层级路径；`release-*` 不匹配含 `/` 的分支名。每次发布前应维护 `app/pubspec.yaml` 中的应用版本；tag 名不会自动修改应用版本。工作流读取 `app/.fvmrc` 固定 Flutter 版本，递归检出固定提交的 LibRaw 子模块。

同一 `dev/dev-v*` 分支的新提交取消旧构建；发布构建不会被自动取消。任一平台失败，Release 不会创建。没有鸿蒙 runner 时鸿蒙任务会排队，其余平台仍可独立构建并下载各自产物，但完整流水线和发布必须等待鸿蒙成功，不能以跳过鸿蒙冒充四端交付。

## 产物与测试

| 平台 | 产物 | 检查 |
| --- | --- | --- |
| macOS | arm64 / x86_64 独立应用 ZIP | 两种 runner 均运行 CTest、Flutter analyze、Flutter test（设置 `PHOTO_CORE_LIBRARY`，执行 FFI 集成测试）；打包脚本校验 ad-hoc 签名及动态依赖闭包 |
| Android | arm64-v8a / x86_64 分架构 APK | Gradle 对各 ABI 编译原生内核和固定源码依赖，再构建 Release APK |
| iOS | arm64 未签名设备 `.app` ZIP、arm64 模拟器 Debug `.app` ZIP | 构建设备 arm64、模拟器 arm64/x86_64 静态库闭包，再构建两种应用；模拟器应用只针对当前 arm64 宿主 |
| HarmonyOS | arm64-v8a Release HAP（保留上游文件名后缀） | ohos Flutter 构建 HAP、静态分析和 Flutter 测试；FFI 主机集成测试由 macOS 执行 |

所有平台在 dev 和 release 上使用完全相同的构建步骤。打包成功不代表真机或照片画质验收通过。汇总任务会检查所有必需产物、ZIP/APK/HAP 完整性，生成 `build-info.json` 和 `SHA256SUMS`。Actions 中单平台 `package-*` 和完整 `release-bundle` 保留 14 天；Release assets 长期保留。下载后可执行 `sha256sum -c SHA256SUMS` 校验。

macOS 沿用 ad-hoc 签名，无 Developer ID 公证。Android 沿用项目当前开发签名，不适合直接用于商店上架；托管 runner 的开发证书可能随构建变化。iOS 设备包未签名，需通过自己的 Apple 签名流程才能安装，未伪装为可安装 IPA。HarmonyOS 签名由 runner 中的 DevEco/宿主配置决定。

## 首次启用：鸿蒙 runner

GitHub 托管 runner 不提供项目需要的 ohos Flutter / DevEco 配套环境。需在仓库 Settings → Actions → Runners 注册专用自托管 runner，添加 `ohos` 标签（工作流使用 `[self-hosted, ohos]`）。runner 需支持 Bash、Python 3，以及当前版本 Actions 所需的 Node runtime；建议使用 macOS 或 Linux。

在 runner 服务运行环境中提供以下工具和变量（终端交互 shell 中可见不代表服务可见）：

- `OHOS_FLUTTER`：匹配项目 Dart SDK 要求及插件的 ohos Flutter 可执行文件绝对路径。
- `OHOS_SDK_NATIVE`：包含 `build/cmake/ohos.toolchain.cmake` 的 native SDK 目录。
- `DEVECO_SDK_HOME`：匹配 ohos Flutter 的 DevEco SDK。
- PATH 中提供 Git、CMake、Python 3、Node、Java、ohpm、hvigorw，以及 ohos SDK 要求的工具。
- SDK 协议、依赖下载源、HAP 签名及可在无交互环境下执行的构建配置。

先从干净检出的源码执行 `sh scripts/check_toolchains.sh ohos` 和 `sh scripts/build_ohos.sh`，确认可生成 `app/ohos/entry/build/default/outputs/default/*.hap`。工作流检出会清理工作目录，不依赖预先手工修改的 `app/ohos` 文件；如 SDK 要求自定义宿主或签名配置，应将非敏感配置纳入源码，敏感配置通过 runner 的安全配置注入。自托管 runner 应只运行受信任分支，本工作流不接受外部 PR 触发。

托管 macOS/iOS/Android 任务无需额外仓库 secrets。发布仅用内置 `GITHUB_TOKEN`；仓库/组织需允许 Actions 创建 Release（发布 job 申请 `contents: write`）。源码检出不保留 Git 凭据。

## 使用示例

```sh
# 先在开发分支执行完整构建验证
 git switch -c dev/dev-v0.1.0
 git push -u origin dev/dev-v0.1.0

# release 分支每次 push 创建独立预发布，不覆盖历史资产
 git switch -c release-0.1.0
 git push -u origin release-0.1.0

# 已验证的提交打 tag，创建正式 Release
 git tag v0.1.0
 git push origin v0.1.0
```

发布先创建 draft、上传全部资产，最后公开。上传失败保留 draft，可从 Actions 重跑失败任务恢复。已经公开的相同 tag Release 保持不变；修复产物后应发布新 tag。tag 正式发布不会自动设为 Latest，需在 Releases 页面自行选择。

触发和通配符遵循 [GitHub workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)；固定 SDK 的配置见 [flutter-action](https://github.com/subosito/flutter-action)，runner 架构见 [runner-images](https://github.com/actions/runner-images)。
