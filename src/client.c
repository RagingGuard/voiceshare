/**
 * @file client.c
 * @brief 客户端模块实现 (TCP控制 + UDP音频 + JitterBuffer)
 * 
 * 架构:
 * - UDP 发现线程: 发送广播, 接收服务器响应
 * - TCP 控制线程: 会话管理、心跳、音频控制
 * - UDP 音频线程: 接收 RTP 包 -> JitterBuffer -> 解码 -> 播放
 * - 播放线程: 从 JitterBuffer 取数据播放
 */

#include "client.h"
#include "opus_codec.h"
#include "jitter_buffer.h"
#include "audio.h"
#include "gui.h"

//=============================================================================
// 客户端状态
//=============================================================================
typedef struct {
    bool            initialized;
    bool            connected;
    bool            discovering;
    bool            in_session;
    char            name[MAX_NAME_LEN];
    uint32_t        client_id;
    uint32_t        ssrc;               // 分配的 SSRC
    uint16_t        discovery_port;     // 发现端口（可配置）
    
    // 网络
    SOCKET          udp_discovery;      // UDP 发现
    SOCKET          tcp_control;        // TCP 控制连接
    SOCKET          udp_audio;          // UDP 音频
    uint16_t        local_udp_port;     // 本地 UDP 端口
    
    // 服务器信息
    ServerInfo      current_server;
    char            server_ip[16];
    uint16_t        server_tcp_port;
    uint16_t        server_udp_port;
    SOCKADDR_IN     server_audio_addr;
    
    // 发现的服务器
    ServerInfo      servers[MAX_SERVERS];
    int             server_count;
    Mutex           servers_mutex;
    
    // 在线用户
    PeerInfo        peers[MAX_CLIENTS];
    int             peer_count;
    Mutex           peers_mutex;
    
    // 线程
    Thread          discovery_thread;
    Thread          tcp_recv_thread;
    Thread          heartbeat_thread;
    Thread          udp_audio_thread;
    Thread          playback_thread;
    Event           stop_event;
    
    // TCP 接收缓冲
    uint8_t         recv_buf[MAX_PACKET_SIZE];
    int             recv_len;
    
    // RTP 发送
    uint16_t        rtp_sequence;
    uint32_t        rtp_timestamp;
    
    // Opus 解码器 (保留用于发送端，接收端使用 MultiStreamJB)
    OpusCodec*      opus_decoder;
    
    // 多流 Jitter Buffer (per-SSRC)
    MultiStreamJitterBuffer* multi_jitter_buffer;
    
    // 回调
    ClientCallbacks callbacks;
} ClientState;

static ClientState g_client = {0};

//=============================================================================
// 内部函数声明
//=============================================================================
static DWORD WINAPI DiscoveryThreadProc(LPVOID param);
static DWORD WINAPI TcpRecvThreadProc(LPVOID param);
static DWORD WINAPI HeartbeatThreadProc(LPVOID param);
static DWORD WINAPI UdpAudioRecvThreadProc(LPVOID param);
static DWORD WINAPI PlaybackThreadProc(LPVOID param);
static void HandleTcpPacket(const uint8_t* data, int len);

//=============================================================================
// 公共接口
//=============================================================================

bool Client_Init(void) {
    if (g_client.initialized) return true;
    
    memset(&g_client, 0, sizeof(g_client));
    MutexInit(&g_client.servers_mutex);
    MutexInit(&g_client.peers_mutex);
    
    g_client.client_id = (uint32_t)time(NULL) ^ GetCurrentProcessId();
    g_client.ssrc = g_client.client_id;
    g_client.discovery_port = DISCOVERY_PORT;  // 默认发现端口
    strcpy(g_client.name, "User");
    g_client.initialized = true;
    
    LOG_INFO("Client module initialized");
    return true;
}

void Client_Shutdown(void) {
    if (!g_client.initialized) return;
    
    Client_StopDiscovery();
    Client_Disconnect();
    
    MutexDestroy(&g_client.servers_mutex);
    MutexDestroy(&g_client.peers_mutex);
    g_client.initialized = false;
    
    LOG_INFO("Client module shutdown");
}

