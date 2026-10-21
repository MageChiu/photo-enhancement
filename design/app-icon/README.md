# 光屿正式图标：B「取景」

2026-10-21 用户选定 B。使用内置 image_gen，以候选 b-viewfinder.png 为编辑参考生成正式母版，再通过 macOS sips 缩放打包；不再使用候选图的白色展示底。

- viewfinder-square.png：1254×1254，无透明通道；铺满画布的石墨底，供 iOS、Android 和鸿蒙使用。
- viewfinder-macos.png：1254×1254，透明外沿，内含圆角图标；供 macOS 使用。

## 母版生成提示词

### 方形母版

Use case: precise-object-edit. Turn the attached SELECTED B viewfinder app icon into a production full-bleed SQUARE master for the photo editor 光屿. Preserve its exact identity: four thick rounded ivory/silver viewfinder corner brackets, graphite ground, centered photographic pane with a rising diagonal warm ivory light to cool teal shadow, thin teal inner accents. Change only presentation and surface polish: REMOVE all external white presentation background and the outer rounded tile boundary; extend the graphite icon background edge-to-edge across the entire square canvas, all four image corners MUST be opaque dark graphite. The icon artwork should occupy a centered safe area about 70 percent of the canvas width, retain the bracket proportions and positions relative to the central image. Slightly reduce fine grain and bevel depth for small-size legibility, keep the selected composition and colors. Flat straight-on front view. No newly invented logo, no text, no perspective, no outside shadow, no padding around an icon tile. Produce a single square raster icon master, suitable for iOS system masking and Android legacy launcher resources.

### macOS 母版

Use case: precise-object-edit. Prepare the attached SELECTED B viewfinder app icon as a production macOS app icon PNG on GENUINE TRANSPARENT background. Preserve the chosen design exactly: dark graphite rounded square tile, four bold rounded silver/ivory viewfinder corner brackets, photographic central pane with a rising diagonal warm ivory light to teal shadow and restrained teal inner edge. Remove the white exterior presentation background entirely and replace it with actual alpha transparency. Fit the rounded square tile centered within the square image, occupying 82 percent of width and height, leaving equal transparent margin. Retain rounded tile silhouette, simplify only very fine grain and overly deep bevels to improve small-icon clarity. Strict front-facing view, no perspective, no text, no new objects, no white matte, no checkerboard painted into image, no external scene, minimal subtle shadow only if it has real transparency. This is the selected identity, do not redesign it.

## 平台资源

macOS 与 iOS 按各自 AppIcon.appiconset/Contents.json 生成全部现有规格；Android 更新五档 mipmap 的 ic_launcher.png。平台现有引用与 Apple 资源清单不变。

鸿蒙预备图标为 platform/ohos/app_icon.png。scripts/build_ohos.sh 在生成/检查宿主后，调用 --ohos-only，将 AppScope/app.json5 和 entry/src/main/module.json5 中已有的应用/Ability icon 字段连接到 photo_app_icon，并复制到对应资源目录。当前未生成鸿蒙宿主，接线尚待实际构建验证。

在 macOS 重新生成全部尺寸：

```sh
python3 scripts/generate_app_icons.py
```

已有鸿蒙宿主只安装预备资源（无需 sips）：

```sh
python3 scripts/generate_app_icons.py --ohos-only
```

本轮已生成资源、检查代表性 PNG 的尺寸/透明通道，并查看 macOS 128px 图标。未运行编译、自动化测试或应用；系统实际显示仍待用户重新构建确认。
