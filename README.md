# WallStreetTicker C++

Windows 任务栏要闻提示程序的 C++/Win32 版本。程序采集华尔街见闻 7x24 快讯接口，只显示 `score >= importantScoreThreshold` 的重要内容。

## 构建

本项目不依赖 Qt、.NET 或第三方包，使用 Win32/GDI/WinHTTP。当前机器可直接使用 CLion 自带 MinGW：

```powershell
mingw32-make
```

生成文件：

```text
build\WallStreetTicker.exe
```

运行：

```powershell
mingw32-make run
```

## 功能

- 默认嵌入 Windows 任务栏中间区域，每次显示一条新闻并自动翻页。
- 右键托盘图标可以刷新、切换任务栏/悬浮、打开来源网页、退出。
- 任务栏模式下拖动文字区域移动位置，拖动左右边缘调整宽度，并自动保存到 `appsettings.json`。
- 悬浮模式显示透明跑马灯，可配置在屏幕顶部或底部。
- 双击文字打开当前新闻；无新闻时打开快讯首页。

## 配置

编辑 exe 同目录的 `appsettings.json` 后重启生效：

- `apiUrl`: 快讯接口地址
- `importantScoreThreshold`: 重要程度阈值，默认 `2`
- `newsTextColor`: 普通新闻文字颜色，默认 `#FFFFFF`
- `importantNewsColor`: 重要新闻文字颜色，默认 `#FF0000`
- `textOpacity`: 新闻文字透明度，`0` 到 `255`
- `refreshSeconds`: 接口刷新间隔，最低 `15`
- `pageSeconds`: 任务栏模式翻页间隔，`2` 到 `60`
- `pixelsPerSecond`: 悬浮跑马灯速度，`30` 到 `600`
- `barHeight`: 横条高度，`24` 到 `120`
- `position`: `taskbar`、`top` 或 `bottom`
- `taskbarLeftOffset` / `taskbarRightOffset` / `taskbarWidth`: 任务栏区域位置和宽度
- `taskbarFontFamily` / `taskbarFontSize`: 任务栏字体配置
- `fontFamily` / `fontSize`: 悬浮跑马灯字体配置