void Client_SetName(const char* name) {
    if (name) {
        strncpy(g_client.name, name, MAX_NAME_LEN - 1);
    }
}

const char* Client_GetName(void) {
    return g_client.name;
}

void Client_SetCallbacks(const ClientCallbacks* callbacks) {
    if (callbacks) {
        g_client.callbacks = *callbacks;
    }
}

void Client_SetDiscoveryPort(uint16_t port) {
    if (port > 0 && port <= 65535) {
        g_client.discovery_port = port;
        LOG_INFO("Discovery port set to %d", port);
    }
}

bool Client_StartDiscovery(void) {
    if (!g_client.initialized || g_client.discovering) return false;
    
    g_client.udp_discovery = Network_CreateUdpBroadcast(0, false);
    if (g_client.udp_discovery == INVALID_SOCKET) {
        return false;
    }
    
    Network_SetRecvTimeout(g_client.udp_discovery, 500);
    
    g_client.stop_event = EventCreate();
    g_client.discovering = true;
    
    MutexLock(&g_client.servers_mutex);
    g_client.server_count = 0;
    MutexUnlock(&g_client.servers_mutex);
    
    ThreadCreate(&g_client.discovery_thread, DiscoveryThreadProc, NULL);
    
    LOG_INFO("Discovery started");
    return true;
}

void Client_StopDiscovery(void) {
    if (!g_client.discovering) return;
    
    g_client.discovering = false;
    EventSet(g_client.stop_event);
    
    Network_CloseSocket(g_client.udp_discovery);
    
    ThreadJoin(g_client.discovery_thread);
    ThreadClose(g_client.discovery_thread);
    EventDestroy(g_client.stop_event);
    
    g_client.udp_discovery = INVALID_SOCKET;
    
    LOG_INFO("Discovery stopped");
}

int Client_GetServers(ServerInfo* servers, int max_count) {
    int count = 0;
    MutexLock(&g_client.servers_mutex);
    for (int i = 0; i < g_client.server_count && count < max_count; i++) {
        servers[count++] = g_client.servers[i];
    }
    MutexUnlock(&g_client.servers_mutex);
    return count;
}

