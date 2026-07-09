#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <winhttp.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"WallStreetTickerCppWindow";
constexpr wchar_t kTrayMessageName[] = L"WallStreetTickerCppTrayMessage";
constexpr int kRefreshTimer = 1;
constexpr int kPlacementTimer = 2;
constexpr int kPageTimer = 3;
constexpr int kAnimationTimer = 4;
constexpr int kResizeGripWidth = 24;
constexpr UINT kMenuRefresh = 1001;
constexpr UINT kMenuToggleMode = 1002;
constexpr UINT kMenuOpenSource = 1003;
constexpr UINT kMenuExit = 1004;
constexpr COLORREF kTransparentColor = RGB(1, 1, 1);

UINT g_trayMessage = WM_APP + 1;

enum class DragMode {
    None,
    Move,
    ResizeLeft,
    ResizeRight
};

struct AppConfig {
    std::wstring apiUrl = L"https://api-one-wscn.awtmt.com/apiv1/content/lives?channel=global-channel&limit=50";
    int importantScoreThreshold = 2;
    std::wstring newsTextColor = L"#FFFFFF";
    std::wstring importantNewsColor = L"#FF0000";
    int textOpacity = 255;
    int refreshSeconds = 60;
    int pageSeconds = 6;
    int pixelsPerSecond = 125;
    int barHeight = 56;
    std::wstring position = L"taskbar";
    int taskbarLeftOffset = 360;
    int taskbarRightOffset = 240;
    int taskbarWidth = 0;
    std::wstring taskbarFontFamily = L"Microsoft YaHei UI";
    int taskbarFontSize = 9;
    std::wstring fontFamily = L"Microsoft YaHei UI";
    int fontSize = 18;
};

struct LiveNewsItem {
    long long id = 0;
    int score = 0;
    std::chrono::system_clock::time_point displayTime{};
    std::wstring title;
    std::wstring contentText;
    std::wstring uri;
};

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const auto slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

std::wstring SettingsPath() {
    return ExeDirectory() + L"\\appsettings.json";
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string ReadTextFileUtf8(const std::wstring& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    auto text = buffer.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::wstring JsonUnescape(std::string_view value) {
    std::string bytes;
    bytes.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch != '\\' || i + 1 >= value.size()) {
            bytes.push_back(ch);
            continue;
        }
        const char esc = value[++i];
        switch (esc) {
            case '"': bytes.push_back('"'); break;
            case '\\': bytes.push_back('\\'); break;
            case '/': bytes.push_back('/'); break;
            case 'b': bytes.push_back('\b'); break;
            case 'f': bytes.push_back('\f'); break;
            case 'n': bytes.push_back('\n'); break;
            case 'r': bytes.push_back('\r'); break;
            case 't': bytes.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= value.size()) {
                    break;
                }
                unsigned code = 0;
                for (int n = 0; n < 4; ++n) {
                    const char c = value[++i];
                    code <<= 4;
                    if (c >= '0' && c <= '9') code += c - '0';
                    else if (c >= 'a' && c <= 'f') code += 10 + c - 'a';
                    else if (c >= 'A' && c <= 'F') code += 10 + c - 'A';
                }
                wchar_t wide[2]{static_cast<wchar_t>(code), 0};
                bytes += WideToUtf8(wide);
                break;
            }
            default:
                bytes.push_back(esc);
                break;
        }
    }
    return Utf8ToWide(bytes);
}

std::wstring JsonStringField(const std::string& json, const std::string& name, const std::wstring& fallback = L"") {
    const std::regex pattern("\"" + name + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"");
    std::smatch match;
    return std::regex_search(json, match, pattern) ? JsonUnescape(match[1].str()) : fallback;
}

int JsonIntField(const std::string& json, const std::string& name, int fallback = 0) {
    const std::regex pattern("\"" + name + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        return fallback;
    }
    try {
        return std::stoi(match[1].str());
    } catch (...) {
        return fallback;
    }
}

std::string JsonEscapeKey(const std::string& name) {
    return "\"" + name + "\"";
}

