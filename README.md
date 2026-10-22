# 光屿 · Photo Enhancement

纯本地照片编辑应用：Flutter 管交互与编辑配方，平台无关 C++ 管像素处理。

各端工具安装、环境变量和一键编译命令见 [构建环境与命令](docs/构建环境与命令.md)。当前 dist 可能早于最后的源码修改，使用构建脚本重新生成最终产物。

滤镜的视觉目标与状态单独维护在 [滤镜清单](docs/滤镜清单.md)；解码导出、滤镜能力、边框、编辑体验和平台交付的工作顺序见 [功能迭代](docs/功能迭代.md)。当前优先处理导出阻塞，再收敛寒峻效果。

## GitHub 自动构建与发布

推送 `dev/dev-v*` 分支运行与 release 相同的 macOS、Windows、Android、iOS 应用构建、打包和发布包校验，但不发布且不额外运行测试；推送 `release-*` 分支生成预发布，推送任意 tag 生成正式 Release。产物包含 macOS 双架构 ZIP / DMG、Windows x64 MSI / ZIP、Android 双 ABI APK、iOS 未签名设备包及模拟器包，以及 SHA256 校验和。鸿蒙暂不参与流水线；上述四端全部成功后才发布。Windows 构建入口已接入，首次 Windows CI 和安装验收尚待实跑，详见 [Windows 构建](docs/Windows构建.md)。配置方法、签名边界及使用示例见 [GitHub 流水线](docs/GitHub流水线.md)。

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
| C++ 图像内核 | JPEG / PNG / LibRaw / HEIF / WebP；ICC；十项调色、HSL、五点主曲线、分离色调、锐化、局部降噪；LUT；文字边框；安全导出 | macOS 编译通过；iOS 设备及双架构模拟器原生库编译通过；真机和画质验收按用户要求暂不开展 |
| C ABI | 版本、错误码、资源上限、不可变图像句柄、显式释放、RGBA 预览 | API 在 native/include/pe/pe.h |
| Flutter | 编辑页、基础/高级调色、纪实光影滤镜、原片对比、可选 EXIF 与品牌文字/PNG 相框、独立边框预设、撤销重做、原图导出 | 早期版本静态检查及 9 项自动化检查通过；本轮新增功能尚待用户编译与测试 |
| macOS 平台接入 | Runner、Release 依赖闭包重定位、独立应用和 ZIP 打包 | Release 构建、打包和 ad-hoc 签名校验已通过；不含 Developer ID 公证 |
| Windows x64 | Flutter Runner、原生 DLL、中文路径及文件平台层、MSI / ZIP 打包和 CI 入口 | 本机原生回归与脚本检查已执行；Windows 编译、安装及启动尚待实际验证 |
| iOS | 设备/模拟器静态库构建、强制链接 C ABI、系统文档导入导出 | 原生三架构编译通过；整包构建被本机 Xcode 缺少 iOS 26.5 平台组件阻塞 |
| Android | Gradle 按 ABI 源码构建内核，系统文档导入导出 | arm64-v8a/x86_64 配置已接入；本机缺 Android SDK/NDK，尚未整包编译 |
| HarmonyOS | ohos SDK 显式构建入口、ArkTS 文档桥接、Dart FFI 动态库加载 | 宿主模板和生成/接入脚本已编写；缺 ohos Flutter/DevEco SDK，尚未编译 |

原始方案的 M0–M4 是完整路线图，不应把本次实现理解为四端已经交付。尤其不能直接运行移动端 Flutter 打包命令就假设图像库会被自动编译。

## 1. 本机编译共享内核（不需要 Flutter / 完整 Xcode）

macOS 安装依赖：

```sh
brew install cmake pkgconf jpeg-turbo libpng libtiff little-cms2 freetype webp libheif
python3 scripts/generate_luts.py
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 8
ctest --test-dir build/native --output-on-failure
```

产物：

- build/native/libphoto_core.dylib：共享内核。
- build/native/photo_cli：独立命令行入口。
- build/native/photo_core_tests：测试程序（包含多项独立回归场景）。

LibRaw 直接从 third_party/LibRaw 的源码编译，不依赖目录内已有的静态库或可执行文件。JPEG、PNG、libtiff（≥4.6.1）、lcms2、FreeType 是必需依赖。HEIF、WebP 可用下面的配置关闭：

```sh
cmake -S . -B build/native -DPE_WITH_HEIF=OFF -DPE_WITH_WEBP=OFF
```

输入默认限制为 512MiB（移动端应用为 256MiB），单边尺寸最多 32768；已移除固定的 8000 万／3200 万像素门槛。相机像素数与压缩文件大小不能直接代表内存占用。

