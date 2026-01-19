/* gui.c - Win32 GUI with Server/Client Tab interface */
#include "gui.h"
#include "network.h"
#include "protocol.h"
#include "resource_ids.h"
#include <shellapi.h>
#include <stdio.h>
#include <uxtheme.h>

#ifndef WM_TRAYICON
#define WM_TRAYICON (WM_USER + 1)
#endif

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

#define WND_CLASS L"SharedVoiceClass"

// 颜色定义
#define COLOR_HEADER_BG     RGB(45, 55, 72)
#define COLOR_ACCENT        RGB(66, 153, 225)
#define COLOR_SUCCESS       RGB(72, 187, 120)
#define COLOR_WARNING       RGB(237, 137, 54)
#define COLOR_DANGER        RGB(245, 101, 101)

static struct {
    HINSTANCE hInstance;
    HWND hMain, hTab;
    // 服务器页面控件 - 直接在主窗口创建
    HWND hSrvName, hSrvPort, hSrvUdpPort, hSrvDiscPort;
    HWND hSrvStart, hSrvStop, hSrvClients, hSrvStatus;
    HWND hSrvGroupConfig, hSrvGroupUsers;
    HWND hSrvLblName, hSrvLblTcpPort, hSrvLblUdpPort, hSrvLblDiscPort;
    // 客户端页面控件
    HWND hCliServers, hCliRefresh, hCliDisconnect, hCliPeers, hCliStatus;
    HWND hCliGroupServers, hCliGroupUsers;
    // 客户端连接配置控件
    HWND hCliGroupManual;
    HWND hCliManualIp, hCliManualTcpPort, hCliManualUdpPort, hCliManualDiscPort;
    HWND hCliManualConnect;
    HWND hCliLblIp, hCliLblTcpPort, hCliLblUdpPort, hCliLblDiscPort;
    HWND hCliUsername, hCliLblUsername;
    // 公共控件
    HWND hMuteBtn, hInputSlider, hOutputSlider, hInputLevel, hOutputLevel, hStatus;
    HWND hHeaderLabel, hAudioGroup;
    HWND hLblInput, hLblOutput, hLblInLevel, hLblOutLevel;
    NOTIFYICONDATAW nid;
    int currentTab;
    BOOL serverRunning, clientConnected;
    GuiCallbacks callbacks;
    HFONT hTitleFont, hNormalFont, hBoldFont, hMonoFont;
    HBRUSH hHeaderBrush, hAccentBrush;
} g = {0};

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void CreateServerControls(void);
static void CreateClientControls(void);
static void CreateCommonControls(void);
static void SwitchTab(int tab);
static void UpdateUI(void);
static void CreateFonts(void);

bool Gui_Init(HINSTANCE hInstance, const GuiCallbacks* cb) {
    g.hInstance = hInstance;
    if (cb) g.callbacks = *cb;

    WNDCLASSEXW wc = {sizeof(wc), CS_HREDRAW|CS_VREDRAW, WndProc, 0, 0, hInstance,
        LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP)), LoadCursor(NULL,IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW+1), NULL, WND_CLASS, LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP))};
    if (!RegisterClassExW(&wc)) return false;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    // 创建字体和画刷
    CreateFonts();
    g.hHeaderBrush = CreateSolidBrush(COLOR_HEADER_BG);
    g.hAccentBrush = CreateSolidBrush(COLOR_ACCENT);

    g.hMain = CreateWindowExW(0, WND_CLASS, L"SharedVoice - 局域网语音通话 v1.0", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 590, 580, NULL, NULL, hInstance, NULL);
    if (!g.hMain) return false;

    RECT rc; GetClientRect(g.hMain, &rc);

    // 标题头部区域
    g.hHeaderLabel = CreateWindowExW(0, L"STATIC", L"  SharedVoice - 局域网语音通话平台", 
        WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,
        0, 0, rc.right, 40, g.hMain, NULL, hInstance, NULL);
    SendMessage(g.hHeaderLabel, WM_SETFONT, (WPARAM)g.hTitleFont, TRUE);

    // Tab控件
    g.hTab = CreateWindowExW(0, WC_TABCONTROLW, NULL, WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|TCS_HOTTRACK,
        10, 48, rc.right-20, 280, g.hMain, (HMENU)IDC_TAB, hInstance, NULL);
    SendMessage(g.hTab, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    TCITEMW ti = {TCIF_TEXT, 0, 0, L"服务器模式", 0, 0, 0};
    TabCtrl_InsertItem(g.hTab, 0, &ti);
    ti.pszText = L"客户端模式";
    TabCtrl_InsertItem(g.hTab, 1, &ti);

    CreateServerControls();
    CreateClientControls();
    CreateCommonControls();
    TabCtrl_SetCurSel(g.hTab, 1); // 默认选中客户端模式页签
    SwitchTab(1); // 默认进入客户端模式

    // 状态栏 - 分区显示
    int parts[] = {220, 380, -1};
    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD|WS_VISIBLE,
        0, 0, 0, 0, g.hMain, (HMENU)IDC_STATUS, hInstance, NULL);
    SendMessage(g.hStatus, SB_SETPARTS, 3, (LPARAM)parts);
    SendMessage(g.hStatus, SB_SETTEXTW, 0, (LPARAM)L"就绪");
    SendMessage(g.hStatus, SB_SETTEXTW, 1, (LPARAM)L"发现端口: 37020");
    SendMessage(g.hStatus, SB_SETTEXTW, 2, (LPARAM)L"v1.0");

    SetTimer(g.hMain, IDT_UPDATE, 100, NULL);
    
    // 创建托盘图标
    ZeroMemory(&g.nid, sizeof(g.nid));
    g.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g.nid.hWnd = g.hMain;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAYICON;
    // load the embedded application icon (fallback to system icon if resource missing)
    HICON hAppIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                      0, 0, LR_DEFAULTSIZE);
    if (!hAppIcon) hAppIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
    g.nid.hIcon = hAppIcon;
    wcscpy(g.nid.szTip, L"SharedVoice - 局域网语音通话");
    Shell_NotifyIconW(NIM_ADD, &g.nid);
    
    ShowWindow(g.hMain, SW_SHOW);
    UpdateWindow(g.hMain);
    return true;
}