bool Client_Connect(const char* ip, uint16_t tcp_port, uint16_t audio_udp_port) {
    if (!g_client.initialized || g_client.connected) return false;
    
    LOG_INFO("Connecting to %s (TCP:%d, UDP:%d)", ip, tcp_port, audio_udp_port);
    
    // 快速验证服务器是否可达 (2秒超时)
    if (!Network_TcpQuickTest(ip, tcp_port, 2000)) {
        LOG_ERROR("Server not reachable: %s:%d", ip, tcp_port);
        return false;
    }
    
    // TCP 控制连接
    g_client.tcp_control = Network_TcpConnect(ip, tcp_port);
    if (g_client.tcp_control == INVALID_SOCKET) {
        LOG_ERROR("Failed to connect TCP");
        return false;
    }
    
    // 创建本地 UDP 音频 socket
    g_client.udp_audio = Network_CreateUdpAudio(0, &g_client.local_udp_port);
    if (g_client.udp_audio == INVALID_SOCKET) {
        Network_CloseSocket(g_client.tcp_control);
        LOG_ERROR("Failed to create UDP audio socket");
        return false;
    }
    
    // 保存服务器信息
    strncpy(g_client.server_ip, ip, sizeof(g_client.server_ip) - 1);
    g_client.server_tcp_port = tcp_port;
    g_client.server_udp_port = audio_udp_port;
    Network_MakeAddr(&g_client.server_audio_addr, ip, audio_udp_port);
    
    strncpy(g_client.current_server.ip, ip, sizeof(g_client.current_server.ip) - 1);
    g_client.current_server.tcp_port = tcp_port;
    g_client.current_server.audio_udp_port = audio_udp_port;
    
    // 创建 Opus 解码器 (保留，可选)
    OpusDecoderConfig dec_config;
    OpusCodec_GetDefaultDecoderConfig(&dec_config);
    g_client.opus_decoder = OpusCodec_Create(NULL, &dec_config);
    if (!g_client.opus_decoder) {
        Network_CloseSocket(g_client.tcp_control);
        Network_CloseSocket(g_client.udp_audio);
        LOG_ERROR("Failed to create Opus decoder");
        return false;
    }
    
    // 创建多流 Jitter Buffer (per-SSRC, 最多 MAX_CLIENTS 路)
    g_client.multi_jitter_buffer = MultiStreamJB_Create(MAX_CLIENTS, NULL);
    if (!g_client.multi_jitter_buffer) {
        OpusCodec_Destroy(g_client.opus_decoder);
        Network_CloseSocket(g_client.tcp_control);
        Network_CloseSocket(g_client.udp_audio);
        LOG_ERROR("Failed to create Multi-Stream Jitter Buffer");
        return false;
    }
    
    // 配置解码器工厂
    MultiStreamJB_SetDecoderFactory(g_client.multi_jitter_buffer,
                                     OpusCodec_CreateDecoder,
                                     OpusCodec_DestroyDecoder,
                                     OpusCodec_JitterDecode,
                                     OpusCodec_JitterPlc);
    
    g_client.stop_event = EventCreate();
    g_client.connected = true;
    g_client.recv_len = 0;
    g_client.rtp_sequence = 0;
    g_client.rtp_timestamp = 0;
    
    MutexLock(&g_client.peers_mutex);
    g_client.peer_count = 0;
    MutexUnlock(&g_client.peers_mutex);
    
    // 启动线程
    ThreadCreate(&g_client.tcp_recv_thread, TcpRecvThreadProc, NULL);
    ThreadCreate(&g_client.heartbeat_thread, HeartbeatThreadProc, NULL);
    
    // 发送 HELLO
    HelloRequest req;
    PacketHeader_Init(&req.header, MSG_HELLO, sizeof(HelloRequest) - sizeof(PacketHeader));
    req.client_id = g_client.client_id;
    req.capability_flags = CAP_OPUS | CAP_VAD | CAP_JITTER;
    strncpy(req.client_name, g_client.name, MAX_NAME_LEN);
    
    if (Network_TcpSend(g_client.tcp_control, &req, sizeof(req)) != sizeof(req)) {
        LOG_ERROR("Failed to send HELLO");
        Client_Disconnect();
        return false;
    }
    
    LOG_INFO("Connected to server (local UDP port: %d)", g_client.local_udp_port);
    
    if (g_client.callbacks.onConnected) {
        g_client.callbacks.onConnected(g_client.callbacks.userdata);
    }
    
    return true;
}

void Client_Disconnect(void) {
    if (!g_client.connected) return;
    
    Client_LeaveSession();
    
    g_client.connected = false;
    EventSet(g_client.stop_event);
    
    Network_CloseSocket(g_client.tcp_control);
    Network_CloseSocket(g_client.udp_audio);
    
    ThreadJoin(g_client.tcp_recv_thread);
    ThreadJoin(g_client.heartbeat_thread);
    
    ThreadClose(g_client.tcp_recv_thread);
    ThreadClose(g_client.heartbeat_thread);
    
    EventDestroy(g_client.stop_event);
    
    // 销毁 Opus 解码器
    if (g_client.opus_decoder) {
        OpusCodec_Destroy(g_client.opus_decoder);
        g_client.opus_decoder = NULL;
    }
    
    // 销毁多流 Jitter Buffer
    if (g_client.multi_jitter_buffer) {
        MultiStreamJB_Destroy(g_client.multi_jitter_buffer);
        g_client.multi_jitter_buffer = NULL;
    }
    
    g_client.tcp_control = INVALID_SOCKET;
    g_client.udp_audio = INVALID_SOCKET;
    
    LOG_INFO("Disconnected");
    
    if (g_client.callbacks.onDisconnected) {
        g_client.callbacks.onDisconnected(g_client.callbacks.userdata);
    }
}

bool Client_IsConnected(void) {
    return g_client.connected;
}

