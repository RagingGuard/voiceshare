/* gui.c - Win32 GUI with Server/Client Tab interface */
#include "gui.h"
#include "network.h"
#include "../res/resource.h"
#include <shellapi.h>
#include <stdio.h>
#include <uxtheme.h>

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
    HWND hCliServers, hCliRefresh, hCliConnect, hCliDisconnect, hCliPeers, hCliStatus;
    HWND hCliGroupServers, hCliGroupUsers;
    // 公共控件
    HWND hMuteBtn, hInputSlider, hOutputSlider, hInputLevel, hOutputLevel, hStatus;
    HWND hHeaderLabel, hAudioGroup;
    HWND hLblInput, hLblOutput, hLblInLevel, hLblOutLevel;
    NOTIFYICONDATAW nid;
    int currentTab;
    BOOL serverRunning, clientConnected;
    GuiCallbacks callbacks;
    HFONT hTitleFont, hNormalFont, hBoldFont;
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
        LoadIcon(NULL,IDI_APPLICATION), LoadCursor(NULL,IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW+1), NULL, WND_CLASS, LoadIcon(NULL,IDI_APPLICATION)};
    if (!RegisterClassExW(&wc)) return false;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    // 创建字体和画刷
    CreateFonts();
    g.hHeaderBrush = CreateSolidBrush(COLOR_HEADER_BG);
    g.hAccentBrush = CreateSolidBrush(COLOR_ACCENT);

    g.hMain = CreateWindowExW(0, WND_CLASS, L"SharedVoice - 局域网语音通话 v1.0", 
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 580, 540, NULL, NULL, hInstance, NULL);
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
    SwitchTab(0);

    // 状态栏 - 分区显示
    int parts[] = {220, 380, -1};
    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
        0, 0, 0, 0, g.hMain, (HMENU)IDC_STATUS, hInstance, NULL);
    SendMessage(g.hStatus, SB_SETPARTS, 3, (LPARAM)parts);
    SendMessage(g.hStatus, SB_SETTEXTW, 0, (LPARAM)L"就绪");
    SendMessage(g.hStatus, SB_SETTEXTW, 1, (LPARAM)L"发现端口: 37020");
    SendMessage(g.hStatus, SB_SETTEXTW, 2, (LPARAM)L"v1.0");

    SetTimer(g.hMain, IDT_UPDATE, 100, NULL);
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

    LVCOLUMNW col = {LVCF_TEXT|LVCF_WIDTH, 0, 140, L"用户名", 0, 0, 0, 0};
    ListView_InsertColumn(g.hSrvClients, 0, &col);
    col.cx = 130; col.pszText = L"IP地址";
    ListView_InsertColumn(g.hSrvClients, 1, &col);
    col.cx = 80; col.pszText = L"UDP端口";
    ListView_InsertColumn(g.hSrvClients, 2, &col);
    col.cx = 80; col.pszText = L"状态";
    ListView_InsertColumn(g.hSrvClients, 3, &col);
    ListView_SetExtendedListViewStyle(g.hSrvClients, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
}

