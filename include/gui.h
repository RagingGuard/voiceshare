/**
 * @file gui.h
 * @brief Win32 GUI 头文件
 */

#ifndef GUI_H
#define GUI_H

#include "common.h"
#include <commctrl.h>

//=============================================================================
// GUI 回调
//=============================================================================
typedef struct {
    void (*onStartServer)(const char* name, uint16_t port, void* userdata);
    void (*onStopServer)(void* userdata);
    void (*onConnect)(const char* ip, uint16_t port, void* userdata);
    void (*onDisconnect)(void* userdata);
    void (*onRefreshServers)(void* userdata);
    void (*onMuteChanged)(bool muted, void* userdata);
    void (*onVolumeChanged)(int input, int output, void* userdata);
    void* userdata;
} GuiCallbacks;

//=============================================================================
// GUI 接口
//=============================================================================

/**
 * @brief 初始化 GUI
 */
bool Gui_Init(HINSTANCE hInstance, const GuiCallbacks* callbacks);

/**
 * @brief 运行消息循环
 */
int Gui_Run(void);

/**
 * @brief 关闭 GUI
 */
void Gui_Shutdown(void);

/**
 * @brief 更新服务器列表
 */
void Gui_UpdateServerList(const void* servers, int count);

/**
 * @brief 更新用户列表
 */
void Gui_UpdatePeerList(const void* peers, int count);

/**
 * @brief 设置连接状态
 */
void Gui_SetConnected(bool connected, const char* server_info);

/**
 * @brief 设置服务器状态
 */
void Gui_SetServerRunning(bool running);

/**
 * @brief 更新音频电平
 */
void Gui_UpdateAudioLevel(float input, float output);

/**
 * @brief 添加日志
 */
void Gui_AddLog(const char* msg);

/**
 * @brief 显示错误消息
 */
void Gui_ShowError(const char* msg);

#endif // GUI_H
