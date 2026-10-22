# Windows x64 构建与安装包

Windows 构建入口已接入。应用使用 Flutter Windows Runner，通过程序目录中的 `photo_core.dll` 加载 C++ 内核。首次 Windows CI 编译和 Windows 安装/启动验收仍需实际运行确认，不能将 macOS 上的回归通过等同于 Windows 验证通过。

## 交付文件

- `photo-enhancement-windows-x64.msi`：主要安装包，安装应用及依赖、创建开始菜单快捷方式，支持升级与卸载。
- `photo-enhancement-windows-x64.zip`：完整便携应用目录，解压后运行 `photo_enhancement.exe`，不能单独取出 EXE。

MSI 与 ZIP 包含 Flutter 引擎、资源、`photo_core.dll`、图像编解码 DLL 和应用本地的 VC++ 运行库，不要求用户安装 Flutter 或 Visual Studio，也不在应用启动时下载运行库。因此当前没有另做安装运行库的 Setup.exe。

当前只构建 x64，不含 Windows ARM64。安装包未配置代码签名。项目未接入自动更新服务；升级时运行新版本 MSI。卸载移除安装目录，保留用户目录中的个人预设与缓存。

## 本地构建

需要 Windows、PowerShell 7、Git、Python 3、CMake 3.30 或更新版本、完整 Visual Studio 2022（Desktop development with C++ 与 Windows SDK）、.NET SDK 8，以及 `app/.fvmrc` 指定的 Flutter。所有命令从仓库根目录执行。

```powershell
git submodule update --init --recursive
New-Item build -ItemType Directory -Force
git clone --branch 2026.07.29 --depth 1 https://github.com/microsoft/vcpkg.git build/vcpkg
./build/vcpkg/bootstrap-vcpkg.bat -disableMetrics

dotnet tool install --tool-path build/wix wix --version 4.0.6
$env:PATH = "$PWD/build/wix;$env:PATH"
$env:DOTNET_ROLL_FORWARD = 'Major'
wix extension add --global WixToolset.UI.wixext/4.0.6

./scripts/build_windows.ps1 -VcpkgRoot build/vcpkg
```

产物放在 `artifacts/`。原生依赖放在 `build/windows-vcpkg/x64-windows`，内核放在 `build/windows-native/Release`，Flutter 应用在 `app/build/windows/x64/runner/Release`。构建脚本遇到任何原生命令失败会立即退出，不继续打包旧文件。

版本来自 `app/pubspec.yaml` 的 `major.minor.patch`，构建号 `+N` 不参与 MSI 版本比较。发布升级时须提升这三个字段之一。MSI 前两段最多 255，第三段最多 65535；固定 Upgrade GUID 用于识别同一应用，CPack 的 MajorUpgrade 处理安装替换。安装到 Program Files 需要管理员授权。

原生依赖使用固定 `2026.07.29` vcpkg 仓库版本和 `platform/windows/vcpkg.json`，与移动端的 PortableDependencies 分开管理。HEIF 不启用默认 HEVC 编码器，提供 HEIC 解码和 JPEG/PNG 导出；导出能力仍按运行时检测结果展示。依赖版权文件、LibRaw 许可证、依赖来源说明及 vcpkg 提交记录随包携带。

## 平台适配

- `platform_io` 将公开 C ABI 的 UTF-8 路径转换为 Windows 宽字符路径；JPEG、PNG、TIFF、RAW、LUT、相框图标和字体访问均覆盖中文路径。
- Windows 临时像素缓存使用原子 `CREATE_NEW`、独占文件句柄和关闭时删除；输出使用独占创建，避免覆盖原片或其他已有文件。
- Windows 分块读写使用 64 位定位，并对定位与读写加锁；保留多线程读取缓存时的正确性。
- 使用 `GlobalMemoryStatusEx` 查询内存预算，使用 `_commit` 刷盘。
- FreeType 使用内存字体接口绕过窄字符路径限制；LibRaw 和 TIFF 使用宽字符路径接口；HEIF 使用流式 reader。
- Windows 图标由现有各尺寸 macOS 图标 PNG 封装为 ICO，窗口和产品资源显示光屿名称。

## 流水线与验收

GitHub Actions 的 `windows-2022` job 安装固定依赖与 WiX，执行同一个构建脚本，上传 MSI 和完整 ZIP。`dev/dev-v*` 与 `release-*` 共用构建和打包步骤，不增加分析或单元测试任务；打包前从应用目录加载 DLL（不借用 vcpkg 的 PATH），检查 x64 二进制、C ABI 和编解码能力，避免开发环境掩盖缺失依赖。Windows 与 macOS、Android、iOS 全部构建成功后才汇总，只有 release/tag 执行发布。

首次验收应在没有开发工具链的 Windows x64 机器上安装 MSI，检查启动、中文路径导入导出、JPEG/PNG/TIFF/RAW/HEIC、内置字体相框、个人预设持久化，以及升级/卸载。MSI 生成成功本身不代表这些运行行为已经通过。

开发者可另行构建 `photo_platform_io_tests` 验证平台文件层；现有依赖 POSIX 测试助手的旧原生测试仍只在 Unix 主机运行，不影响默认的应用构建流水线。

本次本机验证：原生内核编译、平台文件层检查、TIFF 与 RAW 回归、两项 Dart FFI 集成、Dart 静态检查、PowerShell 语法/版本解析、MSI CPack 配置与 Actions 语法均通过。完整核心回归为 35/36 通过；原有 HEIF grid 用例未提供字体路径而失败，未修改该用例或放宽断言。Windows 编译和 MSI 安装/运行尚未执行。