bool Client_JoinSession(void) {
    if (!g_client.connected || g_client.in_session) return false;
    
    // 发送 JOIN_SESSION
    JoinSessionRequest req;
    PacketHeader_Init(&req.header, MSG_JOIN_SESSION, sizeof(JoinSessionRequest) - sizeof(PacketHeader));
    req.client_id = g_client.client_id;
    req.local_udp_port = g_client.local_udp_port;
    
    if (Network_TcpSend(g_client.tcp_control, &req, sizeof(req)) != sizeof(req)) {
        LOG_ERROR("Failed to send JOIN_SESSION");
        return false;
    }
    
    g_client.in_session = true;
    
    // 重置多流 Jitter Buffer
    MultiStreamJB_Reset(g_client.multi_jitter_buffer);
    
    // 启动 UDP 音频接收和播放线程
    ThreadCreate(&g_client.udp_audio_thread, UdpAudioRecvThreadProc, NULL);
    ThreadCreate(&g_client.playback_thread, PlaybackThreadProc, NULL);
    
    LOG_INFO("Joined voice session");
    return true;
}

void Client_LeaveSession(void) {
    if (!g_client.in_session) return;
    
    g_client.in_session = false;
    
    // 发送 LEAVE_SESSION
    PacketHeader pkt;
    PacketHeader_Init(&pkt, MSG_LEAVE_SESSION, 0);
    Network_TcpSend(g_client.tcp_control, &pkt, sizeof(pkt));
    
    // 等待音频线程结束
    ThreadJoin(g_client.udp_audio_thread);
    ThreadJoin(g_client.playback_thread);
    ThreadClose(g_client.udp_audio_thread);
    ThreadClose(g_client.playback_thread);
    
    LOG_INFO("Left voice session");
}

bool Client_IsInSession(void) {
    return g_client.in_session;
}

bool Client_GetCurrentServer(ServerInfo* server) {
    if (!g_client.connected || !server) return false;
    *server = g_client.current_server;
    return true;
}

void Client_SendOpusAudio(const uint8_t* opus_data, int opus_len, uint32_t timestamp) {
    if (!g_client.in_session || !opus_data || opus_len <= 0) return;
    
    // 构建 RTP 包
    RtpHeader rtp;
    RtpHeader_Init(&rtp, g_client.ssrc, PAYLOAD_OPUS);
    rtp.sequence = g_client.rtp_sequence++;
    rtp.timestamp = timestamp;
    rtp.payload_len = opus_len;
    RtpHeader_SetVadActive(&rtp, true);
    
    // 发送到服务器
    Network_SendRtpPacket(g_client.udp_audio, &rtp, opus_data, opus_len, 
                           &g_client.server_audio_addr);
}

int Client_GetPeers(PeerInfo* peers, int max_count) {
    int count = 0;
    
    // 如果已连接，首先添加服务器
    if (g_client.connected && count < max_count) {
        peers[count].client_id = g_client.current_server.server_id;
        peers[count].ssrc = 0;  // 服务器 SSRC
        strncpy(peers[count].name, g_client.current_server.name, MAX_NAME_LEN);  // 直接使用服务器名称
        strncpy(peers[count].ip, g_client.server_ip, 15);
        peers[count].ip[15] = '\0';
        peers[count].udp_port = g_client.server_udp_port;
        peers[count].is_talking = false;
        peers[count].is_muted = false;
        peers[count].audio_active = true;
        peers[count].peer_type = PEER_TYPE_SERVER;  // 服务器类型
        count++;
    }
    
    // 然后添加本客户端自己
    if (g_client.connected && count < max_count) {
        peers[count].client_id = g_client.client_id;
        peers[count].ssrc = g_client.ssrc;
        strncpy(peers[count].name, g_client.name, MAX_NAME_LEN);  // 直接使用客户端名称
        strncpy(peers[count].ip, "本机", 15);
        peers[count].ip[15] = '\0';
        peers[count].udp_port = g_client.local_udp_port;
        peers[count].is_talking = false;
        peers[count].is_muted = false;
        peers[count].audio_active = g_client.in_session;
        peers[count].peer_type = PEER_TYPE_SELF;  // 本机类型
        count++;
    }
    
    // 最后添加其他 peer
    MutexLock(&g_client.peers_mutex);
    for (int i = 0; i < g_client.peer_count && count < max_count; i++) {
        // 跳过自己（已经添加了）
        if (g_client.peers[i].client_id == g_client.client_id) continue;
        // 跳过服务器（已经添加了）
        if (g_client.peers[i].client_id == g_client.current_server.server_id) continue;
        peers[count] = g_client.peers[i];
        peers[count].peer_type = PEER_TYPE_CLIENT;  // 其他客户端类型
        count++;
    }
    MutexUnlock(&g_client.peers_mutex);
    return count;
}

