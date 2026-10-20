# 光屿 · Photo Enhancement

纯本地照片编辑应用：Flutter 管交互与编辑配方，平台无关 C++ 管像素处理。

## 获取源码与子模块

```sh
git clone --recurse-submodules git@github.com:MageChiu/photo-enhancement.git
cd photo-enhancement
```

已有本地仓库时，执行以下命令补齐依赖：

```sh
git submodule update --init --recursive
```

`third_party/LibRaw` 作为 Git 子模块管理，固定为上游 `0.22.2` 的提交 `b93f6e45c194f5df9b02a43b1af9a54b4f41f33f`。主仓库记录依赖地址与提交，不收录它的本地编译产物。切换主仓库版本后，应重新执行子模块更新命令；不要用 `git submodule update --remote` 随意更新依赖版本。其他原生依赖仍按下文通过 Homebrew 安装。

## 当前交付范围

| 架构层 | 已落地内容 | 验证边界 |
| --- | --- | --- |
| C++ 图像内核 | JPEG / PNG / LibRaw，按依赖启用 HEIF / WebP；ICC；十项调色；3D LUT；边框与 FreeType 文字；安全导出 | 当前主机为 macOS arm64；完整测试结果见验证记录，不等于四端验收 |
| C ABI | 版本、错误码、资源上限、不可变图像句柄、显式释放、RGBA 预览 | API 在 native/include/pe/pe.h |
| Flutter | 编辑页、文件列表、滑杆、撤销重做、最新请求预览、全分辨率导出、预设导入导出 | 用户已反馈 macOS Debug 构建并启动成功；独立静态检查及测试尚未确认 |
| macOS 平台接入 | 自动生成 Runner、开发加载动态库、Release 依赖闭包和独立应用打包脚本 | Debug 应用已由用户构建成功；Release 应用打包尚待验证 |
| iOS / Android / HarmonyOS | 共享源代码与 ABI 设计 | **尚未完成原生依赖交叉编译、应用链接及设备验收** |

原始方案的 M0–M4 是完整路线图，不应把本次实现理解为四端已经交付。尤其不能直接运行移动端 Flutter 打包命令就假设图像库会被自动编译。

## 1. 本机编译共享内核（不需要 Flutter / 完整 Xcode）

macOS 安装依赖：

```sh
brew install cmake pkgconf jpeg-turbo libpng little-cms2 freetype webp libheif
python3 scripts/generate_luts.py
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 8
ctest --test-dir build/native --output-on-failure
```

产物：

- build/native/libphoto_core.dylib：共享内核。
- build/native/photo_cli：独立命令行入口。
- build/native/photo_core_tests：测试程序（包含多项独立回归场景）。

LibRaw 直接从 third_party/LibRaw 的源码编译，不依赖目录内已有的静态库或可执行文件。JPEG、PNG、lcms2、FreeType 是必需依赖。HEIF、WebP 可用下面的配置关闭：

```sh
cmake -S . -B build/native -DPE_WITH_HEIF=OFF -DPE_WITH_WEBP=OFF
```

**不要对不可信 RAW 使用无资源限制的上层封装。** ABI 默认限制输入 512MiB、原始像素 8000 万，单边不超过 32768。这是尺寸上限，不是峰值内存承诺；RAW 解码及多个管线缓冲可能占用数 GiB。移动端需降低上限，并单独完成分块/内存预算验证。

## 2. 指定 macOS：开发运行

要求：

