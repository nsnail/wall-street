using System.Diagnostics;
using System.Drawing.Text;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace WallStreetTicker;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new TickerForm(AppConfig.Load()));
    }
}

internal sealed record AppConfig(
    string ApiUrl,
    int ImportantScoreThreshold,
    string NewsTextColor,
    string ImportantNewsColor,
    int TextOpacity,
    int RefreshSeconds,
    int PageSeconds,
    int PixelsPerSecond,
    int BarHeight,
    string Position,
    int TaskbarLeftOffset,
    int TaskbarRightOffset,
    int TaskbarWidth,
    string TaskbarFontFamily,
    float TaskbarFontSize,
    string FontFamily,
    float FontSize)
{
    public static AppConfig Load()
    {
        var defaults = new AppConfig(
            "https://api-one-wscn.awtmt.com/apiv1/content/lives?channel=global-channel&limit=50",
            2,
            "#FFFFFF",
            "#FF0000",
            255,
            60,
            6,
            125,
            56,
            "taskbar",
            360,
            240,
            0,
            "Microsoft YaHei UI",
            9,
            "Microsoft YaHei UI",
            18);

        var path = Path.Combine(AppContext.BaseDirectory, "appsettings.json");
        if (!File.Exists(path))
        {
            return defaults;
        }

        try
        {
            using var json = JsonDocument.Parse(File.ReadAllText(path, Encoding.UTF8));
            var root = json.RootElement;

            return defaults with
            {
                ApiUrl = ReadString(root, "apiUrl", defaults.ApiUrl),
                ImportantScoreThreshold = ReadInt(root, "importantScoreThreshold", defaults.ImportantScoreThreshold),
                NewsTextColor = ReadString(root, "newsTextColor", defaults.NewsTextColor),
                ImportantNewsColor = ReadString(root, "importantNewsColor", defaults.ImportantNewsColor),
                TextOpacity = Math.Clamp(ReadInt(root, "textOpacity", defaults.TextOpacity), 0, 255),
                RefreshSeconds = Math.Max(15, ReadInt(root, "refreshSeconds", defaults.RefreshSeconds)),
                PageSeconds = Math.Clamp(ReadInt(root, "pageSeconds", defaults.PageSeconds), 2, 60),
                PixelsPerSecond = ReadSpeed(root, defaults.PixelsPerSecond),
                BarHeight = Math.Clamp(ReadInt(root, "barHeight", defaults.BarHeight), 24, 120),
                Position = ReadString(root, "position", defaults.Position),
                TaskbarLeftOffset = Math.Clamp(ReadInt(root, "taskbarLeftOffset", defaults.TaskbarLeftOffset), 0, 10000),
                TaskbarRightOffset = Math.Clamp(ReadInt(root, "taskbarRightOffset", defaults.TaskbarRightOffset), 0, 10000),
                TaskbarWidth = Math.Clamp(ReadInt(root, "taskbarWidth", defaults.TaskbarWidth), 0, 10000),
                TaskbarFontFamily = ReadString(root, "taskbarFontFamily", ReadString(root, "fontFamily", defaults.TaskbarFontFamily)),
                TaskbarFontSize = Math.Clamp(ReadFloat(root, "taskbarFontSize", defaults.TaskbarFontSize), 6, 24),
                FontFamily = ReadString(root, "fontFamily", defaults.FontFamily),
                FontSize = Math.Clamp(ReadFloat(root, "fontSize", defaults.FontSize), 10, 42)
            };
        }
        catch
        {
            return defaults;
        }
    }

    private static string ReadString(JsonElement root, string name, string fallback) =>
        root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;

    private static int ReadInt(JsonElement root, string name, int fallback) =>
        root.TryGetProperty(name, out var value) && value.TryGetInt32(out var result)
            ? result
            : fallback;

    private static float ReadFloat(JsonElement root, string name, float fallback) =>
        root.TryGetProperty(name, out var value) && value.TryGetSingle(out var result)
            ? result
            : fallback;

    private static int ReadSpeed(JsonElement root, int fallback)
    {
        if (root.TryGetProperty("pixelsPerSecond", out var pixelsPerSecond) && pixelsPerSecond.TryGetInt32(out var speed))
        {
            return Math.Clamp(speed, 30, 600);
        }

        if (root.TryGetProperty("pixelsPerTick", out var pixelsPerTick) && pixelsPerTick.TryGetInt32(out var oldSpeed))
        {
            return Math.Clamp(oldSpeed * 60, 30, 600);
        }

        return fallback;
    }
}

