#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "MainWindow.h"

#include "DiscordClient.h"
#include "StringUtil.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

constexpr int WM_APP_STATUS    = WM_APP + 1;
constexpr int WM_APP_SPAM_DONE = WM_APP + 2;

constexpr int IDC_TOKEN        = 1001;
constexpr int IDC_MESSAGE      = 1002;
constexpr int IDC_INTERVAL     = 1003;
constexpr int IDC_MAXMSG       = 1004;
constexpr int IDC_TURBO        = 1005;
constexpr int IDC_SERVER       = 1006;
constexpr int IDC_CHANNELS     = 1007;
constexpr int IDC_BTN_VALIDATE = 1010;
constexpr int IDC_BTN_SERVERS  = 1011;
constexpr int IDC_BTN_CHANNELS = 1012;
constexpr int IDC_BTN_START    = 1013;
constexpr int IDC_BTN_STOP     = 1014;
constexpr int IDC_SRC_GUILD    = 1015;
constexpr int IDC_SRC_DM       = 1016;
constexpr int IDC_KIND_AUTO    = 1019;
constexpr int IDC_KIND_BOT     = 1020;
constexpr int IDC_KIND_USER    = 1021;
constexpr int IDC_STATUS_DOT   = 1022;

// --- Green dark palette ---
constexpr COLORREF kBg        = RGB(24, 30, 26);    // window background
constexpr COLORREF kCard      = RGB(34, 44, 38);    // controls / cards
constexpr COLORREF kCardAlt   = RGB(46, 60, 52);    // hover
constexpr COLORREF kBorder    = RGB(60, 84, 70);
constexpr COLORREF kText      = RGB(220, 230, 222);
constexpr COLORREF kTextDim   = RGB(140, 160, 146);
constexpr COLORREF kAccent    = RGB(46, 160, 89);   // primary green
constexpr COLORREF kAccentHov = RGB(70, 190, 110);
constexpr COLORREF kDanger    = RGB(214, 84, 84);
constexpr COLORREF kDangerHov = RGB(235, 110, 110);
constexpr COLORREF kSuccess   = RGB(80, 200, 120);
constexpr COLORREF kIdle      = RGB(140, 160, 146);

enum class ChannelSource { Guild, Dm };

enum class BtnStyle { Default, Primary, Danger };

struct ButtonState {
    BtnStyle style = BtnStyle::Default;
    bool hover = false;
    bool pressed = false;
    WNDPROC oldProc = nullptr;
};

struct GuiState {
    HWND hwnd = nullptr;
    HWND editToken = nullptr;
    HWND editMessage = nullptr;
    HWND editInterval = nullptr;
    HWND editMaxMsg = nullptr;
    HWND comboServer = nullptr;
    HWND listChannels = nullptr;
    HWND chkTurbo = nullptr;
    HWND radioGuild = nullptr;
    HWND radioDm = nullptr;
    HWND radioKindAuto = nullptr;
    HWND radioKindBot = nullptr;
    HWND radioKindUser = nullptr;
    HWND lblStatus = nullptr;
    HWND lblStatusDot = nullptr;
    HWND btnValidate = nullptr;
    HWND btnServers = nullptr;
    HWND btnChannels = nullptr;
    HWND btnStart = nullptr;
    HWND btnStop = nullptr;

    HBRUSH brushBg = nullptr;
    HBRUSH brushCard = nullptr;
    HFONT fontUi = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontHeader = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontButton = nullptr;

    COLORREF statusColor = kIdle;

    std::unordered_map<HWND, ButtonState> buttons;

    std::unique_ptr<DiscordClient> client;
    std::vector<GuildInfo> guilds;
    std::vector<ChannelInfo> channels;

    std::atomic<bool> stopSpam{ false };
    std::atomic<bool> spamRunning{ false };
    std::thread worker;
    std::mutex workerMutex;
};

GuiState* g_state = nullptr;

// ---------- drawing helpers ----------

void FillRect(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    ::FillRect(hdc, &rc, br);
    DeleteObject(br);
}

