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
    // 服务器端口配置: tcp_port=控制端口, udp_port=音频端口, discovery_port=发现端口
    void (*onStartServer)(const char* name, uint16_t tcp_port, uint16_t udp_port, uint16_t discovery_port, void* userdata);
    void (*onStopServer)(void* userdata);
    // 客户端连接: tcp_port=控制端口, udp_port=音频端口
    void (*onConnect)(const char* ip, uint16_t tcp_port, uint16_t udp_port, void* userdata);
    void (*onDisconnect)(void* userdata);
    // discovery_port: 服务发现端口
    void (*onRefreshServers)(uint16_t discovery_port, void* userdata);
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

/**
 * @brief 获取客户端用户名
 * @param name 输出缓冲区
 * @param max_len 缓冲区大小
 */
void Gui_GetClientUsername(char* name, int max_len);

#endif // GUI_H
