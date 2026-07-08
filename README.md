# WallStreetTicker

Windows 任务栏要闻提示程序，采集 `https://wallstreetcn.com/live/global` 对应接口里的 7x24 快讯，并只显示 `score >= 2` 的重要内容。

## 运行

```powershell
dotnet run -c Release
```

程序默认嵌入 Windows 任务栏中间区域，透明且鼠标穿透，不会影响任务栏按钮操作。任务栏模式每次显示一条要闻，并按固定间隔自动翻页；悬浮模式仍使用跑马灯滚动。右键系统托盘图标可以刷新、切换任务栏/悬浮、打开来源网页、退出。

## 配置

编辑 `appsettings.json` 后重启程序生效：

- `importantScoreThreshold`: 重要程度阈值，默认 `2`
- `newsTextColor`: 普通新闻文字颜色，默认 `#FFFFFF`，支持 `#RRGGBB` 或命名颜色
- `importantNewsColor`: 重要新闻文字颜色，默认 `#FF0000`，支持 `#RRGGBB` 或命名颜色
- `textOpacity`: 新闻文字透明度，`0` 到 `255`，默认 `255`
- `refreshSeconds`: 接口刷新间隔，默认 `60`
- `pageSeconds`: 任务栏模式翻页间隔，默认 `6`
- `pixelsPerSecond`: 滚动速度，默认 `125`
- `barHeight`: 横条高度，默认 `40`
- `position`: `taskbar`、`top` 或 `bottom`
- `taskbarLeftOffset`: 任务栏左侧预留宽度，默认 `360`
- `taskbarRightOffset`: 任务栏右侧预留宽度，默认 `240`
- `taskbarWidth`: 任务栏新闻区域宽度，大于 `0` 时优先使用
- `taskbarFontFamily` / `taskbarFontSize`: 任务栏字体配置，默认 9 号白字
- `fontFamily` / `fontSize`: 悬浮跑马灯字体配置

任务栏模式下可用鼠标调整位置：拖动文字区域移动整体位置，拖动左右边缘调整宽度；拖到右侧空间不足时会自动收窄宽度。

## 发布 exe

```powershell
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
```

发布结果在 `bin\Release\net10.0-windows\win-x64\publish\WallStreetTicker.exe`。

## GitHub Actions 发版

推送 `v*` 标签会自动构建 Windows x64 自包含单文件 exe，并创建 GitHub Release：

```powershell
git tag v1.0.0
git push origin v1.0.0
```

也可以在 GitHub Actions 页面手动运行 `Build and Release` workflow，下载构建 artifact。

## License

MIT