internal sealed class TickerForm : Form
{
    private static readonly Color TransparentBackColor = Color.FromArgb(1, 1, 1);
    private const string TickerSeparator = "    |    ";
    private const int ResizeGripWidth = 24;
    private readonly AppConfig _config;
    private readonly LiveNewsClient _client;
    private readonly System.Windows.Forms.Timer _refreshTimer = new();
    private readonly System.Windows.Forms.Timer _placementTimer = new();
    private readonly System.Windows.Forms.Timer _pageTimer = new();
    private readonly Color _newsTextColor;
    private readonly Color _importantNewsColor;
    private readonly CancellationTokenSource _animationCancellation = new();
    private readonly NotifyIcon _notifyIcon = new();
    private readonly Font _tickerFont;
    private readonly Font _taskbarFont;
    private readonly Stopwatch _scrollWatch = new();
    private IReadOnlyList<LiveNewsItem> _items = [];
    private string _tickerText = string.Empty;
    private Size _tickerSize = Size.Empty;
    private bool _tickerTextIsNews;
    private int _currentPageIndex;
    private int _taskbarLeftOffset;
    private int _taskbarRightOffset;
    private int _configuredTaskbarContentWidth;
    private int _taskbarWidth;
    private int _dragStartMouseX;
    private int _dragStartLeft;
    private int _dragStartWidth;
    private TaskbarDragMode _hoverMode = TaskbarDragMode.None;
    private TaskbarDragMode _dragMode = TaskbarDragMode.None;
    private bool _isTaskbarMouseInside;
    private bool _isBottom;
    private bool _useTaskbarMode;
    private bool _isTaskbarHosted;