1. 安装完整 Xcode（不是仅 Command Line Tools），首次启动完成组件安装和协议确认。
2. 选中 Xcode 的开发者目录；典型命令如下，路径须与你安装的位置一致。
3. 安装 Flutter **3.27.4**，把其 bin 目录加入 PATH，确认 flutter doctor 能识别 Xcode。
4. 完成上一节的 Homebrew 依赖安装。

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
flutter --version
flutter doctor -v
sh scripts/dev_macos.sh
```

开发脚本执行顺序：

1. 用 Flutter 生成 macOS / iOS / Android 的标准 Runner 模板，只复制缺少的平台目录，不替换手写的 Dart 应用。
2. 获取 Dart 依赖。
3. 构建本机 C++ 内核。
4. 通过 PHOTO_CORE_LIBRARY 环境变量，将该动态库传给后台 isolate。
5. 启动 macOS 应用。

首版是**非沙盒、本地分发**的开发应用，脚本会关闭 macOS Runner 的 App Sandbox。上架 Mac App Store 前必须改用安全作用域文件访问，并重新验证嵌入库及权限。

## 3. 指定 macOS：打包 Release 应用

**需要可双击的独立应用时用此命令，不用开发调试脚本。** 在项目根目录执行：

```sh
sh scripts/build_macos.sh --open
```

预期交付产物：

```text
dist/光屿.app
dist/光屿-macOS.zip
```

应用可双击启动或拖入“应用程序”，不依赖 flutter run 或终端中的内核路径变量。--open 选项在打包成功后打开该独立应用；去掉它则只生成文件。压缩包用于保存/传输应用包。

该脚本构建内核和 Flutter Release 应用，然后复制一份到交付目录；递归复制图像库的非系统动态依赖，将路径改为包内相对路径，设置应用显示名称，并进行本地 ad-hoc 签名和签名校验。这样不会只复制一份仍依赖 Homebrew 绝对路径的内核。新应用处理成功前保留交付目录中的上一次构建。

若要先运行原生测试、Flutter 静态检查和 Flutter 测试，再打包：

```sh
sh scripts/build_macos.sh --verify --open
```

Finder 中定位或直接打开：

```sh
open -R "dist/光屿.app"
open "dist/光屿.app"
```

开发脚本也会生成调试版应用，位于 app/build/macos/Build/Products/Debug/photo_enhancement.app；但它没有经过独立分发依赖打包，不应直接拷给其他机器使用。

**这不是已公证的安装包。** 面向其他用户分发还需 Developer ID 签名、公证、许可证审计、不同机器启动测试；HEVC 相关编码器和专利也需单独评估。打包脚本当前尚未在完整 Xcode 环境执行。

## 4. 指定其他平台：还缺什么

| 平台 | 必需工具链 | 仍需完成的原生步骤 | 最终 Flutter 构建方式 |
| --- | --- | --- | --- |
| Android | Android SDK / NDK、Java、Flutter | 将依赖按 arm64-v8a 等 ABI 交叉编译，用 NDK 构建 photo_core 并由 Gradle 打入应用；文件/相册导出适配 | 原生集成完成后再执行 flutter build apk 或 flutter build appbundle |
| iOS | 完整 Xcode、iOS SDK、Flutter、真机签名 | 为设备/模拟器构建内核及依赖，生成并链接 XCFramework，确保 C ABI 符号不被裁剪；相册/文件权限适配 | 集成完成后再执行 flutter build ios 或 flutter build ipa |
| HarmonyOS | DevEco / OpenHarmony SDK、版本匹配的 ohos Flutter 分支 | ohos 依赖交叉编译、NAPI 适配、平台插件及工程配置、设备验证 | 完成平台工程后，使用对应 ohos 分支支持的构建命令 |

当前没有声称上述三端能够打包。生成标准 Flutter Runner 只是建立宿主工程，不会完成这些原生步骤。若要推进到四端交付，应按 docs/技术方案-v1.md 的 M0 清单逐项验收，不用空壳工程代替验证。

## 5. 命令行完整编辑流程

```sh
build/native/photo_cli original.jpg edited.png \
  --exposure 0.3 \
  --lut app/assets/luts/documentary.cube \
  --strength 0.7 \
  --border 0.04 --bottom 0.15 \
  --font /System/Library/Fonts/Supplemental/Arial.ttf
```

输出文件必须不存在；绝不覆盖原文件，出错会删除本次创建的不完整输出。导出 PNG 为 16bit sRGB；JPEG / HEIF 为 8bit。PNG 保留 alpha，JPEG / HEIF 在线性光下合成到白底。相框没有 EXIF 时自动留白，也可用 --caption 设置文字。字体需要包含对应字形。

## 6. 已明确的产品/技术约束

- **工作空间**：RGBA16、线性 sRGB、非预乘 alpha；调色和 LUT 的显示域运算显式转换，不混淆 RAW 线性输出与 JPEG 的 sRGB 编码值。
- **LUT**：SDR sRGB 输入/输出、2–65 阶 .cube、三线性插值、红轴变化最快；不接受 1D LUT / log 相机 LUT / 超范围浮点输出。
- **色温**：相对于解码白平衡的调整量，不伪装成绝对 Kelvin。
- **预览**：常驻工作 isolate 缓存 1600px 解码源；一次最多一个渲染请求，过期结果不显示；导出使用独立 isolate、原分辨率重新解码。
- **非破坏编辑**：编辑历史只保存不可变参数，滑杆一次拖动记一次历史；不保存全分辨率图像副本。
- **EXIF**：首版用有界 TIFF 白名单解析/重建，保留机型、ISO、光圈、快门、焦距和拍摄时间（RAW 时间字段仍待补齐），导出方向固定为 1。不复制 GPS、MakerNote、缩略图及原始 ICC。
- **许可证**：没有默认引入 Exiv2，避免未经决策直接引入 GPL 分发约束。LibRaw、libheif 等仍须按实际选用的许可证履约，详见 docs/第三方依赖.md。
- **当前不支持**：HDR PQ/HLG HEIF 的色调映射、CMYK JPEG、动画 WebP、RAW 分块导出；不把未经真实样张验证的机型标记为已支持。
- **预设**：三套原创起点 LUT（并非机构官方调色）；便携 JSON 内嵌 LUT，不信任导入文件中的本地绝对路径，字体需在目标设备上重选。
- **离线**：运行时不需要网络；首次安装依赖及工具链需要网络。

## 7. 测试与开发

```sh
# Native regression tests
cmake --build build/native --parallel 8
ctest --test-dir build/native --output-on-failure

# Memory/undefined-behavior checks (separate build directory)
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DPE_SANITIZE=ON
cmake --build build/asan --parallel 8
ctest --test-dir build/asan --output-on-failure

# Flutter tests; the FFI test is skipped without PHOTO_CORE_LIBRARY
cd app
flutter pub get
flutter analyze
PHOTO_CORE_LIBRARY="$(pwd)/../build/native/libphoto_core.dylib" flutter test
```

三套 LUT 可用 python3 scripts/generate_luts.py 确定性重新生成。

真实样张回归入口为 scripts/validate_samples.py，样张清单示例在 samples/manifest.example.json。样张放在忽略版本控制的 samples/private，工具只在本地解码和记录耗时，不上传文件。