std::string_view TopLevelJsonValue(const std::string& object, const std::string& name) {
    const std::string key = JsonEscapeKey(name);
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < object.size(); ++i) {
        const char ch = object[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            if (depth == 1 && object.compare(i, key.size(), key) == 0) {
                size_t pos = i + key.size();
                while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) ++pos;
                if (pos >= object.size() || object[pos] != ':') continue;
                ++pos;
                while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) ++pos;
                const size_t valueStart = pos;
                bool valueString = false;
                bool valueEscaped = false;
                int nestedDepth = 0;
                for (; pos < object.size(); ++pos) {
                    const char valueCh = object[pos];
                    if (valueString) {
                        if (valueEscaped) valueEscaped = false;
                        else if (valueCh == '\\') valueEscaped = true;
                        else if (valueCh == '"') valueString = false;
                        continue;
                    }
                    if (valueCh == '"') {
                        valueString = true;
                    } else if (valueCh == '{' || valueCh == '[') {
                        ++nestedDepth;
                    } else if (valueCh == '}' || valueCh == ']') {
                        if (nestedDepth == 0) break;
                        --nestedDepth;
                    } else if (valueCh == ',' && nestedDepth == 0) {
                        break;
                    }
                }
                size_t valueEnd = pos;
                while (valueEnd > valueStart && std::isspace(static_cast<unsigned char>(object[valueEnd - 1]))) --valueEnd;
                return std::string_view(object.data() + valueStart, valueEnd - valueStart);
            }
            inString = true;
        } else if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
        }
    }

    return {};
}

std::wstring TopLevelJsonStringField(const std::string& object, const std::string& name, const std::wstring& fallback = L"") {
    auto value = TopLevelJsonValue(object, name);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return fallback;
    }
    return JsonUnescape(value.substr(1, value.size() - 2));
}

long long TopLevelJsonLongField(const std::string& object, const std::string& name, long long fallback = 0) {
    auto value = TopLevelJsonValue(object, name);
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoll(std::string(value));
    } catch (...) {
        return fallback;
    }
}

int TopLevelJsonIntField(const std::string& object, const std::string& name, int fallback = 0) {
    return static_cast<int>(TopLevelJsonLongField(object, name, fallback));
}

AppConfig LoadConfig() {
    AppConfig config;
    const auto text = ReadTextFileUtf8(SettingsPath());
    if (text.empty()) {
        return config;
    }

    config.apiUrl = JsonStringField(text, "apiUrl", config.apiUrl);
    config.importantScoreThreshold = JsonIntField(text, "importantScoreThreshold", config.importantScoreThreshold);
    config.newsTextColor = JsonStringField(text, "newsTextColor", config.newsTextColor);
    config.importantNewsColor = JsonStringField(text, "importantNewsColor", config.importantNewsColor);
    config.textOpacity = std::clamp(JsonIntField(text, "textOpacity", config.textOpacity), 0, 255);
    config.refreshSeconds = std::max(15, JsonIntField(text, "refreshSeconds", config.refreshSeconds));
    config.pageSeconds = std::clamp(JsonIntField(text, "pageSeconds", config.pageSeconds), 2, 60);
    config.pixelsPerSecond = std::clamp(JsonIntField(text, "pixelsPerSecond", JsonIntField(text, "pixelsPerTick", 2) * 60), 30, 600);
    config.barHeight = std::clamp(JsonIntField(text, "barHeight", config.barHeight), 24, 120);
    config.position = JsonStringField(text, "position", config.position);
    config.taskbarLeftOffset = std::clamp(JsonIntField(text, "taskbarLeftOffset", config.taskbarLeftOffset), 0, 10000);
    config.taskbarRightOffset = std::clamp(JsonIntField(text, "taskbarRightOffset", config.taskbarRightOffset), 0, 10000);
    config.taskbarWidth = std::clamp(JsonIntField(text, "taskbarWidth", config.taskbarWidth), 0, 10000);
    config.fontFamily = JsonStringField(text, "fontFamily", config.fontFamily);
    config.fontSize = std::clamp(JsonIntField(text, "fontSize", config.fontSize), 10, 42);
    config.taskbarFontFamily = JsonStringField(text, "taskbarFontFamily", JsonStringField(text, "fontFamily", config.taskbarFontFamily));
    config.taskbarFontSize = std::clamp(JsonIntField(text, "taskbarFontSize", config.taskbarFontSize), 6, 24);
    return config;
}