内核按设备物理内存和当前可用内存估算处理预算：桌面取物理内存的 1/4 与可用内存的 1/2 的较小值；移动端取物理内存的 1/8、512MiB 和可用内存的 1/3 的较小值；预览预算最多 128MiB。预算是保守估算，无法保证其他应用占用变化或第三方解码库的实际峰值。

- JPEG 导入先进行解码器缩放，再生成预览，基线 JPEG 无需先在内存中展开原尺寸照片。
- JPEG→JPEG/PNG/HEIF 原尺寸导出的编辑阶段使用分条处理和私有临时磁盘缓存，支持 EXIF 方向、ICC、调色、LUT、锐化／降噪及文字边框。临时空间约为源像素数 × 8 字节；退出处理后自动释放。
- RAW 直接交给 LibRaw 的文件流，不先把压缩文件整份复制到内存。支持的整数单平面 Bayer／X-Trans 半尺寸预览按解包、处理、输出三个阶段估算，不再统一按全尺寸 RGB 管线计费；完整传感器解包的开销仍保留。解包后确认实际缓冲类型，无法确认半尺寸处理时恢复保守估算。RAW 输出直接写入 RGBA16 行缓冲并原地扩展 RGB，避免额外整幅 RGB16 副本。
- 标准整数单平面 Bayer RAW 原尺寸导出沿用 LibRaw AHD 和相机色彩转换，按解包／处理阶段估算内存，逐行写入私有 RGBA16 磁盘缓存后复用分条编辑及 JPEG/PNG/HEIF 编码。避免整图调色副本与相框画布，保留原尺寸；临时空间约为源像素数 × 8 字节。X-Trans、浮点、多平面、旋转传感器等未确认布局仍走保守整图路径。
- 渐进式 JPEG 仍需全尺寸系数缓冲，计入内存预算；其他未支持磁盘缓存的输入仍需整图处理；HEIF 直接接收编辑条带，超过 1024px 的输出使用 1024×1024 HEIF grid 图块逐个编码，仅保留一条 RGB8 带和当前图块；小图使用单平面。已压缩图块仍由 libheif 暂存，预算计入输出像素数 × 8 字节、行带、编码器工作区及固定余量，并非恒定内存或无限大图支持。预算不足时会报告预计内存和当前预算，不承诺所有设备都能处理所有中画幅 RAW。HEIF 网格编码要求 libheif ≥1.19.7（本轮待用户构建验证）。

无需设置 `PHOTO_CORE_MAX_PIXELS`。修改内核后，完整退出应用，再运行 `sh scripts/dev_macos.sh`，或运行 `sh scripts/build_macos.sh --open` 重新打包启动；热重载不会更新已加载的动态库。

## 2. 指定 macOS：开发运行

要求：

1. 安装完整 Xcode（不是仅 Command Line Tools），首次启动完成组件安装和协议确认。
2. 选中 Xcode 的开发者目录；典型命令如下，路径须与你安装的位置一致。
3. 安装 Flutter **3.47.7**（app/.fvmrc），把其 bin 目录加入 PATH，确认 flutter doctor 能识别 Xcode。鸿蒙单独使用兼容的 ohos Flutter 分支，不复用此 SDK。
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

**这不是已公证的安装包。** 面向其他用户分发还需 Developer ID 签名、公证、许可证审计、不同机器启动测试；HEVC 相关编码器和专利也需单独评估。打包脚本已在本机执行并通过签名校验。

## 4. 移动端开发与构建

| 平台 | 必需工具链 | 仍需完成的原生步骤 | 最终 Flutter 构建方式 |
| --- | --- | --- | --- |
| Android | Android SDK / NDK、Java 17、Flutter | Gradle externalNativeBuild 从源码构建内核和依赖，打入两个 ABI；SAF 文档桥接已接入 | sh scripts/build_android.sh |
| iOS | 完整 Xcode、iOS 平台组件、Flutter、真机签名 | 先构建设备 arm64、模拟器 arm64/x86_64 静态依赖闭包，再由 xcconfig force_load 保留 C ABI | sh scripts/build_ios_core.sh；之后在 app 下 flutter build ios 或 flutter build ios --simulator |
| HarmonyOS | DevEco / OpenHarmony native SDK、ohos Flutter | 脚本生成宿主、安装 ArkTS 文件桥接、编译并复制 arm64 内核；使用 ohos Flutter 的 FFI 直接复用 C ABI | 设置 OHOS_FLUTTER、OHOS_SDK_NATIVE 后 sh scripts/build_ohos.sh |