    public TickerForm(AppConfig config)
    {
        _config = config;
        _client = new LiveNewsClient(config);
        _isBottom = config.Position.Equals("bottom", StringComparison.OrdinalIgnoreCase);
        _useTaskbarMode = config.Position.Equals("taskbar", StringComparison.OrdinalIgnoreCase);
        _taskbarLeftOffset = config.TaskbarLeftOffset;
        _taskbarRightOffset = config.TaskbarRightOffset;
        _configuredTaskbarContentWidth = config.TaskbarWidth;
        _newsTextColor = WithAlpha(ParseColor(config.NewsTextColor, Color.White), config.TextOpacity);
        _importantNewsColor = WithAlpha(ParseColor(config.ImportantNewsColor, Color.Red), config.TextOpacity);
        _tickerFont = CreateTickerFont(config);
        _taskbarFont = CreateFont(config.TaskbarFontFamily, config.TaskbarFontSize);

        InitializeWindow();
        InitializeMenu();
        InitializeTimers();
    }

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.ExStyle |= NativeMethods.WsExLayered
                | NativeMethods.WsExToolWindow
                | NativeMethods.WsExNoActivate;
            return cp;
        }
    }

    protected override async void OnShown(EventArgs e)
    {
        base.OnShown(e);
        PlaceWindow();
        SetTickerText("正在加载华尔街见闻重要快讯...");
        NativeMethods.TimeBeginPeriod(1);
        _ = RunAnimationLoopAsync(_animationCancellation.Token);
        _ = ReattachToTaskbarAfterStartupAsync();
        await RefreshNewsAsync();
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == NativeMethods.WmNcHitTest && !_isTaskbarHosted)
        {
            m.Result = NativeMethods.HtTransparent;
            return;
        }

        base.WndProc(ref m);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _animationCancellation.Cancel();
        NativeMethods.TimeEndPeriod(1);
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
        _tickerFont.Dispose();
        _taskbarFont.Dispose();
        _client.Dispose();
        _refreshTimer.Dispose();
        _placementTimer.Dispose();
        _pageTimer.Dispose();
        base.OnFormClosing(e);
    }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        e.Graphics.Clear(TransparentBackColor);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (string.IsNullOrWhiteSpace(_tickerText))
        {
            return;
        }

        e.Graphics.TextRenderingHint = _isTaskbarHosted
            ? TextRenderingHint.ClearTypeGridFit
            : TextRenderingHint.ClearTypeGridFit;
        using var shadowBrush = new SolidBrush(Color.FromArgb(170, 0, 0, 0));
        using var textBrush = new SolidBrush(GetTickerTextColor());

        if (_isTaskbarHosted)
        {
            DrawTaskbarPage(e.Graphics);
            return;
        }

        var x = CalculateTextX();
        var y = Math.Max(0, (Height - _tickerSize.Height) / 2);
        if (_tickerTextIsNews && _items.Count > 0)
        {
            DrawTickerItems(e.Graphics, shadowBrush, x, y);
            return;
        }

        e.Graphics.DrawString(_tickerText, _tickerFont, shadowBrush, x + 1, y + 1);
        e.Graphics.DrawString(_tickerText, _tickerFont, textBrush, x, y);
    }

    private void InitializeWindow()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        TopMost = false;
        ShowInTaskbar = false;
        BackColor = TransparentBackColor;
        ForeColor = Color.White;
        Height = _config.BarHeight;
        SetStyle(ControlStyles.AllPaintingInWmPaint
            | ControlStyles.UserPaint
            | ControlStyles.OptimizedDoubleBuffer
            | ControlStyles.ResizeRedraw
            | ControlStyles.SupportsTransparentBackColor, true);

        MouseDown += HandleTaskbarMouseDown;
        MouseMove += HandleTaskbarMouseMove;
        MouseUp += HandleTaskbarMouseUp;
        DoubleClick += (_, _) => OpenCurrentNews();
        MouseEnter += (_, _) =>
        {
            if (_isTaskbarHosted)
            {
                _pageTimer.Stop();
            }
        };
        MouseLeave += (_, _) =>
        {
            UpdateTaskbarHoverFromCursor();
        };
    }

    private void InitializeMenu()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("刷新", null, async (_, _) => await RefreshNewsAsync());
        menu.Items.Add("切换任务栏/悬浮", null, (_, _) =>
        {
            _useTaskbarMode = !_useTaskbarMode;
            if (!_useTaskbarMode)
            {
                NativeMethods.SetParent(Handle, IntPtr.Zero);
                _isTaskbarHosted = false;
            }

            PlaceWindow();
        });
        menu.Items.Add("打开来源网页", null, (_, _) =>
        {
            Process.Start(new ProcessStartInfo("https://wallstreetcn.com/live/global") { UseShellExecute = true });
        });
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("退出", null, (_, _) => Close());

        ContextMenuStrip = menu;

        _notifyIcon.Icon = SystemIcons.Information;
        _notifyIcon.Text = "华尔街见闻要闻跑马灯";
        _notifyIcon.ContextMenuStrip = menu;
        _notifyIcon.Visible = true;
        _notifyIcon.DoubleClick += async (_, _) => await RefreshNewsAsync();
    }

    private void InitializeTimers()
    {
        _refreshTimer.Interval = _config.RefreshSeconds * 1000;
        _refreshTimer.Tick += async (_, _) => await RefreshNewsAsync();
        _refreshTimer.Start();

        _placementTimer.Interval = 2000;
        _placementTimer.Tick += (_, _) => PlaceWindow();
        _placementTimer.Start();

        _pageTimer.Interval = _config.PageSeconds * 1000;
        _pageTimer.Tick += (_, _) => ShowNextPage();
        _pageTimer.Start();
    }

    private void PlaceWindow()
    {
        if (_useTaskbarMode)
        {
            if (TryPlaceInTaskbar())
            {
                return;
            }
        }

        _isTaskbarHosted = false;
        NativeMethods.SetParent(Handle, IntPtr.Zero);
        BackColor = TransparentBackColor;
        TransparencyKey = TransparentBackColor;
        ApplyLayeredTransparency();
        var area = Screen.PrimaryScreen?.WorkingArea ?? Screen.GetWorkingArea(this);
        var oldBounds = Bounds;
        Width = area.Width;
        Left = area.Left;
        Top = _isBottom ? area.Bottom - Height : area.Top;
        TopMost = true;
        if (Bounds != oldBounds)
        {
            RestartScroll();
        }
    }

    private bool TryPlaceInTaskbar()
    {
        var taskbarHandle = NativeMethods.FindWindow("Shell_TrayWnd", null);
        if (taskbarHandle == IntPtr.Zero || !NativeMethods.GetWindowRect(taskbarHandle, out var rect))
        {
            return false;
        }

        var taskbarWidth = rect.Right - rect.Left;
        var taskbarHeight = rect.Bottom - rect.Top;
        if (taskbarWidth <= 0 || taskbarHeight <= 0)
        {
            return false;
        }

        if (!_isTaskbarHosted)
        {
            NativeMethods.SetParent(Handle, taskbarHandle);
            _isTaskbarHosted = true;
            BackColor = TransparentBackColor;
            ApplyLayeredTransparency();
        }

        TopMost = false;
        var isHorizontal = taskbarWidth >= taskbarHeight;
        if (isHorizontal)
        {
            _taskbarWidth = taskbarWidth;
            var left = Math.Min(_taskbarLeftOffset, Math.Max(0, taskbarWidth - 120));
            var width = _configuredTaskbarContentWidth > 0
                ? Math.Clamp(_configuredTaskbarContentWidth, 120, Math.Max(120, taskbarWidth - left))
                : Math.Max(120, taskbarWidth - left - Math.Min(_taskbarRightOffset, Math.Max(0, taskbarWidth - left - 120)));
            _taskbarRightOffset = Math.Max(0, taskbarWidth - left - width);
            var height = Math.Min(_config.BarHeight, taskbarHeight);
            var top = Math.Max(0, (taskbarHeight - height) / 2);

            MoveWindowIfNeeded(left, top, width, height);
        }
        else
        {
            var top = Math.Min(96, Math.Max(0, taskbarHeight - 120));
            var bottom = Math.Min(96, Math.Max(0, taskbarHeight - top - 120));
            var height = Math.Max(120, taskbarHeight - top - bottom);
            var width = Math.Min(_config.BarHeight * 4, taskbarWidth);
            var left = Math.Max(0, (taskbarWidth - width) / 2);

            MoveWindowIfNeeded(left, top, width, height);
        }

        return true;
    }

    private void MoveWindowIfNeeded(int left, int top, int width, int height)
    {
        if (Left == left && Top == top && Width == width && Height == height)
        {
            BringTaskbarWindowToFront();
            return;
        }

        NativeMethods.SetWindowPos(
            Handle,
            NativeMethods.HwndTop,
            left,
            top,
            width,
            height,
            NativeMethods.SwpShowWindow | NativeMethods.SwpNoActivate);
        RestartScroll();
        Invalidate();
    }

    private void BringTaskbarWindowToFront()
    {
        if (!_isTaskbarHosted)
        {
            return;
        }

        NativeMethods.ShowWindow(Handle, NativeMethods.SwShowNoActivate);
        NativeMethods.SetWindowPos(
            Handle,
            NativeMethods.HwndTop,
            0,
            0,
            0,
            0,
            NativeMethods.SwpNoMove | NativeMethods.SwpNoSize | NativeMethods.SwpShowWindow | NativeMethods.SwpNoActivate);
        Invalidate();
    }

    private async Task ReattachToTaskbarAfterStartupAsync()
    {
        await Task.Delay(200);
        if (!IsDisposed && IsHandleCreated && _useTaskbarMode)
        {
            PlaceWindow();
        }

        await Task.Delay(600);
        if (!IsDisposed && IsHandleCreated && _useTaskbarMode)
        {
            PlaceWindow();
        }
    }

    private void ApplyLayeredTransparency()
    {
        NativeMethods.SetLayeredWindowAttributes(
            Handle,
            ColorTranslator.ToWin32(TransparentBackColor),
            255,
            NativeMethods.LwaColorKey);
    }

    private void HandleTaskbarMouseDown(object? sender, MouseEventArgs e)
    {
        if (!_isTaskbarHosted || e.Button != MouseButtons.Left)
        {
            return;
        }

        _dragMode = _hoverMode == TaskbarDragMode.None ? GetDragMode(e.X) : _hoverMode;
        _dragStartMouseX = PointToScreen(e.Location).X;
        _dragStartLeft = Left;
        _dragStartWidth = Width;
        Capture = true;
    }

    private void HandleTaskbarMouseMove(object? sender, MouseEventArgs e)
    {
        if (!_isTaskbarHosted)
        {
            Cursor = Cursors.Default;
            _isTaskbarMouseInside = false;
            return;
        }

        if (_dragMode == TaskbarDragMode.None)
        {
            UpdateTaskbarHoverFromCursor();
            return;
        }

        var delta = PointToScreen(e.Location).X - _dragStartMouseX;
        var minWidth = 120;
        var maxLeft = Math.Max(0, _taskbarWidth - minWidth);
        var left = _dragStartLeft;
        var width = _dragStartWidth;

        switch (_dragMode)
        {
            case TaskbarDragMode.Move:
                left = Math.Clamp(_dragStartLeft + delta, 0, maxLeft);
                if (left + width > _taskbarWidth)
                {
                    width = Math.Max(minWidth, _taskbarWidth - left);
                }
                break;
            case TaskbarDragMode.ResizeLeft:
                left = Math.Clamp(_dragStartLeft + delta, 0, maxLeft);
                width = Math.Max(minWidth, _dragStartWidth + (_dragStartLeft - left));
                if (left + width > _taskbarWidth)
                {
                    width = _taskbarWidth - left;
                }
                break;
            case TaskbarDragMode.ResizeRight:
                width = Math.Clamp(_dragStartWidth + delta, minWidth, Math.Max(minWidth, _taskbarWidth - _dragStartLeft));
                break;
        }

        ApplyTaskbarBounds(left, width);
    }

    private void HandleTaskbarMouseUp(object? sender, MouseEventArgs e)
    {
        if (_dragMode == TaskbarDragMode.None)
        {
            return;
        }

        _dragMode = TaskbarDragMode.None;
        Capture = false;
        _hoverMode = TaskbarDragMode.None;
        UpdateTaskbarHoverFromCursor();

        SaveTaskbarPlacement();
    }

    private void UpdateTaskbarHoverFromCursor()
    {
        if (!_isTaskbarHosted || _dragMode != TaskbarDragMode.None)
        {
            return;
        }

        var mousePosition = Cursor.Position;
        if (!GetTaskbarWindowScreenBounds().Contains(mousePosition))
        {
            if (_isTaskbarMouseInside)
            {
                _isTaskbarMouseInside = false;
                _hoverMode = TaskbarDragMode.None;
                Cursor = Cursors.Default;
                _pageTimer.Start();
            }

            return;
        }

        _isTaskbarMouseInside = true;
        _pageTimer.Stop();
        _hoverMode = GetDragMode(PointToClient(mousePosition).X);
        var cursor = _hoverMode == TaskbarDragMode.Move ? Cursors.SizeAll : Cursors.SizeWE;
        Cursor = cursor;
        Cursor.Current = cursor;
    }

    private Rectangle GetTaskbarWindowScreenBounds()
    {
        return new Rectangle(PointToScreen(Point.Empty), ClientSize);
    }

    private TaskbarDragMode GetDragMode(int x)
    {
        if (x <= ResizeGripWidth)
        {
            return TaskbarDragMode.ResizeLeft;
        }

        if (x >= Width - ResizeGripWidth)
        {
            return TaskbarDragMode.ResizeRight;
        }

        return TaskbarDragMode.Move;
    }

    private void ApplyTaskbarBounds(int left, int width)
    {
        width = Math.Clamp(width, 120, Math.Max(120, _taskbarWidth));
        left = Math.Clamp(left, 0, Math.Max(0, _taskbarWidth - width));
        _taskbarLeftOffset = left;
        _taskbarRightOffset = Math.Max(0, _taskbarWidth - left - width);
        _configuredTaskbarContentWidth = width;
        MoveWindowIfNeeded(left, Top, width, Height);
    }

    private void SaveTaskbarPlacement()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "appsettings.json");
        try
        {
            JsonObject root;
            if (File.Exists(path))
            {
                root = JsonNode.Parse(File.ReadAllText(path, Encoding.UTF8)) as JsonObject ?? [];
            }
            else
            {
                root = [];
            }

            root["taskbarLeftOffset"] = _taskbarLeftOffset;
            root["taskbarRightOffset"] = _taskbarRightOffset;
            root["taskbarWidth"] = Width;

            var options = new JsonSerializerOptions
            {
                WriteIndented = true,
                Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
            };
            File.WriteAllText(path, root.ToJsonString(options), Encoding.UTF8);
        }
        catch
        {
            _notifyIcon.ShowBalloonTip(2500, "华尔街见闻要闻", "保存任务栏位置失败，请检查 appsettings.json 是否可写。", ToolTipIcon.Warning);
        }
    }

    private async Task RunAnimationLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if (!IsDisposed && IsHandleCreated)
                {
                    BeginInvoke(() =>
                    {
                        UpdateTaskbarHoverFromCursor();
                        Invalidate();
                    });
                }

                await Task.Delay(8, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (InvalidOperationException)
            {
                break;
            }
        }
    }

    private float CalculateTextX()
    {
        var travelDistance = Width + _tickerSize.Width;
        if (travelDistance <= 0)
        {
            return Width;
        }

        var moved = (_scrollWatch.Elapsed.TotalSeconds * _config.PixelsPerSecond) % travelDistance;
        return (float)(Width - moved);
    }

    private async Task RefreshNewsAsync()
    {
        try
        {
            var items = await _client.GetImportantNewsAsync();
            if (items.Count == 0)
            {
                SetTickerText($"[{DateTime.Now:HH:mm}] 暂无 score >= {_config.ImportantScoreThreshold} 的重要快讯");
                return;
            }

            SetNewsItems(items);
        }
        catch (Exception ex)
        {
            var cached = _items.Count > 0 ? "，继续显示上次内容" : "";
            SetTickerText($"[{DateTime.Now:HH:mm}] 获取快讯失败：{ex.Message}{cached}");
        }
    }

    private void SetTickerText(string text, bool isNewsText = false)
    {
        _tickerText = text;
        _tickerTextIsNews = isNewsText;
        using var graphics = CreateGraphics();
        _tickerSize = Size.Ceiling(graphics.MeasureString(_tickerText, _tickerFont));
        RestartScroll();
        Invalidate();
    }

    private void SetNewsItems(IReadOnlyList<LiveNewsItem> items)
    {
        _items = items;
        _currentPageIndex = Math.Clamp(_currentPageIndex, 0, Math.Max(0, _items.Count - 1));
        SetTickerText(FormatTicker(items), isNewsText: true);
    }

    private void ShowNextPage()
    {
        if (_items.Count == 0)
        {
            return;
        }

        _currentPageIndex = (_currentPageIndex + 1) % _items.Count;
        Invalidate();
    }

    private void OpenCurrentNews()
    {
        var uri = _items.Count == 0
            ? "https://wallstreetcn.com/live/global"
            : $"https://wallstreetcn.com/livenews/{_items[Math.Clamp(_currentPageIndex, 0, _items.Count - 1)].Id}";
        Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true });
    }

    private void RestartScroll()
    {
        _scrollWatch.Restart();
    }

    private static string FormatTicker(IEnumerable<LiveNewsItem> items)
    {
        return string.Join(TickerSeparator, items.Select(item =>
            FormatNewsItem(item)));
    }

    private void DrawTickerItems(Graphics graphics, Brush shadowBrush, float x, float y)
    {
        for (var i = 0; i < _items.Count; i++)
        {
            var item = _items[i];
            var text = FormatNewsItem(item);
            DrawTickerSegment(graphics, shadowBrush, text, item.Score > 1 ? _importantNewsColor : _newsTextColor, ref x, y);

            if (i < _items.Count - 1)
            {
                DrawTickerSegment(graphics, shadowBrush, TickerSeparator, _newsTextColor, ref x, y);
            }
        }
    }

    private void DrawTickerSegment(Graphics graphics, Brush shadowBrush, string text, Color textColor, ref float x, float y)
    {
        using var textBrush = new SolidBrush(textColor);
        graphics.DrawString(text, _tickerFont, shadowBrush, x + 1, y + 1);
        graphics.DrawString(text, _tickerFont, textBrush, x, y);
        x += graphics.MeasureString(text, _tickerFont).Width;
    }

    private static string FormatNewsItem(LiveNewsItem item)
    {
        var headline = string.IsNullOrWhiteSpace(item.Title)
            ? item.ContentText
            : item.ContentText.StartsWith(item.Title, StringComparison.Ordinal)
                ? item.ContentText
                : $"{item.Title}：{item.ContentText}";

        return $"[{item.DisplayTime:HH:mm}] {headline}";
    }

    private string GetCurrentPageText()
    {
        if (_items.Count == 0)
        {
            return _tickerText;
        }

        return FormatNewsItem(_items[Math.Clamp(_currentPageIndex, 0, _items.Count - 1)]);
    }

    private void DrawTaskbarPage(Graphics graphics)
    {
        var area = new RectangleF(0, 0, Math.Max(1, Width), Math.Max(1, Height));
        var (firstLine, secondLine) = GetCurrentPageLines();
        var lineHeight = _taskbarFont.GetHeight(graphics);
        var totalHeight = lineHeight * 2;
        var top = Math.Max(0, (area.Height - totalHeight) / 2);
        var firstArea = new RectangleF(area.Left, top, area.Width, lineHeight);
        var secondArea = new RectangleF(area.Left, top + lineHeight, area.Width, lineHeight);
        using var previousClip = graphics.Clip.Clone();
        graphics.SetClip(area);
        var textColor = GetCurrentNewsTextColor();
        DrawTaskbarLine(graphics, firstLine, Rectangle.Round(firstArea), textColor);
        DrawTaskbarLine(graphics, secondLine, Rectangle.Round(secondArea), textColor);
        graphics.Clip = previousClip;
    }

    private void DrawTaskbarLine(Graphics graphics, string text, Rectangle area, Color textColor)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }

        using var textBrush = new SolidBrush(textColor);
        using var format = new StringFormat(StringFormatFlags.NoWrap)
        {
            Alignment = StringAlignment.Near,
            LineAlignment = StringAlignment.Center,
            Trimming = StringTrimming.None
        };
        graphics.DrawString(text, _taskbarFont, textBrush, area, format);
    }

    private Color GetTickerTextColor()
    {
        return _tickerTextIsNews && _items.Any(item => item.Score > 1)
            ? _importantNewsColor
            : _newsTextColor;
    }

    private Color GetCurrentNewsTextColor()
    {
        if (_items.Count == 0)
        {
            return _newsTextColor;
        }

        var item = _items[Math.Clamp(_currentPageIndex, 0, _items.Count - 1)];
        return item.Score > 1 ? _importantNewsColor : _newsTextColor;
    }

    private (string FirstLine, string SecondLine) GetCurrentPageLines()
    {
        if (_items.Count == 0)
        {
            return (_tickerText, string.Empty);
        }

        var item = _items[Math.Clamp(_currentPageIndex, 0, _items.Count - 1)];
        if (!string.IsNullOrWhiteSpace(item.Title))
        {
            return ($"[{item.DisplayTime:HH:mm}] {item.Title}", item.ContentText);
        }

        return SplitUntitledNews($"[{item.DisplayTime:HH:mm}] {item.ContentText}");
    }

    private static (string FirstLine, string SecondLine) SplitUntitledNews(string text)
    {
        if (text.Length <= 44)
        {
            return (text, string.Empty);
        }

        var splitAt = Math.Min(44, Math.Max(1, text.Length - 1));
        return (text[..splitAt], text[splitAt..]);
    }

    private static Font CreateTickerFont(AppConfig config, float? overrideSize = null)
    {
        return CreateFont(config.FontFamily, overrideSize ?? config.FontSize);
    }

    private static Font CreateFont(string configuredFamily, float size)
    {
        var fontFamily = FontFamily.Families.Any(f => f.Name.Equals(configuredFamily, StringComparison.OrdinalIgnoreCase))
            ? configuredFamily
            : "Segoe UI";
        return new Font(fontFamily, size, FontStyle.Bold, GraphicsUnit.Point);
    }

    private static Color ParseColor(string configuredColor, Color fallback)
    {
        if (string.IsNullOrWhiteSpace(configuredColor))
        {
            return fallback;
        }

        try
        {
            var color = ColorTranslator.FromHtml(configuredColor.Trim());
            return color.IsEmpty ? fallback : color;
        }
        catch
        {
            return fallback;
        }
    }

    private static Color WithAlpha(Color color, int alpha)
    {
        return Color.FromArgb(Math.Clamp(alpha, 0, 255), color.R, color.G, color.B);
    }
}