void Client_GetJitterStats(float* jitter_ms, float* loss_rate, int* buffer_level) {
    if (!g_client.multi_jitter_buffer) {
        if (jitter_ms) *jitter_ms = 0;
        if (loss_rate) *loss_rate = 0;
        if (buffer_level) *buffer_level = 0;
        return;
    }
    
    JitterStats stats;
    MultiStreamJB_GetStats(g_client.multi_jitter_buffer, &stats);
    
    if (jitter_ms) *jitter_ms = stats.avg_jitter_ms;
    if (loss_rate) *loss_rate = stats.loss_rate;
    if (buffer_level) *buffer_level = MultiStreamJB_GetActiveStreams(g_client.multi_jitter_buffer);
}

uint32_t Client_GetSSRC(void) {
    return g_client.ssrc;
}

//=============================================================================
// 内部函数实现
//=============================================================================

static DWORD WINAPI DiscoveryThreadProc(LPVOID param) {
    LOG_DEBUG("Discovery thread started");
    
    uint8_t buffer[MAX_PACKET_SIZE];
    uint32_t last_broadcast = 0;
    
    while (g_client.discovering) {
        uint32_t now = GetTickCountMs();
        
        // 定期发送广播（使用配置的发现端口）
        if (now - last_broadcast >= DISCOVERY_INTERVAL) {
            // 每次广播前清空服务器列表，重新发现
            MutexLock(&g_client.servers_mutex);
            g_client.server_count = 0;
            MutexUnlock(&g_client.servers_mutex);
            
            DiscoveryRequest req;
            PacketHeader_Init(&req.header, MSG_DISCOVERY_REQUEST, 
                              sizeof(DiscoveryRequest) - sizeof(PacketHeader));
            req.client_id = g_client.client_id;
            req.service_mask = 0;
            strncpy(req.client_name, g_client.name, MAX_NAME_LEN);
            
            Network_UdpBroadcast(g_client.udp_discovery, &req, sizeof(req), g_client.discovery_port);
            last_broadcast = now;
        }
        
        // 接收响应
        SOCKADDR_IN from;
        int len = Network_UdpRecvFrom(g_client.udp_discovery, buffer, sizeof(buffer), &from);
        
        if (len >= (int)sizeof(PacketHeader)) {
            PacketHeader* hdr = (PacketHeader*)buffer;
            
            if (PacketHeader_Validate(hdr) && hdr->msg_type == MSG_DISCOVERY_RESPONSE) {
                DiscoveryResponse* resp = (DiscoveryResponse*)buffer;
                
                // 更新服务器列表
                MutexLock(&g_client.servers_mutex);
                
                int found = -1;
                for (int i = 0; i < g_client.server_count; i++) {
                    if (g_client.servers[i].server_id == resp->server_id) {
                        found = i;
                        break;
                    }
                }
                
                ServerInfo* srv;
                if (found >= 0) {
                    srv = &g_client.servers[found];
                } else if (g_client.server_count < MAX_SERVERS) {
                    srv = &g_client.servers[g_client.server_count++];
                } else {
                    MutexUnlock(&g_client.servers_mutex);
                    continue;
                }
                
                srv->server_id = resp->server_id;
                strncpy(srv->name, resp->server_name, MAX_NAME_LEN);
                strncpy(srv->ip, inet_ntoa(from.sin_addr), sizeof(srv->ip));
                srv->tcp_port = resp->tcp_port;
                srv->audio_udp_port = resp->audio_udp_port;
                srv->capability_flags = resp->capability_flags;
                srv->peer_count = resp->current_peers;
                srv->max_peers = resp->max_peers;
                srv->last_seen = now;
                srv->valid = true;
                
                MutexUnlock(&g_client.servers_mutex);
                
                if (g_client.callbacks.onServerFound && found < 0) {
                    g_client.callbacks.onServerFound(srv, g_client.callbacks.userdata);
                }
                
                LOG_DEBUG("Server found: %s (%s:%d)", srv->name, srv->ip, srv->tcp_port);
            }
        }
    }
    
    LOG_DEBUG("Discovery thread stopped");
    return 0;
}