跨平台依赖在 cmake/PortableDependencies.cmake 固定版本及 SHA256，下载到 build 下，不增加嵌套 Git 仓库，也不链接 macOS Homebrew 二进制。JPEG、PNG、RAW、WebP、HEIC 解码共享同一套内核。移动端不默认引入 HEVC 编码器，导出提供 JPEG/PNG；只有检测到 HEVC 编码器时才显示 HEIF 导出。

移动端通过系统文档选择器将输入复制到应用私有目录，先生成完整临时输出，再由文档选择器保存；不申请全盘存储或相册访问权限。当前未实现直接写入系统相册。输入限制为 3200 万像素、256MiB，这不是峰值内存保证；大图 RAW 分块处理仍是后续任务。

鸿蒙示例：

```sh
export OHOS_FLUTTER=/path/to/ohos-flutter/bin/flutter
export OHOS_SDK_NATIVE=/path/to/openharmony/native
sh scripts/build_ohos.sh
```

必须使用匹配的 ohos Flutter/SDK；脚本不会将官方 Flutter 当作鸿蒙 SDK，也不会把缺 SDK 时的模板生成视作编译成功。鸿蒙宿主自定义前应先生成一次工程；后续构建只检查接入文件，不覆盖自定义 EntryAbility。

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
- **EXIF**：有界 TIFF 白名单解析/重建，保留机型、ISO、光圈、快门、焦距和拍摄时间；RAW 使用 LibRaw 提供的时间，不声称含时区信息。导出方向固定为 1。不复制 GPS、MakerNote、缩略图及原始 ICC。
- **许可证**：没有默认引入 Exiv2，避免未经决策直接引入 GPL 分发约束。LibRaw、libheif 等仍须按实际选用的许可证履约，详见 docs/第三方依赖.md。
- **当前不支持**：HDR PQ/HLG HEIF 的色调映射、CMYK JPEG、动画 WebP、RAW 分块导出；不把未经真实样张验证的机型标记为已支持。
- **预设**：四款「纪实光影」原创 LUT 与三款基础纪实 LUT（并非机构官方调色）；便携 JSON 内嵌 LUT / PNG 图标，不信任导入文件中的本地绝对路径，字体需在目标设备上重选。
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

十七套 LUT 可用 `python3 scripts/generate_luts.py` 确定性重新生成；`--styles` 可只生成指定配方。新增「胶片灵感」「经典影调」「自然色彩」六款试用，参考富士、徕卡与哈苏公开视觉特点，属于独立风格模拟；不是官方配方或 HNCS。默认配置、参考来源与单独生成命令见 [滤镜清单](docs/滤镜清单.md)，原有十一款配方与 LUT 保留。

高级调整顺序：十项基础调色 → HSL → SDR 五点折线主曲线 → 分离色调 → 局部明度/色彩降噪与锐化 → LUT → 边框。高级参数随配方保存，默认无效果；旧版预设缺少高级字段时按默认值读取。锐化半径为当前图像的像素单位，因此缩略预览不代替原尺寸细节判断。

真实样张回归入口为 scripts/validate_samples.py，样张清单示例在 samples/manifest.example.json。样张放在忽略版本控制的 samples/private，工具只在本地解码和记录耗时，不上传文件。

## 纪实光影、原片对比与相框（本轮开发，待用户构建测试）

| 滤镜 | 方向 |
| --- | --- |
| 现场 | 中性色彩、适度反差、清晰层次 |
| 烈日 | 金橙暖光、浓厚暗部、强反差 |
| 蓝暮 | 冷色暗部、暖色亮部、压低中间调 |
| 色叙 | 红青色彩突出、杂色收敛、肤色区域减弱染色 |

四款采用 33 阶 SDR sRGB LUT，明暗曲线和色彩变换全部包含在 LUT 中；强度为 0 时移除完整滤镜效果，保留手动调色。肤色保护使用颜色范围权重，并非人脸识别。图像参考来自 AFP 公开图集，不分发参考照片，不宣称恢复原作者的调色参数。原三款基础 LUT 的颜色资源保留，内置预设不再附加会覆盖手动滑杆的参数。

风格面板按系列展示，并用当前原片生成默认强度下的缩略图；缩略图不包含手动调色和边框。选择内置滤镜只更新滤镜与强度，保留手动参数和相框；本地「完整编辑预设」会恢复整套配方。

画布提供「看原片／看效果」按钮和按住查看按钮。对比保留相框和相同缩放位置；原片使用同一次默认解码结果，跳过调色、高级效果和 LUT。RAW 原片指默认解码结果；对比开关不进入历史、不影响导出。