static void CreateFonts(void) {
    // 标题字体
    LOGFONTW lf = {0};
    lf.lfHeight = -18;
    lf.lfWeight = FW_BOLD;
    wcscpy(lf.lfFaceName, L"Microsoft YaHei UI");
    g.hTitleFont = CreateFontIndirectW(&lf);

    // 普通字体
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    g.hNormalFont = CreateFontIndirectW(&lf);

    // 粗体字体
    lf.lfWeight = FW_SEMIBOLD;
    g.hBoldFont = CreateFontIndirectW(&lf);
    
    // 等宽字体 (用于日志)
    lf.lfHeight = -12;
    lf.lfWeight = FW_NORMAL;
    wcscpy(lf.lfFaceName, L"Consolas");
    g.hMonoFont = CreateFontIndirectW(&lf);
}

static void CreateServerControls(void) {
    int baseY = 75;
    int y = baseY;

    // 服务器配置区域
    g.hSrvGroupConfig = CreateWindowExW(0, L"BUTTON", L" 服务器配置 ", WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
        20, y, 535, 95, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvGroupConfig, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 20;

    // 第一行: 服务器名称
    g.hSrvLblName = CreateWindowExW(0, L"STATIC", L"服务器名称:", WS_CHILD|WS_VISIBLE,
        30, y+3, 80, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvLblName, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"语音服务器",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 115, y, 180, 24,
        g.hMain, (HMENU)IDC_SERVER_NAME, g.hInstance, NULL);
    SendMessage(g.hSrvName, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvLblDiscPort = CreateWindowExW(0, L"STATIC", L"发现端口:", WS_CHILD|WS_VISIBLE,
        310, y+3, 65, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvLblDiscPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvDiscPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"37020",
        WS_CHILD|WS_VISIBLE|ES_NUMBER, 380, y, 60, 24,
        g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvDiscPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    y += 30;

    // 第二行: TCP端口 和 UDP端口
    g.hSrvLblTcpPort = CreateWindowExW(0, L"STATIC", L"TCP端口:", WS_CHILD|WS_VISIBLE,
        30, y+3, 60, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvLblTcpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5000",
        WS_CHILD|WS_VISIBLE|ES_NUMBER, 95, y, 60, 24,
        g.hMain, (HMENU)IDC_SERVER_PORT, g.hInstance, NULL);
    SendMessage(g.hSrvPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvLblUdpPort = CreateWindowExW(0, L"STATIC", L"UDP端口:", WS_CHILD|WS_VISIBLE,
        170, y+3, 60, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvLblUdpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hSrvUdpPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"6000",
        WS_CHILD|WS_VISIBLE|ES_NUMBER, 235, y, 60, 24,
        g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvUdpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    // 按钮
    g.hSrvStart = CreateWindowExW(0, L"BUTTON", L"启动服务器", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        310, y, 100, 26, g.hMain, (HMENU)IDC_BTN_START, g.hInstance, NULL);
    SendMessage(g.hSrvStart, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    g.hSrvStop = CreateWindowExW(0, L"BUTTON", L"停止服务器", WS_CHILD|WS_VISIBLE|WS_DISABLED|BS_PUSHBUTTON,
        420, y, 100, 26, g.hMain, (HMENU)IDC_BTN_STOP, g.hInstance, NULL);
    SendMessage(g.hSrvStop, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 30;

    // 状态
    g.hSrvStatus = CreateWindowExW(0, L"STATIC", L"状态: 已停止", WS_CHILD|WS_VISIBLE|SS_LEFT,
        30, y+3, 300, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvStatus, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 28;

    // 连接用户区域
    g.hSrvGroupUsers = CreateWindowExW(0, L"BUTTON", L" 已连接用户 ", WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
        20, y, 535, 130, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hSrvGroupUsers, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 20;

    g.hSrvClients = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 30, y, 515, 100,
        g.hMain, (HMENU)IDC_PEER_LIST, g.hInstance, NULL);
    SendMessage(g.hSrvClients, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    LVCOLUMNW col = {LVCF_TEXT|LVCF_WIDTH, 0, 120, L"用户名", 0, 0, 0, 0};
    ListView_InsertColumn(g.hSrvClients, 0, &col);
    col.cx = 60; col.pszText = L"类型";
    ListView_InsertColumn(g.hSrvClients, 1, &col);
    col.cx = 110; col.pszText = L"IP地址";
    ListView_InsertColumn(g.hSrvClients, 2, &col);
    col.cx = 70; col.pszText = L"UDP端口";
    ListView_InsertColumn(g.hSrvClients, 3, &col);
    col.cx = 70; col.pszText = L"状态";
    ListView_InsertColumn(g.hSrvClients, 4, &col);
    ListView_SetExtendedListViewStyle(g.hSrvClients, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
}

static void CreateClientControls(void) {
    int baseY = 75;
    int y = baseY;

    // 连接配置区域（合并我的信息和手动连接）
    g.hCliGroupManual = CreateWindowExW(0, L"BUTTON", L" 连接配置 ", WS_CHILD|BS_GROUPBOX,
        20, y, 535, 75, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliGroupManual, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 20;

    // 第一行：昵称
    g.hCliLblUsername = CreateWindowExW(0, L"STATIC", L"昵称:", WS_CHILD,
        30, y+3, 35, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliLblUsername, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    
    g.hCliUsername = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"User",
        WS_CHILD|ES_AUTOHSCROLL, 70, y, 120, 22,
        g.hMain, (HMENU)IDC_CLIENT_USERNAME, g.hInstance, NULL);
    SendMessage(g.hCliUsername, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    SendMessage(g.hCliUsername, EM_SETLIMITTEXT, 31, 0);

    // 发现端口（放在昵称同一行）
    g.hCliLblDiscPort = CreateWindowExW(0, L"STATIC", L"发现端口:", WS_CHILD,
        200, y+3, 60, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliLblDiscPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    
    g.hCliManualDiscPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"37020",
        WS_CHILD|ES_NUMBER, 265, y, 55, 22,
        g.hMain, (HMENU)IDC_MANUAL_DISC, g.hInstance, NULL);
    SendMessage(g.hCliManualDiscPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    // 刷新按钮
    g.hCliRefresh = CreateWindowExW(0, L"BUTTON", L"扫描服务器", WS_CHILD|BS_PUSHBUTTON,
        330, y, 90, 24, g.hMain, (HMENU)IDC_BTN_REFRESH, g.hInstance, NULL);
    SendMessage(g.hCliRefresh, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    // 状态显示
    g.hCliStatus = CreateWindowExW(0, L"STATIC", L"未连接", WS_CHILD|SS_LEFT,
        430, y+3, 115, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliStatus, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 26;

    // 第二行：IP、TCP、UDP、连接、断开
    g.hCliLblIp = CreateWindowExW(0, L"STATIC", L"IP:", WS_CHILD,
        30, y+3, 18, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliLblIp, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    
    g.hCliManualIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|ES_AUTOHSCROLL, 50, y, 110, 22,
        g.hMain, (HMENU)IDC_MANUAL_IP, g.hInstance, NULL);
    SendMessage(g.hCliManualIp, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hCliLblTcpPort = CreateWindowExW(0, L"STATIC", L"TCP:", WS_CHILD,
        168, y+3, 28, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliLblTcpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    
    g.hCliManualTcpPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5000",
        WS_CHILD|ES_NUMBER, 198, y, 50, 22,
        g.hMain, (HMENU)IDC_MANUAL_TCP, g.hInstance, NULL);
    SendMessage(g.hCliManualTcpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hCliLblUdpPort = CreateWindowExW(0, L"STATIC", L"UDP:", WS_CHILD,
        256, y+3, 30, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliLblUdpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    
    g.hCliManualUdpPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"6000",
        WS_CHILD|ES_NUMBER, 288, y, 50, 22,
        g.hMain, (HMENU)IDC_MANUAL_UDP, g.hInstance, NULL);
    SendMessage(g.hCliManualUdpPort, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    // 连接按钮
    g.hCliManualConnect = CreateWindowExW(0, L"BUTTON", L"连接", WS_CHILD|BS_PUSHBUTTON,
        350, y, 70, 24, g.hMain, (HMENU)IDC_BTN_MANUAL_CONN, g.hInstance, NULL);
    SendMessage(g.hCliManualConnect, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    // 断开按钮（紧挨着连接按钮）
    g.hCliDisconnect = CreateWindowExW(0, L"BUTTON", L"断开", WS_CHILD|WS_DISABLED|BS_PUSHBUTTON,
        425, y, 70, 24, g.hMain, (HMENU)IDC_BTN_DISCONNECT, g.hInstance, NULL);
    SendMessage(g.hCliDisconnect, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 30;

    // 服务器列表区域（自动发现）
    g.hCliGroupServers = CreateWindowExW(0, L"BUTTON", L" 自动发现的服务器 ", WS_CHILD|BS_GROUPBOX,
        20, y, 535, 120, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliGroupServers, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 18;

    g.hCliServers = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 30, y, 515, 90,
        g.hMain, (HMENU)IDC_SERVER_LIST, g.hInstance, NULL);
    SendMessage(g.hCliServers, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    LVCOLUMNW col = {LVCF_TEXT|LVCF_WIDTH, 0, 150, L"服务器名称", 0, 0, 0, 0};
    ListView_InsertColumn(g.hCliServers, 0, &col);
    col.cx = 110; col.pszText = L"IP地址";
    ListView_InsertColumn(g.hCliServers, 1, &col);
    col.cx = 70; col.pszText = L"TCP";
    ListView_InsertColumn(g.hCliServers, 2, &col);
    col.cx = 70; col.pszText = L"UDP";
    ListView_InsertColumn(g.hCliServers, 3, &col);
    col.cx = 60; col.pszText = L"在线";
    ListView_InsertColumn(g.hCliServers, 4, &col);
    ListView_SetExtendedListViewStyle(g.hCliServers, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    y += 99;

    // 在线用户区域
    g.hCliGroupUsers = CreateWindowExW(0, L"BUTTON", L" 在线用户 ", WS_CHILD|BS_GROUPBOX,
        20, y, 535, 110, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliGroupUsers, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 18;

    g.hCliPeers = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 30, y, 515, 80,
        g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliPeers, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    col.cx = 150; col.pszText = L"用户名";
    ListView_InsertColumn(g.hCliPeers, 0, &col);
    col.cx = 60; col.pszText = L"类型";
    ListView_InsertColumn(g.hCliPeers, 1, &col);
    col.cx = 80; col.pszText = L"状态";
    ListView_InsertColumn(g.hCliPeers, 2, &col);
    col.cx = 80; col.pszText = L"SSRC";
    ListView_InsertColumn(g.hCliPeers, 3, &col);
    ListView_SetExtendedListViewStyle(g.hCliPeers, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
}

static void CreateCommonControls(void) {
    int y = 400;

    // 分隔线
    CreateWindowExW(0, L"STATIC", NULL, WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
        15, y, 545, 2, g.hMain, NULL, g.hInstance, NULL);
    y += 8;

    // 音频控制区域
    g.hAudioGroup = CreateWindowExW(0, L"BUTTON", L" 音频控制 ", WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
        15, y, 545, 100, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hAudioGroup, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 22;

    // 静音按钮
    g.hMuteBtn = CreateWindowExW(0, L"BUTTON", L"麦克风静音", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        30, y, 120, 22, g.hMain, (HMENU)IDC_BTN_MUTE, g.hInstance, NULL);
    SendMessage(g.hMuteBtn, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);
    y += 26;

    // 输入音量
    g.hLblInput = CreateWindowExW(0, L"STATIC", L"输入:", WS_CHILD|WS_VISIBLE,
        30, y+3, 40, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hLblInput, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hInputSlider = CreateWindowExW(0, TRACKBAR_CLASSW, NULL, WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
        75, y, 180, 25, g.hMain, (HMENU)IDC_SLIDER_INPUT, g.hInstance, NULL);
    SendMessage(g.hInputSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(g.hInputSlider, TBM_SETPOS, TRUE, 80);

    g.hLblInLevel = CreateWindowExW(0, L"STATIC", L"信号:", WS_CHILD|WS_VISIBLE,
        265, y+3, 40, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hLblInLevel, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hInputLevel = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
        310, y+3, 235, 18, g.hMain, (HMENU)IDC_LEVEL_INPUT, g.hInstance, NULL);
    SendMessage(g.hInputLevel, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(g.hInputLevel, PBM_SETBARCOLOR, 0, COLOR_SUCCESS);
    y += 26;

    // 输出音量
    g.hLblOutput = CreateWindowExW(0, L"STATIC", L"输出:", WS_CHILD|WS_VISIBLE,
        30, y+3, 40, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hLblOutput, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hOutputSlider = CreateWindowExW(0, TRACKBAR_CLASSW, NULL, WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
        75, y, 180, 25, g.hMain, (HMENU)IDC_SLIDER_OUTPUT, g.hInstance, NULL);
    SendMessage(g.hOutputSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(g.hOutputSlider, TBM_SETPOS, TRUE, 80);

    g.hLblOutLevel = CreateWindowExW(0, L"STATIC", L"信号:", WS_CHILD|WS_VISIBLE,
        265, y+3, 40, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hLblOutLevel, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    g.hOutputLevel = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
        310, y+3, 235, 18, g.hMain, (HMENU)IDC_LEVEL_OUTPUT, g.hInstance, NULL);
    SendMessage(g.hOutputLevel, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(g.hOutputLevel, PBM_SETBARCOLOR, 0, COLOR_ACCENT);
}

static void SwitchTab(int tab) {
    g.currentTab = tab;
    
    // 服务器模式控件
    BOOL showServer = (tab == 0);
    ShowWindow(g.hSrvGroupConfig, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvGroupUsers, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvName, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvPort, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvUdpPort, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvDiscPort, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvStart, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvStop, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvClients, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvStatus, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvLblName, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvLblTcpPort, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvLblUdpPort, showServer ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSrvLblDiscPort, showServer ? SW_SHOW : SW_HIDE);
    
    // 客户端模式控件
    BOOL showClient = (tab == 1);
    ShowWindow(g.hCliGroupServers, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliGroupUsers, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliServers, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliRefresh, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliDisconnect, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliPeers, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliStatus, showClient ? SW_SHOW : SW_HIDE);
    // 连接配置控件
    ShowWindow(g.hCliGroupManual, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliManualIp, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliManualTcpPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliManualUdpPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliManualDiscPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliManualConnect, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliLblIp, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliLblTcpPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliLblUdpPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliLblDiscPort, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliUsername, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliLblUsername, showClient ? SW_SHOW : SW_HIDE);
}

static void UpdateUI(void) {
    EnableWindow(g.hSrvName, !g.serverRunning);
    EnableWindow(g.hSrvPort, !g.serverRunning);
    EnableWindow(g.hSrvUdpPort, !g.serverRunning);
    EnableWindow(g.hSrvDiscPort, !g.serverRunning);
    EnableWindow(g.hSrvStart, !g.serverRunning);
    EnableWindow(g.hSrvStop, g.serverRunning);
    
    if (g.serverRunning) {
        SetWindowTextW(g.hSrvStatus, L"状态: 运行中");
    } else {
        SetWindowTextW(g.hSrvStatus, L"状态: 已停止");
    }

    EnableWindow(g.hCliServers, !g.clientConnected);
    EnableWindow(g.hCliRefresh, !g.clientConnected);
    EnableWindow(g.hCliDisconnect, g.clientConnected);
    // 连接配置控件
    EnableWindow(g.hCliManualIp, !g.clientConnected);
    EnableWindow(g.hCliManualTcpPort, !g.clientConnected);
    EnableWindow(g.hCliManualUdpPort, !g.clientConnected);
    EnableWindow(g.hCliManualDiscPort, !g.clientConnected);
    EnableWindow(g.hCliManualConnect, !g.clientConnected);
    // 用户名控件 (连接后禁用)
    EnableWindow(g.hCliUsername, !g.clientConnected);
    
    if (!g.clientConnected) {
        SetWindowTextW(g.hCliStatus, L"未连接");
    }
}

static void OnServerStart(void) {
    wchar_t name[64], port[16], udpPort[16], discPort[16];
    GetWindowTextW(g.hSrvName, name, 64);
    GetWindowTextW(g.hSrvPort, port, 16);
    GetWindowTextW(g.hSrvUdpPort, udpPort, 16);
    GetWindowTextW(g.hSrvDiscPort, discPort, 16);
    
    char nameA[64];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, nameA, 64, NULL, NULL);
    int p = _wtoi(port);
    int udpP = _wtoi(udpPort);
    int discP = _wtoi(discPort);
    if (p <= 0 || p > 65535) p = 5000;
    if (udpP <= 0 || udpP > 65535) udpP = 6000;
    if (discP <= 0 || discP > 65535) discP = 37020;
    
    LOG_INFO("GUI: OnServerStart called - name=%s, tcp_port=%d, udp_port=%d, disc_port=%d", nameA, p, udpP, discP);
    
    if (g.callbacks.onStartServer) {
        g.callbacks.onStartServer(nameA, (uint16_t)p, (uint16_t)udpP, (uint16_t)discP, g.callbacks.userdata);
    } else {
        LOG_ERROR("GUI: onStartServer callback is NULL!");
        MessageBoxW(g.hMain, L"内部错误：回调函数未设置", L"错误", MB_OK|MB_ICONERROR);
    }
}

static void OnServerStop(void) {
    LOG_INFO("GUI: OnServerStop called");
    if (g.callbacks.onStopServer) g.callbacks.onStopServer(g.callbacks.userdata);
}

static void OnClientRefresh(void) {
    wchar_t discPortW[16];
    GetWindowTextW(g.hCliManualDiscPort, discPortW, 16);
    int discP = _wtoi(discPortW);
    if (discP <= 0 || discP > 65535) discP = 37020;
    
    LOG_INFO("GUI: OnClientRefresh called with discovery_port=%d", discP);
    if (g.callbacks.onRefreshServers) g.callbacks.onRefreshServers((uint16_t)discP, g.callbacks.userdata);
}

// 连接服务器（使用输入框中的配置）
static void OnClientManualConnect(void) {
    wchar_t ipW[64], tcpPortW[16], udpPortW[16];
    GetWindowTextW(g.hCliManualIp, ipW, 64);
    GetWindowTextW(g.hCliManualTcpPort, tcpPortW, 16);
    GetWindowTextW(g.hCliManualUdpPort, udpPortW, 16);
    
    char ipA[64];
    WideCharToMultiByte(CP_UTF8, 0, ipW, -1, ipA, 64, NULL, NULL);
    int tcpPort = _wtoi(tcpPortW);
    int udpPort = _wtoi(udpPortW);
    
    if (strlen(ipA) == 0) {
        MessageBoxW(g.hMain, L"请输入服务器IP地址", L"连接", MB_OK|MB_ICONWARNING);
        return;
    }
    if (tcpPort <= 0 || tcpPort > 65535) tcpPort = 5000;
    if (udpPort <= 0 || udpPort > 65535) udpPort = 6000;
    
    LOG_INFO("GUI: OnClientManualConnect called - ip=%s, tcp_port=%d, udp_port=%d", ipA, tcpPort, udpPort);
    if (g.callbacks.onConnect) g.callbacks.onConnect(ipA, (uint16_t)tcpPort, (uint16_t)udpPort, g.callbacks.userdata);
}

static void OnClientDisconnect(void) {
    LOG_INFO("GUI: OnClientDisconnect called");
    if (g.callbacks.onDisconnect) g.callbacks.onDisconnect(g.callbacks.userdata);
}

static void OnMuteChanged(void) {
    BOOL muted = (SendMessage(g.hMuteBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
    LOG_INFO("GUI: OnMuteChanged called - muted=%d", muted);
    if (g.callbacks.onMuteChanged) g.callbacks.onMuteChanged(muted ? true : false, g.callbacks.userdata);
}

static void OnVolumeChanged(void) {
    int in = (int)SendMessage(g.hInputSlider, TBM_GETPOS, 0, 0);
    int out = (int)SendMessage(g.hOutputSlider, TBM_GETPOS, 0, 0);
    if (g.callbacks.onVolumeChanged) g.callbacks.onVolumeChanged(in, out, g.callbacks.userdata);
}

// 当选择服务器列表项时，自动填充到手动连接输入框
static void OnServerListSelectionChanged(void) {
    int sel = ListView_GetNextItem(g.hCliServers, -1, LVNI_SELECTED);
    if (sel < 0) return;
    
    wchar_t ip[64], tcpPort[16], udpPort[16];
    ListView_GetItemText(g.hCliServers, sel, 1, ip, 64);
    ListView_GetItemText(g.hCliServers, sel, 2, tcpPort, 16);
    ListView_GetItemText(g.hCliServers, sel, 3, udpPort, 16);
    
    // 自动填充到手动连接的输入框
    SetWindowTextW(g.hCliManualIp, ip);
    SetWindowTextW(g.hCliManualTcpPort, tcpPort);
    SetWindowTextW(g.hCliManualUdpPort, udpPort);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
        // 为标题设置自定义背景色
        if ((HWND)lParam == g.hHeaderLabel) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, COLOR_HEADER_BG);
            return (LRESULT)g.hHeaderBrush;
        }
        break;
        
    case WM_NOTIFY: {
        NMHDR* p = (NMHDR*)lParam;
        if (p->hwndFrom == g.hTab && p->code == TCN_SELCHANGE)
            SwitchTab(TabCtrl_GetCurSel(g.hTab));
        // 处理服务器列表选择变化
        if (p->hwndFrom == g.hCliServers && (p->code == LVN_ITEMCHANGED || p->code == NM_CLICK)) {
            OnServerListSelectionChanged();
        }
        break;
    }
    case WM_COMMAND:
        // 先处理托盘菜单命令
        if (LOWORD(wParam) == IDM_SHOW) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        } else if (LOWORD(wParam) == IDM_MUTE) {
            BOOL muted = (SendMessage(g.hMuteBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SendMessage(g.hMuteBtn, BM_SETCHECK, muted ? BST_UNCHECKED : BST_CHECKED, 0);
            OnMuteChanged();
            return 0;
        } else if (LOWORD(wParam) == IDM_EXIT) {
            Shell_NotifyIconW(NIM_DELETE, &g.nid);
            DestroyWindow(hwnd);
            return 0;
        }
        // 处理其他控件命令
        switch (LOWORD(wParam)) {
        case IDC_BTN_START: 
            LOG_INFO("WM_COMMAND: IDC_BTN_START received");
            OnServerStart(); 
            break;
        case IDC_BTN_STOP: 
            LOG_INFO("WM_COMMAND: IDC_BTN_STOP received");
            OnServerStop(); 
            break;
        case IDC_BTN_REFRESH: 
            LOG_INFO("WM_COMMAND: IDC_BTN_REFRESH received");
            OnClientRefresh(); 
            break;
        case IDC_BTN_MANUAL_CONN:
            LOG_INFO("WM_COMMAND: IDC_BTN_MANUAL_CONN received");
            OnClientManualConnect();
            break;
        case IDC_BTN_DISCONNECT: 
            LOG_INFO("WM_COMMAND: IDC_BTN_DISCONNECT received");
            OnClientDisconnect(); 
            break;
        case IDC_BTN_MUTE: 
            OnMuteChanged(); 
            break;
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lParam == g.hInputSlider || (HWND)lParam == g.hOutputSlider)
            OnVolumeChanged();
        break;
    case WM_SIZE:
        if (g.hStatus) SendMessage(g.hStatus, WM_SIZE, 0, 0);
        break;
    case WM_CLOSE:
        // 最小化到托盘而不是关闭
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_TRAYICON: {
        UINT trayMsg = LOWORD(lParam);
        if (trayMsg == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (trayMsg == WM_RBUTTONUP || trayMsg == WM_CONTEXTMENU || trayMsg == WM_RBUTTONDOWN) {
            HMENU hMenu = LoadMenuW(g.hInstance, MAKEINTRESOURCEW(IDM_TRAY));
            if (hMenu) {
                HMENU hPopup = GetSubMenu(hMenu, 0);
                if (hPopup) {
                    POINT pt;
                    GetCursorPos(&pt);
                    SetForegroundWindow(hwnd);
                    SetFocus(hwnd);
                    CheckMenuItem(hPopup, IDM_MUTE,
                        (SendMessage(g.hMuteBtn, BM_GETCHECK, 0, 0) == BST_CHECKED) ? MF_CHECKED : MF_UNCHECKED);
                    UINT cmd = TrackPopupMenuEx(
                        hPopup,
                        TPM_RIGHTBUTTON | TPM_RETURNCMD,
                        pt.x,
                        pt.y,
                        hwnd,
                        NULL);
                    if (cmd) {
                        PostMessageW(hwnd, WM_COMMAND, cmd, 0);
                    }
                    // Ensure menu closes correctly when clicking elsewhere
                    PostMessageW(hwnd, WM_NULL, 0, 0);
                }
                DestroyMenu(hMenu);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        KillTimer(hwnd, IDT_UPDATE);
        if (g.hTitleFont) DeleteObject(g.hTitleFont);
        if (g.hNormalFont) DeleteObject(g.hNormalFont);
        if (g.hBoldFont) DeleteObject(g.hBoldFont);
        if (g.hMonoFont) DeleteObject(g.hMonoFont);
        if (g.hHeaderBrush) DeleteObject(g.hHeaderBrush);
        if (g.hAccentBrush) DeleteObject(g.hAccentBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int Gui_Run(void) {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void Gui_Shutdown(void) {
    if (g.hMain) { DestroyWindow(g.hMain); g.hMain = NULL; }
}

void Gui_UpdateServerList(const void* servers, int count) {
    if (!g.hCliServers) return;
    ListView_DeleteAllItems(g.hCliServers);
    const ServerInfo* list = (const ServerInfo*)servers;
    for (int i = 0; i < count; i++) {
        wchar_t name[64], ip[64], tcpPort[16], udpPort[16], users[16];
        MultiByteToWideChar(CP_UTF8, 0, list[i].name, -1, name, 64);
        MultiByteToWideChar(CP_UTF8, 0, list[i].ip, -1, ip, 64);
        wsprintfW(tcpPort, L"%d", list[i].tcp_port);
        wsprintfW(udpPort, L"%d", list[i].audio_udp_port);
        wsprintfW(users, L"%d/%d", list[i].peer_count, list[i].max_peers);
        
        LVITEMW item = {LVIF_TEXT, i, 0, 0, 0, name, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        ListView_InsertItem(g.hCliServers, &item);
        ListView_SetItemText(g.hCliServers, i, 1, ip);
        ListView_SetItemText(g.hCliServers, i, 2, tcpPort);
        ListView_SetItemText(g.hCliServers, i, 3, udpPort);
        ListView_SetItemText(g.hCliServers, i, 4, users);
    }
}

void Gui_UpdatePeerList(const void* peers, int count) {
    HWND hList = g.currentTab == 0 ? g.hSrvClients : g.hCliPeers;
    if (!hList) return;
    ListView_DeleteAllItems(hList);
    
    if (!peers || count <= 0) return;
    
    const PeerInfo* list = (const PeerInfo*)peers;
    for (int i = 0; i < count; i++) {
        wchar_t name[64], status[32], extra[32], peerType[16];
        MultiByteToWideChar(CP_UTF8, 0, list[i].name, -1, name, 64);
        
        // 用户类型
        switch (list[i].peer_type) {
            case PEER_TYPE_SERVER: wcscpy(peerType, L"服务器"); break;
            case PEER_TYPE_SELF:   wcscpy(peerType, L"本机"); break;
            default:               wcscpy(peerType, L"客户端"); break;
        }
        
        // 状态
        if (list[i].is_muted) {
            wcscpy(status, L"已静音");
        } else if (list[i].is_talking) {
            wcscpy(status, L"说话中");
        } else {
            wcscpy(status, L"在线");
        }
        
        LVITEMW item = {LVIF_TEXT, i, 0, 0, 0, name, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        ListView_InsertItem(hList, &item);
        
        if (g.currentTab == 0) {
            // 服务器模式：用户名、类型、IP、端口、状态
            wchar_t ip[64], port[16];
            MultiByteToWideChar(CP_UTF8, 0, list[i].ip, -1, ip, 64);
            wsprintfW(port, L"%d", list[i].udp_port);
            ListView_SetItemText(hList, i, 1, peerType);
            ListView_SetItemText(hList, i, 2, ip);
            ListView_SetItemText(hList, i, 3, port);
            ListView_SetItemText(hList, i, 4, status);
        } else {
            // 客户端模式：用户名、类型、状态、SSRC
            wsprintfW(extra, L"%u", list[i].ssrc);
            ListView_SetItemText(hList, i, 1, peerType);
            ListView_SetItemText(hList, i, 2, status);
            ListView_SetItemText(hList, i, 3, extra);
        }
    }
}

void Gui_SetConnected(bool connected, const char* info) {
    g.clientConnected = connected;
    UpdateUI();
    if (connected && info) {
        wchar_t w[256], s[300];
        MultiByteToWideChar(CP_UTF8, 0, info, -1, w, 256);
        wsprintfW(s, L"已连接: %s", w);
        SetWindowTextW(g.hCliStatus, s);
    } else {
        SetWindowTextW(g.hCliStatus, L"状态: 未连接");
    }
}

void Gui_SetServerRunning(bool running) {
    g.serverRunning = running;
    UpdateUI();
    
    if (running) {
        wchar_t port[16], udpPort[16], discPort[16];
        GetWindowTextW(g.hSrvPort, port, 16);
        GetWindowTextW(g.hSrvUdpPort, udpPort, 16);
        GetWindowTextW(g.hSrvDiscPort, discPort, 16);
        wchar_t statusMsg[128];
        wsprintfW(statusMsg, L"服务器运行中 TCP:%s UDP:%s", port, udpPort);
        SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)statusMsg);
        
        wchar_t discMsg[64];
        wsprintfW(discMsg, L"发现端口: %s", discPort);
        SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)discMsg);
    } else {
        SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)L"服务器已停止");
        SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)L"发现端口: 37020");
    }
}

void Gui_UpdateAudioLevel(float in, float out) {
    if (g.hInputLevel) {
        int level = (int)(in * 100);
        SendMessage(g.hInputLevel, PBM_SETPOS, (WPARAM)level, 0);
        if (level > 80) {
            SendMessage(g.hInputLevel, PBM_SETBARCOLOR, 0, COLOR_DANGER);
        } else if (level > 50) {
            SendMessage(g.hInputLevel, PBM_SETBARCOLOR, 0, COLOR_WARNING);
        } else {
            SendMessage(g.hInputLevel, PBM_SETBARCOLOR, 0, COLOR_SUCCESS);
        }
    }
    if (g.hOutputLevel) {
        int level = (int)(out * 100);
        SendMessage(g.hOutputLevel, PBM_SETPOS, (WPARAM)level, 0);
        if (level > 80) {
            SendMessage(g.hOutputLevel, PBM_SETBARCOLOR, 0, COLOR_DANGER);
        } else if (level > 50) {
            SendMessage(g.hOutputLevel, PBM_SETBARCOLOR, 0, COLOR_WARNING);
        } else {
            SendMessage(g.hOutputLevel, PBM_SETBARCOLOR, 0, COLOR_ACCENT);
        }
    }
}

void Gui_AddLog(const char* msg) { 
    LOG_INFO("GUI Log: %s", msg);
}

void Gui_ShowError(const char* msg) {
    wchar_t w[512];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 512);
    MessageBoxW(g.hMain, w, L"错误", MB_OK|MB_ICONERROR);
}

void Gui_GetClientUsername(char* name, int max_len) {
    if (!name || max_len <= 0) return;
    
    wchar_t wname[64];
    GetWindowTextW(g.hCliUsername, wname, 64);
    WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, max_len, NULL, NULL);
    
    // 如果为空，使用默认名
    if (name[0] == '\0') {
        strncpy(name, "User", max_len - 1);
        name[max_len - 1] = '\0';
    }
}