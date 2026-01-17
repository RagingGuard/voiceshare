/**
 * @file network.h
 * @brief 网络层头文件 (TCP控制 + UDP音频)
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"
#include "protocol.h"

//=============================================================================
// 服务器信息结构
//=============================================================================
typedef struct {
    uint32_t    server_id;
    char        name[MAX_NAME_LEN];
    char        ip[16];
    uint16_t    tcp_port;           // TCP 控制端口
    uint16_t    audio_udp_port;     // UDP 音频端口
    uint32_t    capability_flags;   // 能力标志
    uint8_t     peer_count;
    uint8_t     max_peers;
    uint32_t    last_seen;          // 最后发现时间
    bool        valid;
} ServerInfo;

//=============================================================================
// UDP 音频客户端信息 (服务器端用于跟踪)
//=============================================================================
typedef struct {
    uint32_t    client_id;
    uint32_t    ssrc;
    SOCKADDR_IN addr;               // 客户端 UDP 地址
    bool        active;
} AudioClientInfo;

//=============================================================================
// 网络模块接口
//=============================================================================

/**
 * @brief 初始化网络模块
 */
bool Network_Init(void);

/**
 * @brief 关闭网络模块
 */
void Network_Shutdown(void);

/**
 * @brief 创建UDP广播socket (发现用)
 */
SOCKET Network_CreateUdpBroadcast(uint16_t port, bool bind_port);

/**
 * @brief 创建UDP音频socket
 * @param port 绑定端口 (0=自动分配)
 * @param out_port 输出实际绑定的端口
 */
SOCKET Network_CreateUdpAudio(uint16_t port, uint16_t* out_port);

/**
 * @brief 创建TCP监听socket (控制通道)
 */
SOCKET Network_CreateTcpListener(uint16_t port);

/**
 * @brief 创建TCP客户端连接 (控制通道)
 */
SOCKET Network_TcpConnect(const char* ip, uint16_t port);

/**
 * @brief 发送UDP广播
 */
int Network_UdpBroadcast(SOCKET sock, const void* data, int len, uint16_t port);

/**
 * @brief 发送UDP单播
 */
int Network_UdpSendTo(SOCKET sock, const void* data, int len, const SOCKADDR_IN* addr);

/**
 * @brief 接收UDP数据
 */
int Network_UdpRecvFrom(SOCKET sock, void* buf, int len, SOCKADDR_IN* from);

/**
 * @brief 发送RTP音频包 (UDP)
 */
int Network_SendRtpPacket(SOCKET sock, const RtpHeader* rtp, const uint8_t* payload,
                           uint16_t payload_len, const SOCKADDR_IN* addr);

/**
 * @brief 接收RTP音频包 (UDP)
 * @return payload长度, <0 表示错误
 */
int Network_RecvRtpPacket(SOCKET sock, RtpHeader* rtp, uint8_t* payload,
                           uint16_t max_len, SOCKADDR_IN* from);

/**
 * @brief 发送TCP数据 (控制通道)
 */
int Network_TcpSend(SOCKET sock, const void* data, int len);

/**
 * @brief 接收TCP数据 (控制通道)
 */
int Network_TcpRecv(SOCKET sock, void* buf, int len);

/**
 * @brief 接收完整控制报文 (TCP)
 */
int Network_TcpRecvPacket(SOCKET sock, void* buf, int max_len);

/**
 * @brief 设置socket非阻塞
 */
bool Network_SetNonBlocking(SOCKET sock, bool nonblocking);

/**
 * @brief 设置socket接收超时
 */
bool Network_SetRecvTimeout(SOCKET sock, int timeout_ms);

/**
 * @brief 关闭socket
 */
void Network_CloseSocket(SOCKET sock);

/**
 * @brief 获取本机IP地址
 */
bool Network_GetLocalIP(char* ip, int len);

/**
 * @brief 构造 SOCKADDR_IN
 */
void Network_MakeAddr(SOCKADDR_IN* addr, const char* ip, uint16_t port);

#endif // NETWORK_H
