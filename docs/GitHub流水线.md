# GitHub 构建、验证与发布

工作流：`.github/workflows/release.yml`。当前构建 macOS、Android、iOS 应用。HarmonyOS 暂不参与流水线，保留本地构建脚本；项目尚无 Windows/Linux 桌面 Runner，不产出这两端应用。

## 触发规则

| 事件 | 行为 |
| --- | --- |
| push 到 `dev/dev-v*` 分支 | 构建并打包所有平台应用、汇总发布包并生成校验和；只上传 Actions artifacts |
| push 到 `release-*` 分支 | 同上，全部成功后创建预发布 Release；标签为 `<分支名>-build-<run_id>`，固定到此次构建的提交 |
| push 任意 tag，例如 `v0.1.0`、`release-0.1.0` | 同上，全部成功后以原 tag 创建正式 Release |
| Actions → Build and release → Run workflow | 在所选 ref 上验证整套流程，只上传 artifacts，不发布 |

`dev/dev-v*` 匹配如 `dev/dev-v0.1.0` 的分支，不匹配 `dev-foo`、`dev/dev-foo` 或更深层级路径；`release-*` 不匹配含 `/` 的分支名。每次发布前应维护 `app/pubspec.yaml` 中的应用版本；tag 名不会自动修改应用版本。工作流读取 `app/.fvmrc` 固定 Flutter 版本，递归检出固定提交的 LibRaw 子模块。

同一 `dev/dev-v*` 分支的新提交取消旧构建；发布构建不会被自动取消。任一平台失败，Release 不会创建。鸿蒙不再创建排队任务，也不再是发布所需产物。macOS、Android、iOS 三端全部成功后即可汇总和发布。

## 应用产物

| 平台 | 产物 | 检查 |
| --- | --- | --- |
| macOS | arm64 / x86_64 独立应用 ZIP 和 DMG（拖入 Applications） | 两种 runner 均构建原生内核和 Flutter Release 应用；打包脚本校验 ad-hoc 签名及动态依赖闭包，hdiutil 校验 DMG |
| Android | arm64-v8a / x86_64 分架构 APK | setup-android v4 仅安装 platform-tools（不请求已下线的 tools 包），随后安装固定 SDK/NDK；Gradle 对各 ABI 编译原生内核和固定源码依赖，再构建 Release APK |
| iOS | arm64 未签名设备 `.app` ZIP、arm64 模拟器 Debug `.app` ZIP | 构建设备 arm64、模拟器 arm64/x86_64 静态库闭包，再构建两种应用；模拟器应用只针对当前 arm64 宿主 |

dev 用于验证 release 流水线是否能产出应用。两类分支共用同一套构建、打包、汇总步骤，唯一区别是 dev 不执行最后的 GitHub Release 发布；流水线不运行 CTest、Flutter analyze 或 Flutter test。打包成功不代表真机或照片画质验收通过。汇总任务会检查所有必需产物、ZIP/APK 完整性和 DMG 是否非空（DMG 完整性在 macOS runner 验证），生成 `build-info.json` 和 `SHA256SUMS`。Actions 中单平台 `package-*` 和完整 `release-bundle` 保留 14 天；Release assets 长期保留。下载后可执行 `sha256sum -c SHA256SUMS` 校验。

macOS 沿用 ad-hoc 签名，无 Developer ID 公证。Android 沿用项目当前开发签名，不适合直接用于商店上架；托管 runner 的开发证书可能随构建变化。iOS 设备包未签名，需通过自己的 Apple 签名流程才能安装，未伪装为可安装 IPA。

## 鸿蒙与 Windows

鸿蒙暂时退出 CI，不需要自托管 runner，不会阻塞其他平台。`scripts/build_ohos.sh` 和 `scripts/collect_ohos_artifacts.py` 保留，后续接入时再配置匹配的 ohos Flutter / DevEco 工具链并恢复任务。

Windows 当前不能直接生成可用应用：缺少 `app/windows` Runner、原生 DLL 构建/打包和 Dart FFI 自动加载；内核的 `unistd.h`、`pread`、`pwrite`、`mkstemp`、`sysconf` 等 POSIX 接口还需适配。不能仅新增一个 Windows job 就声明支持。

完成适配后，Flutter 的 Windows Release 构建生成 `.exe`、DLL 和 `data` 资源目录，需整目录分发。该 `.exe` 是应用程序本身，不是安装器；可先压缩为便携 ZIP，另用 Inno Setup 制作 `Setup.exe`，或通过单独的配置/签名流程打包 MSIX / MSI。参见 [Flutter Windows 部署文档](https://docs.flutter.dev/deployment/windows)。

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