internal sealed class LiveNewsClient : IDisposable
{
    private readonly AppConfig _config;
    private readonly HttpClient _httpClient = new();

    public LiveNewsClient(AppConfig config)
    {
        _config = config;
        _httpClient.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) WallStreetTicker/1.0");
        _httpClient.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        _httpClient.Timeout = TimeSpan.FromSeconds(15);
    }

    public async Task<IReadOnlyList<LiveNewsItem>> GetImportantNewsAsync()
    {
        using var response = await _httpClient.GetAsync(_config.ApiUrl);
        response.EnsureSuccessStatusCode();

        await using var stream = await response.Content.ReadAsStreamAsync();
        using var json = await JsonDocument.ParseAsync(stream);

        var items = json.RootElement
            .GetProperty("data")
            .GetProperty("items")
            .EnumerateArray()
            .Select(ParseItem)
            .Where(item => item.Score >= _config.ImportantScoreThreshold)
            .GroupBy(item => item.Id)
            .Select(group => group.First())
            .OrderByDescending(item => item.DisplayTime)
            .ToList();

        return items;
    }

    public void Dispose() => _httpClient.Dispose();

    private static LiveNewsItem ParseItem(JsonElement element)
    {
        var title = ReadString(element, "title");
        var contentText = CleanText(ReadString(element, "content_text"));
        var score = ReadInt(element, "score");
        var id = ReadLong(element, "id");
        var displayTime = DateTimeOffset.FromUnixTimeSeconds(ReadLong(element, "display_time")).LocalDateTime;

        return new LiveNewsItem(id, score, displayTime, title.Trim(), contentText);
    }

    private static string ReadString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? string.Empty
            : string.Empty;

    private static int ReadInt(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.TryGetInt32(out var result)
            ? result
            : 0;

    private static long ReadLong(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.TryGetInt64(out var result)
            ? result
            : 0;

    private static string CleanText(string text)
    {
        var withoutHtml = Regex.Replace(text, "<.*?>", string.Empty);
        return Regex.Replace(withoutHtml, @"\s+", " ").Trim();
    }
}

internal sealed record LiveNewsItem(long Id, int Score, DateTime DisplayTime, string Title, string ContentText);

internal enum TaskbarDragMode
{
    None,
    Move,
    ResizeLeft,
    ResizeRight
}

internal static class NativeMethods
{
    public const int WmNcHitTest = 0x0084;
    public const int WsExTransparent = 0x20;
    public const int WsExLayered = 0x80000;
    public const int WsExToolWindow = 0x80;
    public const int WsExNoActivate = 0x08000000;
    public const int SwpNoSize = 0x0001;
    public const int SwpNoMove = 0x0002;
    public const int SwpNoActivate = 0x0010;
    public const int SwpShowWindow = 0x0040;
    public const int SwShowNoActivate = 4;
    public const int LwaColorKey = 0x00000001;
    public static readonly IntPtr HwndTop = new(0);
    public static readonly IntPtr HtTransparent = new(-1);

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindow(string lpClassName, string? lpWindowName);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SetParent(IntPtr hWndChild, IntPtr hWndNewParent);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int nWidth, int nHeight, bool bRepaint);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetLayeredWindowAttributes(IntPtr hwnd, int crKey, byte bAlpha, int dwFlags);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out NativeRect lpRect);

    [System.Runtime.InteropServices.DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    public static extern uint TimeBeginPeriod(uint period);

    [System.Runtime.InteropServices.DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    public static extern uint TimeEndPeriod(uint period);
}

[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
internal struct NativeRect
{
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}
