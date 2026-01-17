/**
 * @file client.h
 * @brief 客户端模块头文件 (TCP控制 + UDP音频 + JitterBuffer)
 */

#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include "protocol.h"
#include "network.h"

//=============================================================================
// 客户端事件回调
//=============================================================================
typedef struct {
    void (*onConnected)(void* userdata);
    void (*onDisconnected)(void* userdata);
    void (*onServerFound)(const ServerInfo* server, void* userdata);
    void (*onPeerJoined)(const PeerInfo* peer, void* userdata);
    void (*onPeerLeft)(uint32_t client_id, void* userdata);
    void (*onPeerListReceived)(const PeerInfo* peers, int count, void* userdata);
    void (*onAudioReceived)(const int16_t* pcm, int samples, void* userdata);
    void (*onError)(const char* msg, void* userdata);
    void* userdata;
} ClientCallbacks;

//=============================================================================
// 客户端接口
//=============================================================================

/**
 * @brief 初始化客户端模块
 */
bool Client_Init(void);

/**
 * @brief 关闭客户端模块
 */
void Client_Shutdown(void);

/**
 * @brief 设置本地名称
 */
void Client_SetName(const char* name);

/**
 * @brief 获取本地名称
 */
const char* Client_GetName(void);

/**
 * @brief 设置事件回调
 */
void Client_SetCallbacks(const ClientCallbacks* callbacks);

/**
 * @brief 开始服务发现
 */
bool Client_StartDiscovery(void);

/**
 * @brief 停止服务发现
 */
void Client_StopDiscovery(void);

/**
 * @brief 获取已发现的服务器列表
 */
int Client_GetServers(ServerInfo* servers, int max_count);

/**
 * @brief 连接到服务器 (TCP 控制)
 */
bool Client_Connect(const char* ip, uint16_t tcp_port, uint16_t audio_udp_port);

/**
 * @brief 断开连接
 */
void Client_Disconnect(void);

/**
 * @brief 是否已连接
 */
bool Client_IsConnected(void);

/**
 * @brief 加入语音会话
 */
bool Client_JoinSession(void);

/**
 * @brief 离开语音会话
 */
void Client_LeaveSession(void);

/**
 * @brief 是否在语音会话中
 */
bool Client_IsInSession(void);

/**
 * @brief 获取当前连接的服务器信息
 */
bool Client_GetCurrentServer(ServerInfo* server);

/**
 * @brief 发送 Opus 编码音频 (UDP)
 */
void Client_SendOpusAudio(const uint8_t* opus_data, int opus_len, uint32_t timestamp);

/**
 * @brief 获取在线用户列表
 */
int Client_GetPeers(PeerInfo* peers, int max_count);

/**
 * @brief 获取 Jitter Buffer 统计
 */
void Client_GetJitterStats(float* jitter_ms, float* loss_rate, int* buffer_level);

/**
 * @brief 获取分配的 SSRC
 */
uint32_t Client_GetSSRC(void);

#endif // CLIENT_H
