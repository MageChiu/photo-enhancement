# 内置边框素材

2026-10-21 整理。源文件保存在 `sources/`，透明 PNG 保存在
`app/assets/frame_marks/`，离线随包提供，预览和导出共用。

| ID | 来源 | 处理 |
| --- | --- | --- |
| sony | https://commons.wikimedia.org/wiki/File:Sony_logo.svg | 保留字形，裁去透明留白 |
| sony-alpha | https://commons.wikimedia.org/wiki/File:Sony_Alpha_logo.svg | 保留 α 原形与原色 |
| hasselblad / hasselblad-h | https://commons.wikimedia.org/wiki/File:Hasselblad_logo.svg | 保留倾斜字形；H 为字标首字母裁切，并非单独取得的官方徽章 |
| fujifilm / nikon / panasonic / dji / kodak | https://github.com/simple-icons/simple-icons/tree/11.15.0/icons | 单色素材；来源库 CC0 许可证随 sources 提供 |
| sony-g | https://seeklogo.com/vector-logo/257089/sony-g-lens | 独立 G 徽标，原色保留银色字形 |
| sony-gm | https://www.sony.co.uk/lenses/g-master-lenses | 官方 G Master 页面图片 |
| canon | https://commons.wikimedia.org/wiki/File:Canon_wordmark.svg | 保留红色字标 |
| leica | https://commons.wikimedia.org/wiki/File:Leica_Camera.svg | 原色为红底白字；黑白变体把白字留为透明 |
| sigma | https://commons.wikimedia.org/wiki/File:Sigma%27s_new_updated_logo_revealed,_February_2025.svg | 新版字标 |
| tamron | https://commons.wikimedia.org/wiki/File:Tamron_Logo_2019.svg | 字标 |
| zeiss | https://commons.wikimedia.org/wiki/File:Zeiss_logo.svg | 原色保留蓝底白字，黑白变体保留字形留白 |
| lumix | https://commons.wikimedia.org/wiki/File:Lumix_logo.svg | 字标 |
| olympus | https://commons.wikimedia.org/wiki/File:Olympus_Corporation_logo.svg | 字标 |
| om-system | https://commons.wikimedia.org/wiki/File:OM-System-Logo.svg | 提取旧版 OM SYSTEM 图形字样，去除背景及附属文字 |
| pentax | https://commons.wikimedia.org/wiki/File:Pentax_Logo.svg | 字标 |
| ricoh | https://commons.wikimedia.org/wiki/File:Ricoh_logo_2005.svg | 红色字标 |
| gopro | https://commons.wikimedia.org/wiki/File:GoPro_logo_light.svg | 字标 |
| canon-l / nikon-s / sigma-art / sigma-contemporary / sigma-sports | 上述品牌字标 + 内置 IBM Plex Mono | 系列名用中性字体排版，并非官方镜头铭牌复刻 |

G Master 原图地址：
https://sony.scene7.com/is/image/sonyglobalsolutions/1_2_GMasterLogo_Mobile?fmt=png-alpha&wid=640

后缀 `1` 为来源原色，`2` 为深色单色版，`3` 为浅色单色版。
默认「自动黑白」按边框明暗选择 2/3；「原色」选择 1，来源为单色时仍显示单色。
品牌标识属于相应权利人；素材来源或来源库许可不代表品牌授权。

安装开发依赖 `sharp` 后运行 `node scripts/generate_frame_marks.cjs` 重建 PNG。
系列文字生成需要 Fontconfig 可写缓存和内置字体。应用运行时不依赖 Node、
SVG 或系统字体。启动时覆盖复制内置素材到 `frame-assets-v2`，不会沿用旧图。

## 字体

`app/assets/frame_fonts/` 包含 Lato Regular（sans）、IBM Plex Mono Regular
（mono）、IBM Plex Serif Regular（serif），来自 Google Fonts 仓库对应的 OFL
目录，许可证随字体提供。三款字体覆盖拉丁字母与数字；中文题注需选择含中文
字形的本地字体。品牌字标保留素材字形，不用系统字体仿制。

## 排版、识别与兼容

- 八个样式使用同一组选择控件，只改变留白和布局，保留品牌、字体与信息开关。原品牌样式改名为「信息栏」「画廊」「留白」，保留原有尺寸与布局。
- 高级排版分别控制主标、镜头标、机型参数和题注的位置、大小，以及文字间距和对齐。位置按底部留白比例保存，限制在留白内；恢复当前样式会退出微调并恢复原排版。
- 自动相机标识按 Make/Model 推荐；缺失或未知时留空。手动选择不要求 EXIF，也不改写照片机型；隐藏会关闭主标和辅助标。
- 镜头自动识别使用 LensMake/LensModel，不从机身品牌猜镜头。支持 Sony G/GM、Canon L、Nikon S、Sigma 三系列及 Tamron、Zeiss、Fuji、Hasselblad 的部分常见命名；未知时留空并允许手选。
- JPEG、PNG、WebP、HEIF 和 LibRaw 路径读取镜头字段；选择保留元数据时 JPEG/PNG/HEIF 写入安全的镜头字段，不复制 MakerNote。
- 主标与镜头素材独立传递到原生管线，普通模式由原生合成，高级模式独立定位。新增原生函数和独立调整结构，v1 结构布局保持不变；需要重编译原生库。
- 配方保存稳定素材 ID 与排版参数，内置素材按 ID 恢复。外部预设中的 logoPath、accentPath、fontPath 忽略；自定义 PNG 仍可内嵌，字体不分发，导入回退 sans。旧配方继续可读。