COLORREF ParseColor(const std::wstring& value, COLORREF fallback) {
    if (value.size() == 7 && value[0] == L'#') {
        try {
            const int r = std::stoi(value.substr(1, 2), nullptr, 16);
            const int g = std::stoi(value.substr(3, 2), nullptr, 16);
            const int b = std::stoi(value.substr(5, 2), nullptr, 16);
            return RGB(r, g, b);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

std::wstring FormatTime(const std::chrono::system_clock::time_point& time) {
    const auto raw = std::chrono::system_clock::to_time_t(time);
    tm local{};
    localtime_s(&local, &raw);
    wchar_t buffer[16]{};
    wcsftime(buffer, std::size(buffer), L"%H:%M", &local);
    return buffer;
}

std::wstring FormatNewsItem(const LiveNewsItem& item) {
    std::wstring headline;
    if (item.title.empty()) {
        headline = item.contentText;
    } else if (item.contentText.rfind(item.title, 0) == 0) {
        headline = item.contentText;
    } else {
        headline = item.title + L"：" + item.contentText;
    }
    return L"[" + FormatTime(item.displayTime) + L"] " + headline;
}

std::wstring StripHtmlAndWhitespace(std::wstring text) {
    static const std::wregex tagPattern(L"<[^>]*>");
    text = std::regex_replace(text, tagPattern, L"");
    std::wstring result;
    bool inSpace = false;
    for (const wchar_t ch : text) {
        if (std::iswspace(ch)) {
            if (!inSpace) {
                result.push_back(L' ');
                inSpace = true;
            }
        } else {
            result.push_back(ch);
            inSpace = false;
        }
    }
    while (!result.empty() && result.front() == L' ') result.erase(result.begin());
    while (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}

bool CrackUrl(const std::wstring& url, URL_COMPONENTSW& parts, std::wstring& host, std::wstring& path) {
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    wchar_t hostBuffer[256]{};
    wchar_t pathBuffer[4096]{};
    parts.lpszHostName = hostBuffer;
    parts.dwHostNameLength = std::size(hostBuffer);
    parts.lpszUrlPath = pathBuffer;
    parts.dwUrlPathLength = std::size(pathBuffer);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        return false;
    }
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    return true;
}

std::string HttpGetUtf8(const std::wstring& url) {
    URL_COMPONENTSW parts{};
    std::wstring host;
    std::wstring path;
    if (!CrackUrl(url, parts, host, path)) {
        throw std::runtime_error("bad url");
    }

    HINTERNET session = WinHttpOpen(L"WallStreetTickerCpp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("WinHttpOpen failed");
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        throw std::runtime_error("WinHttpConnect failed");
    }

    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("WinHttpOpenRequest failed");
    }

    const wchar_t headers[] = L"Accept: application/json\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) WallStreetTickerCpp/1.0\r\n";
    if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("request failed");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("http status " + std::to_string(status));
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return body;
}

std::vector<std::string> ExtractJsonObjectsFromItems(const std::string& json) {
    const auto itemsPos = json.find("\"items\"");
    if (itemsPos == std::string::npos) return {};
    const auto arrayStart = json.find('[', itemsPos);
    if (arrayStart == std::string::npos) return {};

    std::vector<std::string> objects;
    int arrayDepth = 0;
    int objectDepth = 0;
    bool inString = false;
    bool escaped = false;
    size_t objectStart = std::string::npos;
    for (size_t i = arrayStart; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '[') ++arrayDepth;
        else if (ch == ']') {
            --arrayDepth;
            if (arrayDepth == 0) break;
        } else if (ch == '{') {
            if (objectDepth == 0) objectStart = i;
            ++objectDepth;
        } else if (ch == '}') {
            --objectDepth;
            if (objectDepth == 0 && objectStart != std::string::npos) {
                objects.push_back(json.substr(objectStart, i - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }
    return objects;
}

std::vector<LiveNewsItem> ParseNewsItems(const std::string& json, int threshold) {
    std::vector<LiveNewsItem> items;
    std::set<long long> seen;
    for (const auto& object : ExtractJsonObjectsFromItems(json)) {
        LiveNewsItem item;
        item.id = TopLevelJsonLongField(object, "id");
        item.score = TopLevelJsonIntField(object, "score");
        if (item.score < threshold || seen.count(item.id)) {
            continue;
        }
        seen.insert(item.id);
        item.title = TopLevelJsonStringField(object, "title");
        item.contentText = StripHtmlAndWhitespace(TopLevelJsonStringField(object, "content_text"));
        item.uri = TopLevelJsonStringField(object, "uri");
        const auto displayTime = TopLevelJsonLongField(object, "display_time");
        item.displayTime = std::chrono::system_clock::from_time_t(static_cast<time_t>(displayTime));
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const LiveNewsItem& a, const LiveNewsItem& b) {
        return a.displayTime > b.displayTime;
    });
    return items;
}

HFONT CreateUiFont(const std::wstring& family, int points, int weight) {
    HDC screen = GetDC(nullptr);
    const int height = -MulDiv(points, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, family.c_str());
}

class TickerApp {
public:
    explicit TickerApp(HINSTANCE instance) : instance_(instance), config_(LoadConfig()) {
        isBottom_ = Lower(config_.position) == L"bottom";
        useTaskbarMode_ = Lower(config_.position) == L"taskbar";
        taskbarLeftOffset_ = config_.taskbarLeftOffset;
        taskbarRightOffset_ = config_.taskbarRightOffset;
        configuredTaskbarContentWidth_ = config_.taskbarWidth;
        newsColor_ = ParseColor(config_.newsTextColor, RGB(255, 255, 255));
        importantColor_ = ParseColor(config_.importantNewsColor, RGB(255, 0, 0));
        tickerFont_ = CreateUiFont(config_.fontFamily, config_.fontSize, FW_BOLD);
        taskbarFont_ = CreateUiFont(config_.taskbarFontFamily, config_.taskbarFontSize, FW_BOLD);
    }

    ~TickerApp() {
        if (tickerFont_) DeleteObject(tickerFont_);
        if (taskbarFont_) DeleteObject(taskbarFont_);
    }

    int Run() {
        g_trayMessage = RegisterWindowMessageW(kTrayMessageName);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(kTransparentColor);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kWindowClass, L"华尔街见闻要闻跑马灯", WS_POPUP,
                                0, 0, 800, config_.barHeight, nullptr, nullptr, instance_, this);
        if (!hwnd_) {
            return 1;
        }

        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);
        MessageLoop();
        return 0;
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* app = reinterpret_cast<TickerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* creates = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<TickerApp*>(creates->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        }
        if (app) {
            return app->HandleMessage(message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void MessageLoop() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == g_trayMessage) {
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                RefreshNews();
            }
            return 0;
        }

        switch (message) {
            case WM_CREATE:
                OnCreate();
                return 0;
            case WM_DESTROY:
                OnDestroy();
                return 0;
            case WM_NCHITTEST:
                if (!isTaskbarHosted_) return HTTRANSPARENT;
                break;
            case WM_TIMER:
                OnTimer(static_cast<int>(wParam));
                return 0;
            case WM_PAINT:
                OnPaint();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONDOWN:
                OnMouseDown(GET_X_LPARAM(lParam));
                return 0;
            case WM_MOUSEMOVE:
                OnMouseMove(GET_X_LPARAM(lParam));
                return 0;
            case WM_LBUTTONUP:
                OnMouseUp();
                return 0;
            case WM_MOUSELEAVE:
                isTaskbarMouseInside_ = false;
                hoverMode_ = DragMode::None;
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                SetTimer(hwnd_, kPageTimer, config_.pageSeconds * 1000, nullptr);
                return 0;
            case WM_LBUTTONDBLCLK:
                OpenCurrentNews();
                return 0;
            case WM_COMMAND:
                OnCommand(LOWORD(wParam));
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void OnCreate() {
        ApplyTransparency();
        CreateTrayIcon();
        SetTimer(hwnd_, kRefreshTimer, config_.refreshSeconds * 1000, nullptr);
        SetTimer(hwnd_, kPlacementTimer, 2000, nullptr);
        SetTimer(hwnd_, kPageTimer, config_.pageSeconds * 1000, nullptr);
        SetTimer(hwnd_, kAnimationTimer, 16, nullptr);
        timeBeginPeriod(1);
        PlaceWindow();
        SetTickerText(L"正在加载华尔街见闻重要快讯...", false);
        RefreshNews();
    }

    void OnDestroy() {
        timeEndPeriod(1);
        KillTimer(hwnd_, kRefreshTimer);
        KillTimer(hwnd_, kPlacementTimer);
        KillTimer(hwnd_, kPageTimer);
        KillTimer(hwnd_, kAnimationTimer);
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        PostQuitMessage(0);
    }

    void OnTimer(int id) {
        if (id == kRefreshTimer) {
            RefreshNews();
        } else if (id == kPlacementTimer) {
            PlaceWindow();
        } else if (id == kPageTimer) {
            ShowNextPage();
        } else if (id == kAnimationTimer) {
            UpdateTaskbarHoverFromCursor();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void CreateTrayIcon() {
        ZeroMemory(&trayIcon_, sizeof(trayIcon_));
        trayIcon_.cbSize = sizeof(trayIcon_);
        trayIcon_.hWnd = hwnd_;
        trayIcon_.uID = 1;
        trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        trayIcon_.uCallbackMessage = g_trayMessage;
        trayIcon_.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
        wcscpy_s(trayIcon_.szTip, L"华尔街见闻要闻跑马灯");
        Shell_NotifyIconW(NIM_ADD, &trayIcon_);
    }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuRefresh, L"刷新");
        AppendMenuW(menu, MF_STRING, kMenuToggleMode, L"切换任务栏/悬浮");
        AppendMenuW(menu, MF_STRING, kMenuOpenSource, L"打开来源网页");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    void OnCommand(UINT id) {
        if (id == kMenuRefresh) {
            RefreshNews();
        } else if (id == kMenuToggleMode) {
            useTaskbarMode_ = !useTaskbarMode_;
            if (!useTaskbarMode_) {
                SetParent(hwnd_, nullptr);
                isTaskbarHosted_ = false;
            }
            PlaceWindow();
        } else if (id == kMenuOpenSource) {
            ShellExecuteW(nullptr, L"open", L"https://wallstreetcn.com/live/global", nullptr, nullptr, SW_SHOWNORMAL);
        } else if (id == kMenuExit) {
            DestroyWindow(hwnd_);
        }
    }

    void PlaceWindow() {
        if (useTaskbarMode_ && TryPlaceInTaskbar()) {
            return;
        }

        isTaskbarHosted_ = false;
        SetParent(hwnd_, nullptr);
        ApplyTransparency();

        RECT area{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        const int width = area.right - area.left;
        const int top = isBottom_ ? area.bottom - config_.barHeight : area.top;
        MoveWindowIfNeeded(area.left, top, width, config_.barHeight);
        SetWindowPos(hwnd_, HWND_TOPMOST, area.left, top, width, config_.barHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    bool TryPlaceInTaskbar() {
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        RECT rect{};
        if (!taskbar || !GetWindowRect(taskbar, &rect)) {
            return false;
        }
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) {
            return false;
        }

        if (!isTaskbarHosted_) {
            SetParent(hwnd_, taskbar);
            isTaskbarHosted_ = true;
            ApplyTransparency();
        }

        if (width >= height) {
            taskbarWidth_ = width;
            const int left = std::min(taskbarLeftOffset_, std::max(0, width - 120));
            int contentWidth = 0;
            if (configuredTaskbarContentWidth_ > 0) {
                contentWidth = std::clamp(configuredTaskbarContentWidth_, 120, std::max(120, width - left));
            } else {
                const int right = std::min(taskbarRightOffset_, std::max(0, width - left - 120));
                contentWidth = std::max(120, width - left - right);
            }
            taskbarRightOffset_ = std::max(0, width - left - contentWidth);
            const int barHeight = std::min(config_.barHeight, height);
            const int top = std::max(0, (height - barHeight) / 2);
            MoveWindowIfNeeded(left, top, contentWidth, barHeight);
        } else {
            const int top = std::min(96, std::max(0, height - 120));
            const int bottom = std::min(96, std::max(0, height - top - 120));
            const int contentHeight = std::max(120, height - top - bottom);
            const int contentWidth = std::min(config_.barHeight * 4, width);
            const int left = std::max(0, (width - contentWidth) / 2);
            MoveWindowIfNeeded(left, top, contentWidth, contentHeight);
        }

        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }

    void MoveWindowIfNeeded(int left, int top, int width, int height) {
        RECT current{};
        GetWindowRect(hwnd_, &current);
        POINT origin{current.left, current.top};
        if (isTaskbarHosted_) {
            origin = {0, 0};
        }
        if (origin.x == left && origin.y == top && current.right - current.left == width && current.bottom - current.top == height) {
            return;
        }
        SetWindowPos(hwnd_, HWND_TOP, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RestartScroll();
    }

    void ApplyTransparency() {
        SetLayeredWindowAttributes(hwnd_, kTransparentColor, static_cast<BYTE>(config_.textOpacity), LWA_COLORKEY | LWA_ALPHA);
    }

    void RefreshNews() {
        try {
            const auto body = HttpGetUtf8(config_.apiUrl);
            auto fresh = ParseNewsItems(body, config_.importantScoreThreshold);
            if (fresh.empty()) {
                SetTickerText(L"[" + FormatTime(std::chrono::system_clock::now()) + L"] 暂无 score >= " +
                              std::to_wstring(config_.importantScoreThreshold) + L" 的重要快讯", false);
                return;
            }
            items_ = std::move(fresh);
            currentPageIndex_ = std::clamp(currentPageIndex_, 0, static_cast<int>(items_.size()) - 1);
            SetTickerText(FormatTicker(), true);
        } catch (const std::exception& ex) {
            const std::wstring cached = items_.empty() ? L"" : L"，继续显示上次内容";
            SetTickerText(L"[" + FormatTime(std::chrono::system_clock::now()) + L"] 获取快讯失败：" +
                          Utf8ToWide(ex.what()) + cached, false);
        }
    }

    std::wstring FormatTicker() const {
        std::wstring result;
        for (size_t i = 0; i < items_.size(); ++i) {
            if (i > 0) result += L"    |    ";
            result += FormatNewsItem(items_[i]);
        }
        return result;
    }

    void SetTickerText(std::wstring text, bool isNews) {
        tickerText_ = std::move(text);
        tickerTextIsNews_ = isNews;
        MeasureTickerText();
        RestartScroll();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MeasureTickerText() {
        HDC dc = GetDC(hwnd_);
        HFONT old = static_cast<HFONT>(SelectObject(dc, tickerFont_));
        SIZE size{};
        GetTextExtentPoint32W(dc, tickerText_.c_str(), static_cast<int>(tickerText_.size()), &size);
        tickerTextSize_ = size;
        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
    }

    void RestartScroll() {
        scrollStart_ = std::chrono::steady_clock::now();
    }

    float CalculateTextX() const {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const int width = rect.right - rect.left;
        const int travel = width + tickerTextSize_.cx;
        if (travel <= 0) return static_cast<float>(width);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - scrollStart_).count();
        const auto moved = std::fmod(elapsed * config_.pixelsPerSecond, static_cast<double>(travel));
        return static_cast<float>(width - moved);
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT rect{};
        GetClientRect(hwnd_, &rect);

        HDC memDc = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max<LONG>(1, rect.right), std::max<LONG>(1, rect.bottom));
        HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
        HBRUSH transparent = CreateSolidBrush(kTransparentColor);
        FillRect(memDc, &rect, transparent);
        DeleteObject(transparent);
        SetBkMode(memDc, TRANSPARENT);

        if (isTaskbarHosted_) {
            DrawTaskbarPage(memDc, rect);
        } else {
            DrawTicker(memDc, rect);
        }

        BitBlt(dc, 0, 0, rect.right, rect.bottom, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd_, &ps);
    }

    void DrawTicker(HDC dc, const RECT& rect) {
        if (tickerText_.empty()) return;
        HFONT old = static_cast<HFONT>(SelectObject(dc, tickerFont_));
        const int y = static_cast<int>(std::max<LONG>(0, ((rect.bottom - rect.top) - tickerTextSize_.cy) / 2));
        int x = static_cast<int>(CalculateTextX());
        if (tickerTextIsNews_ && !items_.empty()) {
            for (size_t i = 0; i < items_.size(); ++i) {
                DrawTextSegment(dc, FormatNewsItem(items_[i]), items_[i].score > 1 ? importantColor_ : newsColor_, x, y);
                if (i + 1 < items_.size()) {
                    DrawTextSegment(dc, L"    |    ", newsColor_, x, y);
                }
            }
        } else {
            DrawTextSegment(dc, tickerText_, GetTickerTextColor(), x, y);
        }
        SelectObject(dc, old);
    }

    void DrawTextSegment(HDC dc, const std::wstring& text, COLORREF color, int& x, int y) {
        SetTextColor(dc, RGB(0, 0, 0));
        TextOutW(dc, x + 1, y + 1, text.c_str(), static_cast<int>(text.size()));
        SetTextColor(dc, color);
        TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        x += size.cx;
    }

    void DrawTaskbarPage(HDC dc, const RECT& rect) {
        HFONT old = static_cast<HFONT>(SelectObject(dc, taskbarFont_));
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        const int lineHeight = metrics.tmHeight + metrics.tmExternalLeading;
        const int totalHeight = lineHeight * 2;
        const int top = static_cast<int>(std::max<LONG>(0, (rect.bottom - rect.top - totalHeight) / 2));
        auto lines = GetCurrentPageLines();
        SetTextColor(dc, GetCurrentNewsTextColor());
        RECT first{0, top, rect.right, top + lineHeight};
        RECT second{0, top + lineHeight, rect.right, top + lineHeight * 2};
        DrawTextW(dc, lines.first.c_str(), static_cast<int>(lines.first.size()), &first, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        DrawTextW(dc, lines.second.c_str(), static_cast<int>(lines.second.size()), &second, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        SelectObject(dc, old);
    }

    COLORREF GetTickerTextColor() const {
        if (!tickerTextIsNews_) return newsColor_;
        return std::any_of(items_.begin(), items_.end(), [](const LiveNewsItem& item) { return item.score > 1; }) ? importantColor_ : newsColor_;
    }

    COLORREF GetCurrentNewsTextColor() const {
        if (items_.empty()) return newsColor_;
        const auto& item = items_[std::clamp(currentPageIndex_, 0, static_cast<int>(items_.size()) - 1)];
        return item.score > 1 ? importantColor_ : newsColor_;
    }

    std::pair<std::wstring, std::wstring> GetCurrentPageLines() const {
        if (items_.empty()) return {tickerText_, L""};
        const auto& item = items_[std::clamp(currentPageIndex_, 0, static_cast<int>(items_.size()) - 1)];
        if (!item.title.empty()) {
            return {L"[" + FormatTime(item.displayTime) + L"] " + item.title, item.contentText};
        }
        auto text = L"[" + FormatTime(item.displayTime) + L"] " + item.contentText;
        if (text.size() <= 44) return {text, L""};
        return {text.substr(0, 44), text.substr(44)};
    }

    void ShowNextPage() {
        if (items_.empty()) return;
        currentPageIndex_ = (currentPageIndex_ + 1) % static_cast<int>(items_.size());
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void OpenCurrentNews() {
        std::wstring url = L"https://wallstreetcn.com/live/global";
        if (!items_.empty()) {
            const auto& item = items_[std::clamp(currentPageIndex_, 0, static_cast<int>(items_.size()) - 1)];
            url = item.uri.empty() ? L"https://wallstreetcn.com/livenews/" + std::to_wstring(item.id) : item.uri;
        }
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void OnMouseDown(int x) {
        if (!isTaskbarHosted_) return;
        dragMode_ = hoverMode_ == DragMode::None ? GetDragMode(x) : hoverMode_;
        POINT point{};
        GetCursorPos(&point);
        dragStartMouseX_ = point.x;
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        HWND parent = GetParent(hwnd_);
        POINT origin{rect.left, rect.top};
        if (parent) ScreenToClient(parent, &origin);
        dragStartLeft_ = origin.x;
        dragStartWidth_ = rect.right - rect.left;
        SetCapture(hwnd_);
    }

    void OnMouseMove(int) {
        if (!isTaskbarHosted_) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return;
        }
        if (dragMode_ == DragMode::None) {
            TrackMouseLeave();
            UpdateTaskbarHoverFromCursor();
            return;
        }

        POINT point{};
        GetCursorPos(&point);
        const int delta = point.x - dragStartMouseX_;
        constexpr int minWidth = 120;
        const int maxLeft = std::max(0, taskbarWidth_ - minWidth);
        int left = dragStartLeft_;
        int width = dragStartWidth_;

        if (dragMode_ == DragMode::Move) {
            left = std::clamp(dragStartLeft_ + delta, 0, maxLeft);
            if (left + width > taskbarWidth_) width = std::max(minWidth, taskbarWidth_ - left);
        } else if (dragMode_ == DragMode::ResizeLeft) {
            left = std::clamp(dragStartLeft_ + delta, 0, maxLeft);
            width = std::max(minWidth, dragStartWidth_ + (dragStartLeft_ - left));
            if (left + width > taskbarWidth_) width = taskbarWidth_ - left;
        } else if (dragMode_ == DragMode::ResizeRight) {
            width = std::clamp(dragStartWidth_ + delta, minWidth, std::max(minWidth, taskbarWidth_ - dragStartLeft_));
        }

        ApplyTaskbarBounds(left, width);
    }

    void OnMouseUp() {
        if (dragMode_ == DragMode::None) return;
        dragMode_ = DragMode::None;
        ReleaseCapture();
        hoverMode_ = DragMode::None;
        UpdateTaskbarHoverFromCursor();
        SaveTaskbarPlacement();
    }

    void TrackMouseLeave() {
        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = hwnd_;
        TrackMouseEvent(&event);
    }

    DragMode GetDragMode(int x) const {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const int width = rect.right - rect.left;
        if (x <= kResizeGripWidth) return DragMode::ResizeLeft;
        if (x >= width - kResizeGripWidth) return DragMode::ResizeRight;
        return DragMode::Move;
    }

    void UpdateTaskbarHoverFromCursor() {
        if (!isTaskbarHosted_ || dragMode_ != DragMode::None) return;
        POINT point{};
        GetCursorPos(&point);
        RECT bounds{};
        GetWindowRect(hwnd_, &bounds);
        if (!PtInRect(&bounds, point)) {
            if (isTaskbarMouseInside_) {
                isTaskbarMouseInside_ = false;
                hoverMode_ = DragMode::None;
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                SetTimer(hwnd_, kPageTimer, config_.pageSeconds * 1000, nullptr);
            }
            return;
        }
        isTaskbarMouseInside_ = true;
        KillTimer(hwnd_, kPageTimer);
        POINT client = point;
        ScreenToClient(hwnd_, &client);
        hoverMode_ = GetDragMode(client.x);
        SetCursor(LoadCursor(nullptr, hoverMode_ == DragMode::Move ? IDC_SIZEALL : IDC_SIZEWE));
    }

    void ApplyTaskbarBounds(int left, int width) {
        width = std::clamp(width, 120, std::max(120, taskbarWidth_));
        left = std::clamp(left, 0, std::max(0, taskbarWidth_ - width));
        taskbarLeftOffset_ = left;
        taskbarRightOffset_ = std::max(0, taskbarWidth_ - left - width);
        configuredTaskbarContentWidth_ = width;
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        HWND parent = GetParent(hwnd_);
        POINT origin{rect.left, rect.top};
        if (parent) ScreenToClient(parent, &origin);
        MoveWindowIfNeeded(left, origin.y, width, rect.bottom - rect.top);
    }

    void SaveTaskbarPlacement() {
        std::map<std::string, std::string> values;
        values["apiUrl"] = "\"" + WideToUtf8(config_.apiUrl) + "\"";
        values["importantScoreThreshold"] = std::to_string(config_.importantScoreThreshold);
        values["newsTextColor"] = "\"" + WideToUtf8(config_.newsTextColor) + "\"";
        values["importantNewsColor"] = "\"" + WideToUtf8(config_.importantNewsColor) + "\"";
        values["textOpacity"] = std::to_string(config_.textOpacity);
        values["refreshSeconds"] = std::to_string(config_.refreshSeconds);
        values["pageSeconds"] = std::to_string(config_.pageSeconds);
        values["pixelsPerSecond"] = std::to_string(config_.pixelsPerSecond);
        values["barHeight"] = std::to_string(config_.barHeight);
        values["position"] = "\"" + WideToUtf8(config_.position) + "\"";
        values["taskbarLeftOffset"] = std::to_string(taskbarLeftOffset_);
        values["taskbarRightOffset"] = std::to_string(taskbarRightOffset_);
        values["taskbarWidth"] = std::to_string(configuredTaskbarContentWidth_);
        values["taskbarFontFamily"] = "\"" + WideToUtf8(config_.taskbarFontFamily) + "\"";
        values["taskbarFontSize"] = std::to_string(config_.taskbarFontSize);
        values["fontFamily"] = "\"" + WideToUtf8(config_.fontFamily) + "\"";
        values["fontSize"] = std::to_string(config_.fontSize);

        const auto path = SettingsPath();
        std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << "{\n";
        bool first = true;
        for (const auto& [key, value] : values) {
            if (!first) file << ",\n";
            first = false;
            file << "  \"" << key << "\": " << value;
        }
        file << "\n}\n";
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    NOTIFYICONDATAW trayIcon_{};
    AppConfig config_;
    HFONT tickerFont_{};
    HFONT taskbarFont_{};
    COLORREF newsColor_{RGB(255, 255, 255)};
    COLORREF importantColor_{RGB(255, 0, 0)};
    std::vector<LiveNewsItem> items_;
    std::wstring tickerText_;
    SIZE tickerTextSize_{};
    bool tickerTextIsNews_ = false;
    int currentPageIndex_ = 0;
    int taskbarLeftOffset_ = 0;
    int taskbarRightOffset_ = 0;
    int configuredTaskbarContentWidth_ = 0;
    int taskbarWidth_ = 0;
    int dragStartMouseX_ = 0;
    int dragStartLeft_ = 0;
    int dragStartWidth_ = 0;
    DragMode hoverMode_ = DragMode::None;
    DragMode dragMode_ = DragMode::None;
    bool isTaskbarMouseInside_ = false;
    bool isBottom_ = false;
    bool useTaskbarMode_ = false;
    bool isTaskbarHosted_ = false;
    std::chrono::steady_clock::time_point scrollStart_ = std::chrono::steady_clock::now();
};

}  // namespace

int wmain() {
    TickerApp app(GetModuleHandleW(nullptr));
    return app.Run();
}