void DrawRoundedRect(HDC hdc, RECT rc, int radius, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

void DrawTextCentered(HDC hdc, const wchar_t* text, RECT rc, COLORREF color, HFONT font) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

// ---------- custom buttons ----------

LRESULT CALLBACK ButtonSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void RegisterButton(HWND btn, BtnStyle style) {
    ButtonState s;
    s.style = style;
    s.oldProc = (WNDPROC)SetWindowLongPtrW(btn, GWLP_WNDPROC, (LONG_PTR)ButtonSubclass);
    g_state->buttons[btn] = s;
}

LRESULT CALLBACK ButtonSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto it = g_state->buttons.find(hwnd);
    if (it == g_state->buttons.end()) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    ButtonState& st = it->second;

    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!st.hover) {
            st.hover = true;
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        st.hover = false;
        st.pressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
        st.pressed = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONUP: {
        st.pressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        const bool enabled = IsWindowEnabled(hwnd) != 0;

        COLORREF fill = kCard, border = kBorder, text = kText;
        switch (st.style) {
        case BtnStyle::Primary:
            fill = st.hover ? kAccentHov : kAccent;
            if (st.pressed) fill = RGB(34, 130, 70);
            border = fill;
            text = RGB(255, 255, 255);
            break;
        case BtnStyle::Danger:
            fill = st.hover ? kDangerHov : kDanger;
            if (st.pressed) fill = RGB(180, 60, 60);
            border = fill;
            text = RGB(255, 255, 255);
            break;
        case BtnStyle::Default:
            fill = st.hover ? kCardAlt : kCard;
            if (st.pressed) fill = RGB(26, 34, 28);
            border = st.hover ? kAccent : kBorder;
            text = kText;
            break;
        }

        if (!enabled) {
            fill = kCard;
            border = kBorder;
            text = kTextDim;
        }

        // Paint backdrop with window bg so corners look rounded
        FillRect(hdc, rc, kBg);

        RECT rr = rc;
        InflateRect(&rr, -1, -1);
        DrawRoundedRect(hdc, rr, 8, fill, border);

        wchar_t txt[256]{};
        GetWindowTextW(hwnd, txt, 255);
        DrawTextCentered(hdc, txt, rc, text, g_state->fontButton);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY: {
        WNDPROC op = st.oldProc;
        g_state->buttons.erase(it);
        return CallWindowProcW(op, hwnd, msg, wp, lp);
    }
    }
    return CallWindowProcW(st.oldProc, hwnd, msg, wp, lp);
}

// ---------- helpers ----------

void SetStatus(const std::string& text, COLORREF dotColor = kIdle) {
    if (g_state && g_state->lblStatus) {
        SetDlgTextUtf8(g_state->lblStatus, text);
    }
    if (g_state) {
        g_state->statusColor = dotColor;
        if (g_state->lblStatusDot) {
            InvalidateRect(g_state->lblStatusDot, nullptr, TRUE);
        }
    }
}

void PostStatus(const std::string& text, COLORREF dotColor = kIdle) {
    if (!g_state || !g_state->hwnd) {
        return;
    }
    char* heap = new char[text.size() + 1];
    memcpy(heap, text.c_str(), text.size() + 1);
    PostMessageA(g_state->hwnd, WM_APP_STATUS, (WPARAM)dotColor, reinterpret_cast<LPARAM>(heap));
}

ChannelSource GetSource() {
    if (IsDlgButtonChecked(g_state->hwnd, IDC_SRC_DM) == BST_CHECKED) return ChannelSource::Dm;
    return ChannelSource::Guild;
}

void EnableSpamControls(bool enable) {
    EnableWindow(g_state->editToken, enable);
    EnableWindow(g_state->editMessage, enable);
    EnableWindow(g_state->editInterval, enable);
    EnableWindow(g_state->editMaxMsg, enable);
    EnableWindow(g_state->chkTurbo, enable);
    EnableWindow(g_state->comboServer, enable);
    EnableWindow(g_state->listChannels, enable);
    EnableWindow(g_state->btnServers, enable);
    EnableWindow(g_state->btnChannels, enable);
    EnableWindow(g_state->btnValidate, enable);
    EnableWindow(g_state->radioGuild, enable);
    EnableWindow(g_state->radioDm, enable);
    EnableWindow(g_state->radioKindAuto, enable);
    EnableWindow(g_state->radioKindBot, enable);
    EnableWindow(g_state->radioKindUser, enable);
    EnableWindow(g_state->btnStart, enable);
    EnableWindow(g_state->btnStop, !enable);
    for (auto& kv : g_state->buttons) {
        InvalidateRect(kv.first, nullptr, TRUE);
    }
}

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id = -1) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
}