static DWORD WINAPI TcpRecvThreadProc(LPVOID param) {
    LOG_DEBUG("TCP recv thread started");
    
    Network_SetRecvTimeout(g_client.tcp_control, 100);
    
    while (g_client.connected) {
        int len = recv(g_client.tcp_control, 
                       (char*)g_client.recv_buf + g_client.recv_len,
                       MAX_PACKET_SIZE - g_client.recv_len, 0);
        
        if (len <= 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            
            if (g_client.connected) {
                LOG_ERROR("TCP connection lost");
                g_client.connected = false;
                if (g_client.callbacks.onDisconnected) {
                    g_client.callbacks.onDisconnected(g_client.callbacks.userdata);
                }
            }
            break;
        }
        
        g_client.recv_len += len;
        
        // 处理完整数据包
        while (g_client.recv_len >= (int)sizeof(PacketHeader)) {
            PacketHeader* hdr = (PacketHeader*)g_client.recv_buf;
            if (!PacketHeader_Validate(hdr)) {
                g_client.recv_len = 0;
                break;
            }
            
            int pkt_len = sizeof(PacketHeader) + hdr->payload_len;
            if (g_client.recv_len < pkt_len) break;
            
            HandleTcpPacket(g_client.recv_buf, pkt_len);
            
            memmove(g_client.recv_buf, g_client.recv_buf + pkt_len, g_client.recv_len - pkt_len);
            g_client.recv_len -= pkt_len;
        }
    }
    
    LOG_DEBUG("TCP recv thread stopped");
    return 0;
}

static DWORD WINAPI HeartbeatThreadProc(LPVOID param) {
    LOG_DEBUG("Heartbeat thread started");
    
    while (g_client.connected) {
        Sleep(HEARTBEAT_INTERVAL);
        
        if (!g_client.connected) break;
        
        HeartbeatPacket hb;
        PacketHeader_Init(&hb.header, MSG_HEARTBEAT, sizeof(HeartbeatPacket) - sizeof(PacketHeader));
        hb.client_id = g_client.client_id;
        hb.local_time = GetTickCount64Ms();
        
        Network_TcpSend(g_client.tcp_control, &hb, sizeof(hb));
    }
    
    LOG_DEBUG("Heartbeat thread stopped");
    return 0;
}

static DWORD WINAPI UdpAudioRecvThreadProc(LPVOID param) {
    LOG_DEBUG("UDP audio recv thread started");
    
    uint8_t payload[OPUS_MAX_PACKET];
    RtpHeader rtp;
    
    Network_SetRecvTimeout(g_client.udp_audio, 50);
    
    while (g_client.in_session) {
        SOCKADDR_IN from;
        int payload_len = Network_RecvRtpPacket(g_client.udp_audio, &rtp, payload,
                                                 sizeof(payload), &from);
        
        if (payload_len <= 0) continue;
        
        // 跳过自己的包
        if (rtp.ssrc == g_client.ssrc) continue;
        
        // 放入多流 Jitter Buffer (按 SSRC 自动分流)
        MultiStreamJB_Put(g_client.multi_jitter_buffer, &rtp, payload, payload_len);
    }
    
    LOG_DEBUG("UDP audio recv thread stopped");
    return 0;
}

static DWORD WINAPI PlaybackThreadProc(LPVOID param) {
    LOG_DEBUG("Playback thread started");
    
    int16_t pcm[AUDIO_FRAME_SAMPLES];
    uint32_t cleanup_timer = 0;
    uint32_t playback_frame_count = 0;
    
    while (g_client.in_session) {
        // 从多流 Jitter Buffer 获取混音后的音频
        int samples = MultiStreamJB_GetMixed(g_client.multi_jitter_buffer, pcm, AUDIO_FRAME_SAMPLES);
        
        if (samples > 0) {
            playback_frame_count++;
            
            // 回调
            if (g_client.callbacks.onAudioReceived) {
                g_client.callbacks.onAudioReceived(pcm, samples, g_client.callbacks.userdata);
            }
            
            // 提交到音频播放
            Audio_SubmitPlaybackData(pcm, samples);
        } else {
            // 等待数据
            Sleep(5);
        }
        
        // 定期清理不活跃的流 (每 5 秒检查一次，清理超过 10 秒无数据的流)
        cleanup_timer++;
        if (cleanup_timer >= 1000) {  // 约 5 秒 (5ms * 1000)
            cleanup_timer = 0;
            MultiStreamJB_CleanupInactive(g_client.multi_jitter_buffer, 10000);
        }
    }
    
    LOG_DEBUG("Playback thread stopped");
    return 0;
}

