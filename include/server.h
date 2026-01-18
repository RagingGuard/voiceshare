/**
 * @file server.h
 * @brief 服务器模块头文件 (TCP控制 + UDP音频)
 */

#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "protocol.h"

//=============================================================================
// 服务器事件回调
//=============================================================================
typedef struct {
    void (*onStarted)(void* userdata);
    void (*onStopped)(void* userdata);
    void (*onClientJoined)(uint32_t client_id, const char* name, void* userdata);
    void (*onClientLeft)(uint32_t client_id, void* userdata);
    void (*onAudioReceived)(uint32_t client_id, const int16_t* pcm, int samples, void* userdata);
    void (*onError)(const char* msg, void* userdata);
    void* userdata;
} ServerCallbacks;

//=============================================================================
// 服务器接口
//=============================================================================

/**
 * @brief 初始化服务器模块
 */
bool Server_Init(void);

/**
 * @brief 关闭服务器模块
 */
void Server_Shutdown(void);

/**
 * @brief 启动服务器
 * @param name 服务器名称
 * @param tcp_port TCP 控制端口
 * @param udp_port UDP 音频端口
 * @param discovery_port UDP 发现端口
 * @param callbacks 事件回调
 */
bool Server_Start(const char* name, uint16_t tcp_port, uint16_t udp_port, 
                  uint16_t discovery_port, const ServerCallbacks* callbacks);

/**
 * @brief 停止服务器
 */
void Server_Stop(void);

/**
 * @brief 服务器是否运行中
 */
bool Server_IsRunning(void);

/**
 * @brief 获取服务器名称
 */
const char* Server_GetName(void);

/**
 * @brief 获取服务器 TCP 端口
 */
uint16_t Server_GetTcpPort(void);

/**
 * @brief 获取服务器 UDP 音频端口
 */
uint16_t Server_GetUdpPort(void);

/**
 * @brief 获取已连接客户端数
 */
int Server_GetClientCount(void);

/**
 * @brief 获取客户端列表
 */
int Server_GetClients(PeerInfo* peers, int max_count);

/**
 * @brief 发送 Opus 编码音频 (UDP)
 * @param opus_data Opus 编码数据
 * @param opus_len 数据长度
 * @param timestamp 采样时间戳
 */
void Server_SendOpusAudio(const uint8_t* opus_data, int opus_len, uint32_t timestamp);

/**
 * @brief 广播音频控制消息 (TCP)
 */
void Server_BroadcastAudioControl(uint8_t action, uint8_t muted);

#endif // SERVER_H