HWND CreateEdit(HWND parent, int id, int x, int y, int w, int h, bool password = false, bool multiline = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if (multiline) style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
    if (password) style |= ES_PASSWORD;
    return CreateWindowW(L"EDIT", L"", style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

HWND CreateBtn(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, BtnStyle style = BtnStyle::Default) {
    HWND btn = CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    RegisterButton(btn, style);
    return btn;
}

void ApplyDarkTitle(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

void FillServers() {
    SendMessage(g_state->comboServer, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_state->guilds.size(); ++i) {
        const std::wstring line = Utf8ToWide(g_state->guilds[i].name);
        SendMessage(g_state->comboServer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (!g_state->guilds.empty()) {
        SendMessage(g_state->comboServer, CB_SETCURSEL, 0, 0);
    }
}

void FillChannels() {
    SendMessage(g_state->listChannels, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_state->channels.size(); ++i) {
        const std::wstring line = Utf8ToWide("#  " + g_state->channels[i].label + "   [" + g_state->channels[i].id + "]");
        const int idx = static_cast<int>(SendMessage(g_state->listChannels, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str())));
        SendMessage(g_state->listChannels, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
    }
    if (!g_state->channels.empty()) {
        SendMessage(g_state->listChannels, LB_SETCURSEL, 0, 0);
    }
}

std::string GetSelectedChannelId() {
    const int sel = static_cast<int>(SendMessage(g_state->listChannels, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR || sel < 0) return {};
    const LPARAM data = SendMessage(g_state->listChannels, LB_GETITEMDATA, sel, 0);
    const size_t idx = static_cast<size_t>(data);
    if (idx < g_state->channels.size()) return g_state->channels[idx].id;
    return {};
}

// ---------- actions ----------

void OnValidateToken() {
    const std::string token = GetDlgTextUtf8(g_state->editToken);
    if (token.empty()) { SetStatus("Enter a token.", kDanger); return; }

    TokenKind kind = TokenKind::Auto;
    if (IsDlgButtonChecked(g_state->hwnd, IDC_KIND_BOT) == BST_CHECKED) kind = TokenKind::Bot;
    else if (IsDlgButtonChecked(g_state->hwnd, IDC_KIND_USER) == BST_CHECKED) kind = TokenKind::User;

    EnableWindow(g_state->btnValidate, FALSE);
    InvalidateRect(g_state->btnValidate, nullptr, TRUE);
    SetStatus("Validating token...", kAccent);

    std::thread([token, kind]() {
        std::string err;
        auto client = std::make_unique<DiscordClient>(token, kind);
        const bool ok = client->ValidateToken(err);
        if (ok) {
            const char* kindStr =
                kind == TokenKind::Bot ? "Bot" :
                kind == TokenKind::User ? "User" : "Auto";
            g_state->client = std::move(client);
            PostStatus(std::string("Token accepted [") + kindStr + "]. Load servers or channels.", kSuccess);
        } else {
            g_state->client.reset();
            PostStatus("Error: " + err, kDanger);
        }
        EnableWindow(g_state->btnValidate, TRUE);
        InvalidateRect(g_state->btnValidate, nullptr, TRUE);
    }).detach();
}

void OnLoadServers() {
    if (!g_state->client) { SetStatus("Validate the token first.", kDanger); return; }
    SetStatus("Loading servers...", kAccent);
    std::thread([]() {
        g_state->guilds = g_state->client->FetchGuilds();
        PostMessage(g_state->hwnd, WM_APP_STATUS, 1, 0);
    }).detach();
}

void OnLoadChannels() {
    if (!g_state->client) { SetStatus("Validate the token first.", kDanger); return; }

    const auto src = GetSource();
    SetStatus("Loading channels...", kAccent);

    std::thread([src]() {
        if (src == ChannelSource::Dm) {
            g_state->channels = g_state->client->FetchDmChannels();
        } else if (src == ChannelSource::Guild) {
            const int sel = static_cast<int>(SendMessage(g_state->comboServer, CB_GETCURSEL, 0, 0));
            if (sel < 0 || static_cast<size_t>(sel) >= g_state->guilds.size()) {
                PostStatus("Select a server.", kDanger);
                return;
            }
            g_state->channels = g_state->client->FetchGuildTextChannels(g_state->guilds[static_cast<size_t>(sel)].id);
        }
        PostMessage(g_state->hwnd, WM_APP_STATUS, 2, 0);
    }).detach();
}

void OnSpamFinished() {
    g_state->spamRunning.store(false);
    EnableSpamControls(true);
    SetStatus("Spam stopped.", kIdle);
}

void OnStartSpam() {
    if (g_state->spamRunning.load()) return;
    if (!g_state->client) { SetStatus("Validate the token first.", kDanger); return; }

    const std::string channelId = GetSelectedChannelId();
    if (channelId.empty()) { SetStatus("Select a channel.", kDanger); return; }

    const std::string message = GetDlgTextUtf8(g_state->editMessage);
    if (message.empty()) { SetStatus("Enter message text.", kDanger); return; }

    double interval = 2.0;
    try { interval = std::stod(GetDlgTextUtf8(g_state->editInterval)); if (interval < 0.05) interval = 0.05; }
    catch (...) { interval = 2.0; }

    int maxMsg = 0;
    try { maxMsg = std::stoi(GetDlgTextUtf8(g_state->editMaxMsg)); } catch (...) { maxMsg = 0; }

    const bool turbo = SendMessage(g_state->chkTurbo, BM_GETCHECK, 0, 0) == BST_CHECKED;

    g_state->stopSpam.store(false);
    g_state->spamRunning.store(true);
    EnableSpamControls(false);
    SetStatus("Spam started...", kAccent);

    std::lock_guard<std::mutex> lock(g_state->workerMutex);
    if (g_state->worker.joinable()) g_state->worker.join();

    g_state->worker = std::thread([channelId, message, interval, maxMsg, turbo]() {
        const auto result = g_state->client->RunSpamLoop(channelId, message, interval, maxMsg, turbo, g_state->stopSpam);
        PostStatus("Done. Sent: " + std::to_string(result.sent) + ", failed: " + std::to_string(result.failed),
            result.failed == 0 ? kSuccess : kDanger);
        PostMessage(g_state->hwnd, WM_APP_SPAM_DONE, 0, 0);
    });
}

void OnStopSpam() {
    g_state->stopSpam.store(true);
    SetStatus("Stopping...", kAccent);
}

// ---------- layout ----------

constexpr int IDC_SECTION_BASE = 2000;
int g_sectionId = IDC_SECTION_BASE;

HWND CreateSection(HWND parent, const wchar_t* title, int x, int y, int w) {
    return CreateWindowW(L"STATIC", title, WS_CHILD | WS_VISIBLE,
        x, y, w, 22, parent, (HMENU)(INT_PTR)(g_sectionId++), nullptr, nullptr);
}

void CreateUi(HWND hwnd) {
    g_state->hwnd = hwnd;
    const int pad = 18;
    const int w = 560;
    int y = pad;

    // ===== Header =====
    CreateWindowW(L"STATIC", L"Discord Spammer",
        WS_CHILD | WS_VISIBLE,
        pad, y, w, 30, hwnd, (HMENU)(INT_PTR)1900, nullptr, nullptr);
    y += 30;
    CreateWindowW(L"STATIC", L"Bulk message sender control panel",
        WS_CHILD | WS_VISIBLE,
        pad, y, w, 18, hwnd, (HMENU)(INT_PTR)1901, nullptr, nullptr);
    y += 26;

    // ===== Section: Token =====
    CreateSection(hwnd, L"AUTHORIZATION", pad, y, w); y += 26;

    CreateLabel(hwnd, L"Discord token", pad, y, w, 18);
    y += 20;
    g_state->editToken = CreateEdit(hwnd, IDC_TOKEN, pad, y, w - 130, 28, true);
    g_state->btnValidate = CreateBtn(hwnd, IDC_BTN_VALIDATE, L"Validate", pad + w - 120, y, 120, 28, BtnStyle::Primary);
    y += 36;

    CreateLabel(hwnd, L"Token type:", pad, y, 120, 20);
    g_state->radioKindAuto = CreateWindowW(L"BUTTON", L"Auto",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        pad + 110, y, 70, 22, hwnd, (HMENU)(INT_PTR)IDC_KIND_AUTO, nullptr, nullptr);
    g_state->radioKindBot = CreateWindowW(L"BUTTON", L"Bot",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        pad + 185, y, 60, 22, hwnd, (HMENU)(INT_PTR)IDC_KIND_BOT, nullptr, nullptr);
    g_state->radioKindUser = CreateWindowW(L"BUTTON", L"User",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        pad + 250, y, 70, 22, hwnd, (HMENU)(INT_PTR)IDC_KIND_USER, nullptr, nullptr);
    CheckDlgButton(hwnd, IDC_KIND_AUTO, BST_CHECKED);
    y += 30;

    // ===== Section: Message =====
    CreateSection(hwnd, L"MESSAGE", pad, y, w); y += 26;

    CreateLabel(hwnd, L"Message text", pad, y, w, 18);
    y += 20;
    g_state->editMessage = CreateEdit(hwnd, IDC_MESSAGE, pad, y, w, 80, false, true);
    y += 90;

    CreateLabel(hwnd, L"Interval, sec", pad, y, 110, 18);
    g_state->editInterval = CreateEdit(hwnd, IDC_INTERVAL, pad + 115, y - 3, 70, 26);
    SetDlgTextUtf8(g_state->editInterval, "2");

    CreateLabel(hwnd, L"Limit (0=inf)", pad + 205, y, 100, 18);
    g_state->editMaxMsg = CreateEdit(hwnd, IDC_MAXMSG, pad + 305, y - 3, 60, 26);
    SetDlgTextUtf8(g_state->editMaxMsg, "0");

    g_state->chkTurbo = CreateWindowW(L"BUTTON", L"Turbo",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        pad + 390, y - 3, 110, 26, hwnd, (HMENU)(INT_PTR)IDC_TURBO, nullptr, nullptr);
    y += 36;

    // ===== Section: Destination =====
    CreateSection(hwnd, L"DESTINATION", pad, y, w); y += 26;

    g_state->radioGuild = CreateWindowW(L"BUTTON", L"Server",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        pad, y, 100, 22, hwnd, (HMENU)(INT_PTR)IDC_SRC_GUILD, nullptr, nullptr);
    g_state->radioDm = CreateWindowW(L"BUTTON", L"DM",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        pad + 110, y, 110, 22, hwnd, (HMENU)(INT_PTR)IDC_SRC_DM, nullptr, nullptr);
    CheckDlgButton(hwnd, IDC_SRC_GUILD, BST_CHECKED);
    y += 30;

    CreateLabel(hwnd, L"Server", pad, y, 80, 18);
    g_state->comboServer = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        pad + 80, y - 4, w - 220, 260, hwnd, (HMENU)(INT_PTR)IDC_SERVER, nullptr, nullptr);
    g_state->btnServers = CreateBtn(hwnd, IDC_BTN_SERVERS, L"Refresh", pad + w - 130, y - 4, 130, 28);
    y += 36;

    CreateLabel(hwnd, L"Channels / DMs", pad, y, w, 18);
    y += 20;
    g_state->listChannels = CreateWindowW(L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        pad, y, w, 120, hwnd, (HMENU)(INT_PTR)IDC_CHANNELS, nullptr, nullptr);
    y += 110;
    g_state->btnChannels = CreateBtn(hwnd, IDC_BTN_CHANNELS, L"Load", pad, y, 150, 28);
    y += 34;

    // ===== Status and buttons =====
    CreateSection(hwnd, L"STATUS", pad, y, w); y += 26;

    g_state->lblStatusDot = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        pad, y + 4, 14, 14, hwnd, (HMENU)(INT_PTR)IDC_STATUS_DOT, nullptr, nullptr);
    g_state->lblStatus = CreateLabel(hwnd, L"Ready.", pad + 22, y, w - 22, 22);
    y += 32;

    g_state->btnStart = CreateBtn(hwnd, IDC_BTN_START, L"Start", pad, y, 180, 40, BtnStyle::Primary);
    g_state->btnStop  = CreateBtn(hwnd, IDC_BTN_STOP,  L"Stop",  pad + 190, y, 180, 40, BtnStyle::Danger);
    EnableWindow(g_state->btnStop, FALSE);

    // Fonts
    auto setFont = [](HWND h, HFONT f) { if (h) SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE); };
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        setFont(child, g_state->fontUi);
        child = GetWindow(child, GW_HWNDNEXT);
    }
    setFont(GetDlgItem(hwnd, 1900), g_state->fontTitle);
    setFont(GetDlgItem(hwnd, 1901), g_state->fontSmall);
    for (int id = IDC_SECTION_BASE; id < g_sectionId; ++id) {
        setFont(GetDlgItem(hwnd, id), g_state->fontHeader);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_APP_STATUS: {
        if (lParam) {
            std::unique_ptr<char[]> text(reinterpret_cast<char*>(lParam));
            COLORREF c = (COLORREF)wParam;
            SetStatus(text.get(), c == 0 ? kIdle : c);
        } else if (wParam == 1) {
            FillServers();
            SetStatus("Servers loaded: " + std::to_string(g_state->guilds.size()), kSuccess);
        } else if (wParam == 2) {
            FillChannels();
            SetStatus("Channels loaded: " + std::to_string(g_state->channels.size()), kSuccess);
        }
        return 0;
    }

    case WM_APP_SPAM_DONE:
        OnSpamFinished();
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlID == IDC_STATUS_DOT) {
            RECT rc = dis->rcItem;
            FillRect(dis->hDC, rc, kBg);
            HBRUSH br = CreateSolidBrush(g_state->statusColor);
            HPEN pen = CreatePen(PS_SOLID, 1, g_state->statusColor);
            HGDIOBJ ob = SelectObject(dis->hDC, br);
            HGDIOBJ op = SelectObject(dis->hDC, pen);
            Ellipse(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(dis->hDC, ob);
            SelectObject(dis->hDC, op);
            DeleteObject(br);
            DeleteObject(pen);
            return TRUE;
        }
        return FALSE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(ctl);
        SetBkColor(hdc, kBg);
        if (id == 1900) {
            SetTextColor(hdc, RGB(255, 255, 255));
        } else if (id == 1901) {
            SetTextColor(hdc, kTextDim);
        } else if (id >= IDC_SECTION_BASE && id < IDC_SECTION_BASE + 50) {
            SetTextColor(hdc, kAccent);
        } else {
            SetTextColor(hdc, kText);
        }
        return reinterpret_cast<LRESULT>(g_state->brushBg);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkColor(hdc, kCard);
        return reinterpret_cast<LRESULT>(g_state->brushCard);
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kText);
        SetBkColor(hdc, kBg);
        return reinterpret_cast<LRESULT>(g_state->brushBg);
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, rc, kBg);
        return 1;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);

        if (id == IDC_BTN_VALIDATE) OnValidateToken();
        else if (id == IDC_BTN_SERVERS) OnLoadServers();
        else if (id == IDC_BTN_CHANNELS) OnLoadChannels();
        else if (id == IDC_BTN_START) OnStartSpam();
        else if (id == IDC_BTN_STOP) OnStopSpam();
        return 0;
    }

    case WM_DESTROY:
        g_state->stopSpam.store(true);
        {
            std::lock_guard<std::mutex> lock(g_state->workerMutex);
            if (g_state->worker.joinable()) g_state->worker.join();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunGuiApplication() {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    static GuiState state;
    g_state = &state;
    state.brushBg = CreateSolidBrush(kBg);
    state.brushCard = CreateSolidBrush(kCard);

    auto makeFont = [](int height, int weight) {
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    state.fontUi     = makeFont(-14, FW_NORMAL);
    state.fontSmall  = makeFont(-12, FW_NORMAL);
    state.fontHeader = makeFont(-12, FW_BOLD);
    state.fontTitle  = makeFont(-22, FW_SEMIBOLD);
    state.fontButton = makeFont(-14, FW_SEMIBOLD);

    const wchar_t* cls = L"DiscordSpammerGui";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = state.brushBg;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, cls, L"Discord Spammer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 820,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) return 1;

    ApplyDarkTitle(hwnd);
    CreateUi(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(state.brushBg);
    DeleteObject(state.brushCard);
    DeleteObject(state.fontUi);
    DeleteObject(state.fontSmall);
    DeleteObject(state.fontHeader);
    DeleteObject(state.fontTitle);
    DeleteObject(state.fontButton);
    g_state = nullptr;
    return static_cast<int>(msg.wParam);
}