static void CreateClientControls(void) {
    int baseY = 75;
    int y = baseY;

    // 服务器列表区域
    g.hCliGroupServers = CreateWindowExW(0, L"BUTTON", L" 发现的服务器 ", WS_CHILD|BS_GROUPBOX,
        20, y, 535, 120, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliGroupServers, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 18;

    g.hCliServers = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 30, y, 515, 70,
        g.hMain, (HMENU)IDC_SERVER_LIST, g.hInstance, NULL);
    SendMessage(g.hCliServers, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    LVCOLUMNW col = {LVCF_TEXT|LVCF_WIDTH, 0, 150, L"服务器名称", 0, 0, 0, 0};
    ListView_InsertColumn(g.hCliServers, 0, &col);
    col.cx = 100; col.pszText = L"IP地址";
    ListView_InsertColumn(g.hCliServers, 1, &col);
    col.cx = 70; col.pszText = L"TCP端口";
    ListView_InsertColumn(g.hCliServers, 2, &col);
    col.cx = 70; col.pszText = L"UDP端口";
    ListView_InsertColumn(g.hCliServers, 3, &col);
    col.cx = 55; col.pszText = L"在线";
    ListView_InsertColumn(g.hCliServers, 4, &col);
    ListView_SetExtendedListViewStyle(g.hCliServers, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    y += 75;

    // 按钮行
    g.hCliRefresh = CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD|BS_PUSHBUTTON,
        30, y, 80, 26, g.hMain, (HMENU)IDC_BTN_REFRESH, g.hInstance, NULL);
    SendMessage(g.hCliRefresh, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    g.hCliConnect = CreateWindowExW(0, L"BUTTON", L"连接", WS_CHILD|BS_PUSHBUTTON,
        120, y, 80, 26, g.hMain, (HMENU)IDC_BTN_CONNECT, g.hInstance, NULL);
    SendMessage(g.hCliConnect, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    g.hCliDisconnect = CreateWindowExW(0, L"BUTTON", L"断开", WS_CHILD|WS_DISABLED|BS_PUSHBUTTON,
        210, y, 80, 26, g.hMain, (HMENU)IDC_BTN_DISCONNECT, g.hInstance, NULL);
    SendMessage(g.hCliDisconnect, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);

    g.hCliStatus = CreateWindowExW(0, L"STATIC", L"状态: 未连接", WS_CHILD|SS_LEFT,
        310, y+5, 230, 20, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliStatus, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 33;

    // 在线用户区域
    g.hCliGroupUsers = CreateWindowExW(0, L"BUTTON", L" 在线用户 ", WS_CHILD|BS_GROUPBOX,
        20, y, 535, 100, g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliGroupUsers, WM_SETFONT, (WPARAM)g.hBoldFont, TRUE);
    y += 18;

    g.hCliPeers = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 30, y, 515, 72,
        g.hMain, NULL, g.hInstance, NULL);
    SendMessage(g.hCliPeers, WM_SETFONT, (WPARAM)g.hNormalFont, TRUE);

    col.cx = 200; col.pszText = L"用户名";
    ListView_InsertColumn(g.hCliPeers, 0, &col);
    col.cx = 100; col.pszText = L"状态";
    ListView_InsertColumn(g.hCliPeers, 1, &col);
    col.cx = 100; col.pszText = L"SSRC";
    ListView_InsertColumn(g.hCliPeers, 2, &col);
    ListView_SetExtendedListViewStyle(g.hCliPeers, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
}

static void CreateCommonControls(void) {
    int y = 350;

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

    g.hLblInLevel = CreateWindowExW(0, L"STATIC", L"电平:", WS_CHILD|WS_VISIBLE,
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

    g.hLblOutLevel = CreateWindowExW(0, L"STATIC", L"电平:", WS_CHILD|WS_VISIBLE,
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
    ShowWindow(g.hCliConnect, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliDisconnect, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliPeers, showClient ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hCliStatus, showClient ? SW_SHOW : SW_HIDE);
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
    EnableWindow(g.hCliConnect, !g.clientConnected);
    EnableWindow(g.hCliDisconnect, g.clientConnected);
    
    if (!g.clientConnected) {
        SetWindowTextW(g.hCliStatus, L"状态: 未连接");
    }
}

static void OnServerStart(void) {
    wchar_t name[64], port[16], udpPort[16];
    GetWindowTextW(g.hSrvName, name, 64);
    GetWindowTextW(g.hSrvPort, port, 16);
    GetWindowTextW(g.hSrvUdpPort, udpPort, 16);
    
    char nameA[64];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, nameA, 64, NULL, NULL);
    int p = _wtoi(port);
    int udpP = _wtoi(udpPort);
    if (p <= 0 || p > 65535) p = 5000;
    if (udpP <= 0 || udpP > 65535) udpP = 6000;
    
    LOG_INFO("GUI: OnServerStart called - name=%s, tcp_port=%d, udp_port=%d", nameA, p, udpP);
    
    if (g.callbacks.onStartServer) {
        g.callbacks.onStartServer(nameA, (uint16_t)p, g.callbacks.userdata);
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
    LOG_INFO("GUI: OnClientRefresh called");
    if (g.callbacks.onRefreshServers) g.callbacks.onRefreshServers(g.callbacks.userdata);
}

static void OnClientConnect(void) {
    int sel = ListView_GetNextItem(g.hCliServers, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(g.hMain, L"请先选择一个服务器", L"连接", MB_OK|MB_ICONINFORMATION);
        return;
    }
    wchar_t ip[64], portW[16];
    ListView_GetItemText(g.hCliServers, sel, 1, ip, 64);
    ListView_GetItemText(g.hCliServers, sel, 2, portW, 16);
    
    char ipA[64];
    WideCharToMultiByte(CP_UTF8, 0, ip, -1, ipA, 64, NULL, NULL);
    int port = _wtoi(portW);
    if (port <= 0) port = 5000;
    
    LOG_INFO("GUI: OnClientConnect called - ip=%s, port=%d", ipA, port);
    if (g.callbacks.onConnect) g.callbacks.onConnect(ipA, (uint16_t)port, g.callbacks.userdata);
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
        break;
    }
    case WM_COMMAND:
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
        case IDC_BTN_CONNECT: 
            LOG_INFO("WM_COMMAND: IDC_BTN_CONNECT received");
            OnClientConnect(); 
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
        if (g.serverRunning || g.clientConnected) {
            if (MessageBoxW(hwnd, L"当前有活动连接，确定要退出吗？", L"确认退出", MB_YESNO|MB_ICONQUESTION) != IDYES)
                return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_UPDATE);
        if (g.hTitleFont) DeleteObject(g.hTitleFont);
        if (g.hNormalFont) DeleteObject(g.hNormalFont);
        if (g.hBoldFont) DeleteObject(g.hBoldFont);
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
        wchar_t name[64], status[32], extra[32];
        MultiByteToWideChar(CP_UTF8, 0, list[i].name, -1, name, 64);
        
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
            wchar_t ip[64], port[16];
            MultiByteToWideChar(CP_UTF8, 0, list[i].ip, -1, ip, 64);
            wsprintfW(port, L"%d", list[i].udp_port);
            ListView_SetItemText(hList, i, 1, ip);
            ListView_SetItemText(hList, i, 2, port);
            ListView_SetItemText(hList, i, 3, status);
        } else {
            wsprintfW(extra, L"%u", list[i].ssrc);
            ListView_SetItemText(hList, i, 1, status);
            ListView_SetItemText(hList, i, 2, extra);
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