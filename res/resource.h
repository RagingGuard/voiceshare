/**
 * @file resource.h
 * @brief 资源定义
 */

#ifndef RESOURCE_H
#define RESOURCE_H

// 图标
#define IDI_APP             100
#define IDI_CONNECTED       101
#define IDI_DISCONNECTED    102

// 菜单
#define IDM_TRAY            200
#define IDM_SHOW            201
#define IDM_MUTE            202
#define IDM_EXIT            203

// 控件ID
#define IDC_TAB             1000
#define IDC_MODE_GROUP      1001
#define IDC_RADIO_SERVER    1002
#define IDC_RADIO_CLIENT    1003
#define IDC_SERVER_NAME     1004
#define IDC_SERVER_PORT     1005
#define IDC_BTN_START       1006
#define IDC_BTN_STOP        1007
#define IDC_SERVER_LIST     1010
#define IDC_BTN_REFRESH     1011
#define IDC_BTN_CONNECT     1012
#define IDC_BTN_DISCONNECT  1013
#define IDC_BTN_MUTE        1020
#define IDC_SLIDER_INPUT    1021
#define IDC_SLIDER_OUTPUT   1022
#define IDC_LEVEL_INPUT     1023
#define IDC_LEVEL_OUTPUT    1024
#define IDC_PEER_LIST       1030
#define IDC_STATUS          1040
#define IDC_CLIENT_NAME     1050
// 客户端手动连接控件
#define IDC_MANUAL_IP       1060
#define IDC_MANUAL_TCP      1061
#define IDC_MANUAL_UDP      1062
#define IDC_MANUAL_DISC     1063
#define IDC_BTN_MANUAL_CONN 1064

// 定时器
#define IDT_UPDATE          1
#define IDT_DISCOVERY       2

// 托盘消息
#define WM_TRAYICON         (WM_USER + 1)

// 嵌入式DLL资源
#define IDR_OPUS_DLL        300

#endif // RESOURCE_H