static void HandleTcpPacket(const uint8_t* data, int len) {
    PacketHeader* hdr = (PacketHeader*)data;
    
    switch (hdr->msg_type) {
    case MSG_HELLO_ACK: {
        HelloAck* ack = (HelloAck*)data;
        if (ack->result == 0) {
            g_client.client_id = ack->assigned_id;
            g_client.ssrc = ack->assigned_id;
            g_client.server_udp_port = ack->audio_udp_port;
            Network_MakeAddr(&g_client.server_audio_addr, g_client.server_ip, ack->audio_udp_port);
            LOG_INFO("HELLO_ACK received: id=%u, UDP port=%d", ack->assigned_id, ack->audio_udp_port);
        } else {
            LOG_ERROR("HELLO rejected: result=%u", ack->result);
        }
        break;
    }
    
    case MSG_PEER_LIST: {
        PeerListPacket* list = (PeerListPacket*)data;
        PeerInfo* peers = (PeerInfo*)(data + sizeof(PeerListPacket));
        
        MutexLock(&g_client.peers_mutex);
        g_client.peer_count = MIN(list->peer_count, MAX_CLIENTS);
        for (int i = 0; i < g_client.peer_count; i++) {
            g_client.peers[i] = peers[i];
        }
        MutexUnlock(&g_client.peers_mutex);
        
        LOG_INFO("Peer list received: %d peers", list->peer_count);
        
        if (g_client.callbacks.onPeerListReceived) {
            g_client.callbacks.onPeerListReceived(peers, list->peer_count, g_client.callbacks.userdata);
        }
        break;
    }
    
    case MSG_PEER_JOIN: {
        PeerNotifyPacket* notify = (PeerNotifyPacket*)data;
        
        MutexLock(&g_client.peers_mutex);
        if (g_client.peer_count < MAX_CLIENTS) {
            g_client.peers[g_client.peer_count++] = notify->peer;
        }
        MutexUnlock(&g_client.peers_mutex);
        
        LOG_INFO("Peer joined: %s (id=%u)", notify->peer.name, notify->peer.client_id);
        
        if (g_client.callbacks.onPeerJoined) {
            g_client.callbacks.onPeerJoined(&notify->peer, g_client.callbacks.userdata);
        }
        break;
    }
    
    case MSG_PEER_LEAVE: {
        PeerNotifyPacket* notify = (PeerNotifyPacket*)data;
        uint32_t left_id = notify->peer.client_id;
        
        MutexLock(&g_client.peers_mutex);
        for (int i = 0; i < g_client.peer_count; i++) {
            if (g_client.peers[i].client_id == left_id) {
                memmove(&g_client.peers[i], &g_client.peers[i + 1],
                        (g_client.peer_count - i - 1) * sizeof(PeerInfo));
                g_client.peer_count--;
                break;
            }
        }
        MutexUnlock(&g_client.peers_mutex);
        
        LOG_INFO("Peer left: id=%u", left_id);
        
        if (g_client.callbacks.onPeerLeft) {
            g_client.callbacks.onPeerLeft(left_id, g_client.callbacks.userdata);
        }
        break;
    }
    
    case MSG_HEARTBEAT:
        // 心跳响应
        break;
    
    case MSG_TIME_SYNC: {
        TimeSyncPacket* sync = (TimeSyncPacket*)data;
        // 可用于时钟同步
        LOG_DEBUG("Time sync: server=%llu, base=%llu", sync->server_time, sync->base_timestamp);
        break;
    }
    }
}