相框支持纯色留白、EXIF 拍立得、左右信息栏、画廊双行；机型、焦距、光圈、快门、ISO、日期和自定义文字可单独选择。缺失值自动省略。Sony / FUJIFILM / Hasselblad 以选定字体绘制文字标识，也可按 EXIF 自动选择；图形标识使用导入的 PNG（≤4MiB、≤1048576 像素，支持透明）。文字和图标显示于底部留白区域，文字需要覆盖相应字形的字体。

独立边框预设只恢复相框，保留调色与滤镜；便携 JSON 内嵌 PNG，导入后缓存到应用目录，不使用导入文件指定的本地路径。字体需在目标设备重选。边框字段与排版接入完整及分条原尺寸导出；原有 pe_border 的 C ABI 布局不变，通过新增 pe_frame 和导出入口扩展。必须重新编译内核及应用，并完整重启。

本轮编译与测试由用户执行：

```sh
sh scripts/build_macos.sh --verify --open
```

重点查看：四款滤镜是否区分明显；强度归零是否保留手动调整；切换滤镜是否保留边框；缩放后原片对比是否跳动；EXIF 缺失、内容开关、品牌文字与透明 PNG；保存/重新导入边框预设后滤镜是否保留；大 JPEG 导出后底部文字与图标是否完整。

### 寒峻试用与不同输入的处理

风格面板新增「冷峻纪实 → 寒峻 · 试用」，使用更强的低调明暗、冷灰绿与暗红颜色关系，并提供材质感、暗角和颗粒。默认颗粒为 0；这些效果与自适应都由滤镜整体强度控制，强度 0 保留手动调色与边框。前面的七款仍使用单独 LUT，不附加这些空间效果。

同一 LUT 不会让已调暖 JPEG 与默认解码 RAW 自动得到相同结果。RAW 先使用相机白平衡和线性 sRGB 解码；JPEG 保留其已有后期和可能的剪裁损失。应用只能分析当前解码像素，不能知道暖色来自现场灯光、白平衡还是后期，更不能恢复已经丢失的颜色信息。

「自适应起点」是可关闭的保守启发式：均匀采样原片亮度及候选中性色，根据亮度分布提供 -0.35～+0.45EV 的有限建议；色温建议限制为应用相对色温单位 ±0.20，并乘以中性色覆盖率和亮度跨度推导的可信度，再由「自适应程度」缩放。这不是识别历史调色、测量真实色温或语义识别白色物体。中性色不足时不明显修正色温。想保留夕阳/钨丝灯暖色时可关闭，再调整手动色温或滤镜强度。

分析使用手动调色之前的默认解码画面，避免自适应追着用户滑杆反向补偿；风格补偿、LUT、材质感、暗角与颗粒在手动调色之后组合，并整体混合。RAW 与 JPEG 的颜色起点不同，预览缩放也会影响采样，不能承诺不同文件、不同分辨率之间逐像素相同。

原尺寸 JPEG 分条导出从磁盘像素缓存采样一次，所有条带共享分析结果；局部效果累加足够的邻域，暗角/确定性颗粒使用整幅照片坐标。选项随完整编辑预设保存，独立边框预设不覆盖它们。本轮未运行编译、自动化测试或效果验收，需重编译内核与应用后试用。

## 相框分类与分享卡片

「相框」面板包含留白、摄影、胶片、社交卡片四类，选择类型后显示对应设置。胶片支持两侧/上下齿孔、胶片文字、编号和日期；社交卡片提供 Instagram、Facebook、X、微信朋友圈的单照片排版，支持头像、昵称、地点、换行配文、时间与互动信息。照片保留原比例，所有内容合成为导出图片，切换类型保留调色及填写的内容。

新增模板接入原片对比、撤销重做、相框预设及原尺寸整图/分条导出。默认无衬线模板会自动为中文使用内置 Noto Sans SC，其他字体可在相框中选择。此功能不连接社交账号。内核有新增 C API，使用前需执行 `sh scripts/build_macos.sh --open` 重编译并完整重启应用。


### TIFF 输入（2026-10-22，待用户构建测试）

新增普通 TIFF / BigTIFF 照片读取，支持 8/16 位整数 RGB/RGBA、灰度、条带/图块、交错/分平面、ICC 与 EXIF 方向。默认 RAW 路径仍由 LibRaw 识别，避免把 DNG、NEF 等 TIFF 容器里的 RGB 预览当原图。TIFF 预览缩小采样，原尺寸导出复用磁盘像素缓存及分条编辑，支持现有滤镜、相框及 JPEG/PNG/HEIF 输出。CMYK、浮点/HDR 等未支持布局会单独提示，当前不提供 TIFF 输出。详情及回归命令见 [构建环境与命令](docs/构建环境与命令.md)。此改动需要重新编译内核；本轮未运行编译和测试。
