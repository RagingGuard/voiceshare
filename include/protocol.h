/**
 * @file protocol.h
 * @brief 网络协议定义 (TCP控制 + UDP音频 + RTP-like)
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

//=============================================================================
// 消息类型
//=============================================================================
typedef enum {
    // 发现协议 (UDP 广播)
    MSG_DISCOVERY_REQUEST   = 0x0001,
    MSG_DISCOVERY_RESPONSE  = 0x0002,
    
    // TCP 会话控制
    MSG_HELLO               = 0x0101,   // 客户端握手
    MSG_HELLO_ACK           = 0x0102,   // 服务端握手确认
    MSG_JOIN_SESSION        = 0x0103,   // 加入语音会话
    MSG_LEAVE_SESSION       = 0x0104,   // 离开语音会话
    MSG_HEARTBEAT           = 0x0105,   // 心跳
    
    // TCP 音频控制
    MSG_AUDIO_START         = 0x0201,   // 开始语音
    MSG_AUDIO_STOP          = 0x0202,   // 停止语音
    MSG_AUDIO_MUTE          = 0x0203,   // 静音
    MSG_AUDIO_UNMUTE        = 0x0204,   // 取消静音
    MSG_PARAM_UPDATE        = 0x0205,   // 参数更新
    MSG_TIME_SYNC           = 0x0206,   // 时间同步
    
    // TCP 状态通知
    MSG_PEER_LIST           = 0x0301,   // 用户列表
    MSG_PEER_JOIN           = 0x0302,   // 用户加入
    MSG_PEER_LEAVE          = 0x0303,   // 用户离开
    MSG_PEER_STATE          = 0x0304    // 用户状态变化
} MessageType;

//=============================================================================
// RTP Payload Type (音频编码类型)
//=============================================================================
typedef enum {
    PAYLOAD_PCM     = 0,    // 未压缩 PCM
    PAYLOAD_OPUS    = 111   // Opus 编码
} PayloadType;

//=============================================================================
// 报文结构 (网络字节序)
//=============================================================================
#pragma pack(push, 1)

/**
 * @brief TCP 控制报文头
 */
typedef struct {
    uint32_t magic;         // 魔数 PROTOCOL_MAGIC
    uint16_t version;       // 协议版本
    uint16_t msg_type;      // 消息类型
    uint32_t payload_len;   // 负载长度
    uint32_t sequence;      // 序列号
    uint32_t timestamp;     // 时间戳 (毫秒)
} PacketHeader;

/**
 * @brief RTP-like 音频包头 (UDP)
 * 
 * 参考 RFC 3550, 但简化为局域网使用
 */
typedef struct {
    uint8_t  version;       // 版本 (2)
    uint8_t  payload_type;  // 负载类型 (PayloadType)
    uint16_t sequence;      // 序列号 (回绕)
    uint32_t timestamp;     // 采样时间戳 (48kHz 基准)
    uint32_t ssrc;          // 同步源标识符 (发送者ID)
    uint16_t payload_len;   // 负载长度
    uint16_t flags;         // 标志位: bit0=marker, bit1=vad_active
} RtpHeader;

/**
 * @brief 完整 RTP 音频包
 */
typedef struct {
    RtpHeader header;
    uint8_t   payload[OPUS_MAX_PACKET];
} RtpPacket;

/**
 * @brief 发现请求 (UDP 广播)
 */
typedef struct {
    PacketHeader header;
    uint32_t client_id;         // 客户端ID
    uint32_t service_mask;      // 服务类型掩码
    char     client_name[MAX_NAME_LEN];
} DiscoveryRequest;

/**
 * @brief 发现响应 (UDP 单播)
 */
typedef struct {
    PacketHeader header;
    uint32_t server_id;         // 服务器ID
    uint16_t tcp_port;          // TCP 控制端口
    uint16_t audio_udp_port;    // UDP 音频端口
    uint32_t capability_flags;  // 能力标志 (Opus/VAD/Jitter)
    uint8_t  current_peers;     // 当前连接数
    uint8_t  max_peers;         // 最大连接数
    uint8_t  reserved[2];       // 保留
    char     server_name[MAX_NAME_LEN];
    char     version_str[16];   // 版本字符串
} DiscoveryResponse;

// 能力标志位
#define CAP_OPUS        0x0001  // 支持 Opus 编码
#define CAP_VAD         0x0002  // 支持 VAD
#define CAP_JITTER      0x0004  // 支持 Jitter Buffer

/**
 * @brief HELLO 握手请求 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t client_id;
    uint32_t capability_flags;
    char     client_name[MAX_NAME_LEN];
} HelloRequest;

/**
 * @brief HELLO 握手确认 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t result;            // 0=成功
    uint32_t assigned_id;       // 分配的客户端ID
    uint16_t audio_udp_port;    // 服务器 UDP 音频端口
    uint16_t reserved;
    uint64_t server_time;       // 服务器时间戳 (用于同步)
} HelloAck;

/**
 * @brief 加入会话请求 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t client_id;
    uint16_t local_udp_port;    // 客户端本地 UDP 端口
    uint16_t reserved;
} JoinSessionRequest;

/**
 * @brief 加入会话确认 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t result;
    uint32_t ssrc;              // 分配的 SSRC
    uint64_t base_timestamp;    // 基准时间戳
} JoinSessionAck;

/**
 * @brief 心跳包 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t client_id;
    uint64_t local_time;        // 本地时间戳
} HeartbeatPacket;

/**
 * @brief 音频控制 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t client_id;
    uint8_t  action;            // 0=stop, 1=start
    uint8_t  muted;             // 是否静音
    uint8_t  reserved[2];
} AudioControlPacket;

/**
 * @brief 参数更新 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint32_t bitrate;           // 编码码率
    uint8_t  frame_ms;          // 帧长度 (毫秒)
    uint8_t  complexity;        // 编码复杂度
    uint8_t  reserved[2];
} ParamUpdatePacket;

/**
 * @brief 时间同步 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint64_t server_time;       // 服务器时间
    uint64_t base_timestamp;    // 音频基准时间戳
} TimeSyncPacket;

/**
 * @brief 用户类型
 */
typedef enum {
    PEER_TYPE_CLIENT = 0,   // 普通客户端
    PEER_TYPE_SERVER = 1,   // 服务器
    PEER_TYPE_SELF   = 2    // 本机
} PeerType;

/**
 * @brief 用户信息
 */
typedef struct {
    uint32_t client_id;
    uint32_t ssrc;              // RTP SSRC
    char     name[MAX_NAME_LEN];
    char     ip[16];            // IP地址字符串
    uint16_t udp_port;          // UDP 端口
    uint8_t  is_talking;        // 是否在说话
    uint8_t  is_muted;          // 是否静音
    uint8_t  audio_active;      // 音频是否激活
    uint8_t  peer_type;         // 用户类型 (PeerType)
} PeerInfo;

/**
 * @brief 用户列表 (TCP)
 */
typedef struct {
    PacketHeader header;
    uint8_t  peer_count;
    uint8_t  reserved[3];
    // 后接 PeerInfo 数组
} PeerListPacket;

/**
 * @brief 用户状态通知 (TCP)
 */
typedef struct {
    PacketHeader header;
    PeerInfo peer;
} PeerNotifyPacket;

#pragma pack(pop)

//=============================================================================
// RTP 辅助函数
//=============================================================================

/**
 * @brief 初始化 RTP 包头
 */
static inline void RtpHeader_Init(RtpHeader* hdr, uint32_t ssrc, uint8_t payload_type) {
    hdr->version = 2;
    hdr->payload_type = payload_type;
    hdr->sequence = 0;
    hdr->timestamp = 0;
    hdr->ssrc = ssrc;
    hdr->payload_len = 0;
    hdr->flags = 0;
}

/**
 * @brief 设置 RTP marker 位
 */
static inline void RtpHeader_SetMarker(RtpHeader* hdr, bool marker) {
    if (marker) hdr->flags |= 0x01;
    else hdr->flags &= ~0x01;
}

/**
 * @brief 设置 RTP VAD 激活位
 */
static inline void RtpHeader_SetVadActive(RtpHeader* hdr, bool active) {
    if (active) hdr->flags |= 0x02;
    else hdr->flags &= ~0x02;
}

/**
 * @brief 检查 RTP marker 位
 */
static inline bool RtpHeader_GetMarker(const RtpHeader* hdr) {
    return (hdr->flags & 0x01) != 0;
}

/**
 * @brief 检查 RTP VAD 激活位
 */
static inline bool RtpHeader_GetVadActive(const RtpHeader* hdr) {
    return (hdr->flags & 0x02) != 0;
}

//=============================================================================
// TCP 报文辅助函数
//=============================================================================

/**
 * @brief 初始化报文头
 */
static inline void PacketHeader_Init(PacketHeader* hdr, uint16_t type, uint32_t payload_len) {
    hdr->magic = PROTOCOL_MAGIC;
    hdr->version = PROTOCOL_VERSION;
    hdr->msg_type = type;
    hdr->payload_len = payload_len;
    hdr->sequence = 0;
    hdr->timestamp = GetTickCountMs();
}

/**
 * @brief 验证报文头
 */
static inline bool PacketHeader_Validate(const PacketHeader* hdr) {
    return hdr->magic == PROTOCOL_MAGIC;
}

/**
 * @brief 获取完整报文大小
 */
static inline uint32_t Packet_GetTotalSize(const PacketHeader* hdr) {
    return sizeof(PacketHeader) + hdr->payload_len;
}

#endif // PROTOCOL_H
