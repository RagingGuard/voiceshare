/**
 * @file server.c
 * @brief 服务器模块实现 (TCP控制 + UDP音频)
 * 
 * 架构:
 * - UDP 发现线程: 响应局域网发现请求
 * - TCP 控制线程: 会话管理、心跳、音频控制
 * - UDP 音频线程: 接收/转发 RTP 音频包
 */

#include "server.h"
#include "network.h"
#include "opus_codec.h"
#include "jitter_buffer.h"

//=============================================================================
// 客户端会话 (TCP 控制)
//=============================================================================
typedef struct {
    uint32_t    client_id;
    uint32_t    ssrc;               // RTP SSRC
    char        name[MAX_NAME_LEN];
    SOCKET      tcp_socket;         // TCP 控制连接
    SOCKADDR_IN tcp_addr;           // TCP 地址
    SOCKADDR_IN udp_addr;           // UDP 音频地址
    uint16_t    udp_port;           // 客户端 UDP 端口
    uint64_t    last_heartbeat;
    bool        active;
    bool        audio_active;       // 音频会话是否激活
    bool        is_talking;
    bool        is_muted;
    
    // TCP 接收缓冲
    uint8_t     recv_buf[MAX_PACKET_SIZE];
    int         recv_len;
} ClientSession;

//=============================================================================
// 服务器状态
//=============================================================================
typedef struct {
    bool            running;
    bool            initialized;
    char            name[MAX_NAME_LEN];
    uint16_t        tcp_port;           // TCP 控制端口
    uint16_t        udp_audio_port;     // UDP 音频端口
    uint16_t        discovery_port;     // UDP 发现端口
    uint32_t        server_id;
    uint32_t        ssrc;               // 服务器 SSRC
    
    // 网络
    SOCKET          udp_discovery;      // UDP 发现
    SOCKET          tcp_control;        // TCP 控制监听
    SOCKET          udp_audio;          // UDP 音频
    
    // 线程
    Thread          discovery_thread;
    Thread          tcp_accept_thread;
    Thread          tcp_recv_thread;
    Thread          udp_audio_thread;
    Event           stop_event;
    
    // 客户端管理
    ClientSession   clients[MAX_CLIENTS];
    int             client_count;
    Mutex           clients_mutex;
    
    // RTP 序列号
    uint16_t        rtp_sequence;
    uint32_t        rtp_timestamp;
    
    // 回调
    ServerCallbacks callbacks;
    
    // Opus 解码器 (用于接收音频)
    OpusCodec*      opus_decoder;
} ServerState;

static ServerState g_server = {0};

//=============================================================================
// 内部函数声明
//=============================================================================
static DWORD WINAPI DiscoveryThreadProc(LPVOID param);
static DWORD WINAPI TcpAcceptThreadProc(LPVOID param);
static DWORD WINAPI TcpRecvThreadProc(LPVOID param);
static DWORD WINAPI UdpAudioThreadProc(LPVOID param);
static void HandleTcpPacket(ClientSession* client, const uint8_t* data, int len);
static void BroadcastTcpMessage(const void* data, int len, uint32_t exclude_id);
static void BroadcastUdpAudio(const RtpHeader* rtp, const uint8_t* payload, 
                               uint16_t payload_len, uint32_t exclude_ssrc);
static void NotifyPeerJoin(const PeerInfo* peer);
static void NotifyPeerLeave(uint32_t client_id);
static void RemoveClient(int index);
static ClientSession* FindClientBySSRC(uint32_t ssrc);

//=============================================================================
// 公共接口
//=============================================================================

bool Server_Init(void) {
    if (g_server.initialized) return true;
    
    memset(&g_server, 0, sizeof(g_server));
    MutexInit(&g_server.clients_mutex);
    g_server.server_id = (uint32_t)time(NULL) ^ GetCurrentProcessId();
    g_server.ssrc = g_server.server_id;  // 服务器 SSRC
    g_server.initialized = true;
    
    LOG_INFO("Server module initialized");
    return true;
}

void Server_Shutdown(void) {
    if (!g_server.initialized) return;
    
    Server_Stop();
    
    MutexDestroy(&g_server.clients_mutex);
    g_server.initialized = false;
    
    LOG_INFO("Server module shutdown");
}

bool Server_Start(const char* name, uint16_t tcp_port, uint16_t udp_port, 
                  uint16_t discovery_port, const ServerCallbacks* callbacks) {
    if (!g_server.initialized || g_server.running) return false;
    
    strncpy(g_server.name, name, MAX_NAME_LEN - 1);
    g_server.tcp_port = tcp_port;
    g_server.udp_audio_port = udp_port;
    g_server.discovery_port = discovery_port;
    if (callbacks) {
        g_server.callbacks = *callbacks;
    }
    
    // 创建 UDP 发现 socket（使用自定义端口）
    g_server.udp_discovery = Network_CreateUdpBroadcast(discovery_port, true);
    if (g_server.udp_discovery == INVALID_SOCKET) {
        LOG_ERROR("Failed to create UDP discovery socket on port %d", discovery_port);
        return false;
    }
    
    // 创建 TCP 控制 socket
    g_server.tcp_control = Network_CreateTcpListener(tcp_port);
    if (g_server.tcp_control == INVALID_SOCKET) {
        Network_CloseSocket(g_server.udp_discovery);
        LOG_ERROR("Failed to create TCP control socket");
        return false;
    }
    
    // 创建 UDP 音频 socket
    g_server.udp_audio = Network_CreateUdpAudio(udp_port, &g_server.udp_audio_port);
    if (g_server.udp_audio == INVALID_SOCKET) {
        Network_CloseSocket(g_server.udp_discovery);
        Network_CloseSocket(g_server.tcp_control);
        LOG_ERROR("Failed to create UDP audio socket");
        return false;
    }
    
    // 创建 Opus 解码器
    OpusDecoderConfig dec_config;
    OpusCodec_GetDefaultDecoderConfig(&dec_config);
    g_server.opus_decoder = OpusCodec_Create(NULL, &dec_config);
    
    // 创建停止事件
    g_server.stop_event = EventCreate();
    
    g_server.running = true;
    g_server.rtp_sequence = 0;
    g_server.rtp_timestamp = 0;
    
    // 启动线程
    ThreadCreate(&g_server.discovery_thread, DiscoveryThreadProc, NULL);
    ThreadCreate(&g_server.tcp_accept_thread, TcpAcceptThreadProc, NULL);
    ThreadCreate(&g_server.tcp_recv_thread, TcpRecvThreadProc, NULL);
    ThreadCreate(&g_server.udp_audio_thread, UdpAudioThreadProc, NULL);
    
    LOG_INFO("Server started: %s (TCP:%d, UDP Audio:%d, Discovery:%d)", 
             name, tcp_port, g_server.udp_audio_port, g_server.discovery_port);
    
    if (g_server.callbacks.onStarted) {
        g_server.callbacks.onStarted(g_server.callbacks.userdata);
    }
    
    return true;
}

void Server_Stop(void) {
    if (!g_server.running) return;
    
    g_server.running = false;
    EventSet(g_server.stop_event);
    
    // 关闭 socket 使阻塞的线程退出
    Network_CloseSocket(g_server.udp_discovery);
    Network_CloseSocket(g_server.tcp_control);
    Network_CloseSocket(g_server.udp_audio);
    
    // 关闭所有客户端连接
    MutexLock(&g_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_server.clients[i].active) {
            Network_CloseSocket(g_server.clients[i].tcp_socket);
            g_server.clients[i].active = false;
        }
    }
    g_server.client_count = 0;
    MutexUnlock(&g_server.clients_mutex);
    
    // 等待线程结束
    ThreadJoin(g_server.discovery_thread);
    ThreadJoin(g_server.tcp_accept_thread);
    ThreadJoin(g_server.tcp_recv_thread);
    ThreadJoin(g_server.udp_audio_thread);
    
    ThreadClose(g_server.discovery_thread);
    ThreadClose(g_server.tcp_accept_thread);
    ThreadClose(g_server.tcp_recv_thread);
    ThreadClose(g_server.udp_audio_thread);
    
    EventDestroy(g_server.stop_event);
    
    // 销毁 Opus 解码器
    if (g_server.opus_decoder) {
        OpusCodec_Destroy(g_server.opus_decoder);
        g_server.opus_decoder = NULL;
    }
    
    g_server.udp_discovery = INVALID_SOCKET;
    g_server.tcp_control = INVALID_SOCKET;
    g_server.udp_audio = INVALID_SOCKET;
    
    LOG_INFO("Server stopped");
    
    if (g_server.callbacks.onStopped) {
        g_server.callbacks.onStopped(g_server.callbacks.userdata);
    }
}

bool Server_IsRunning(void) {
    return g_server.running;
}

const char* Server_GetName(void) {
    return g_server.name;
}

uint16_t Server_GetTcpPort(void) {
    return g_server.tcp_port;
}

uint16_t Server_GetUdpPort(void) {
    return g_server.udp_audio_port;
}

int Server_GetClientCount(void) {
    return g_server.client_count;
}

int Server_GetClients(PeerInfo* peers, int max_count) {
    int count = 0;
    MutexLock(&g_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS && count < max_count; i++) {
        if (g_server.clients[i].active) {
            peers[count].client_id = g_server.clients[i].client_id;
            peers[count].ssrc = g_server.clients[i].ssrc;
            strncpy(peers[count].name, g_server.clients[i].name, MAX_NAME_LEN);
            // 填充 IP 地址
            strncpy(peers[count].ip, inet_ntoa(g_server.clients[i].tcp_addr.sin_addr), 15);
            peers[count].ip[15] = '\0';
            // 填充 UDP 端口
            peers[count].udp_port = g_server.clients[i].udp_port;
            peers[count].is_talking = g_server.clients[i].is_talking;
            peers[count].is_muted = g_server.clients[i].is_muted;
            peers[count].audio_active = g_server.clients[i].audio_active;
            count++;
        }
    }
    MutexUnlock(&g_server.clients_mutex);
    return count;
}

void Server_SendOpusAudio(const uint8_t* opus_data, int opus_len, uint32_t timestamp) {
    if (!g_server.running || !opus_data || opus_len <= 0) return;
    
    // 构建 RTP 包头
    RtpHeader rtp;
    RtpHeader_Init(&rtp, g_server.ssrc, PAYLOAD_OPUS);
    rtp.sequence = g_server.rtp_sequence++;
    rtp.timestamp = timestamp;
    rtp.payload_len = opus_len;
    RtpHeader_SetVadActive(&rtp, true);
    
    // 发送给所有客户端
    MutexLock(&g_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_server.clients[i].active && g_server.clients[i].audio_active) {
            Network_SendRtpPacket(g_server.udp_audio, &rtp, opus_data, opus_len,
                                   &g_server.clients[i].udp_addr);
        }
    }
    MutexUnlock(&g_server.clients_mutex);
}

void Server_BroadcastAudioControl(uint8_t action, uint8_t muted) {
    AudioControlPacket pkt;
    PacketHeader_Init(&pkt.header, action ? MSG_AUDIO_START : MSG_AUDIO_STOP,
                      sizeof(AudioControlPacket) - sizeof(PacketHeader));
    pkt.client_id = 0;  // 服务器
    pkt.action = action;
    pkt.muted = muted;
    
    BroadcastTcpMessage(&pkt, sizeof(pkt), 0);
}

//=============================================================================
// 内部函数实现
//=============================================================================

static DWORD WINAPI DiscoveryThreadProc(LPVOID param) {
    LOG_DEBUG("Discovery thread started");
    
    uint8_t buffer[MAX_PACKET_SIZE];
    
    while (g_server.running) {
        SOCKADDR_IN from;
        int len = Network_UdpRecvFrom(g_server.udp_discovery, buffer, sizeof(buffer), &from);
        
        if (len < (int)sizeof(PacketHeader)) continue;
        
        PacketHeader* hdr = (PacketHeader*)buffer;
        if (!PacketHeader_Validate(hdr)) continue;
        
        if (hdr->msg_type == MSG_DISCOVERY_REQUEST) {
            // 构建响应
            DiscoveryResponse resp;
            PacketHeader_Init(&resp.header, MSG_DISCOVERY_RESPONSE,
                              sizeof(DiscoveryResponse) - sizeof(PacketHeader));
            resp.server_id = g_server.server_id;
            resp.tcp_port = g_server.tcp_port;
            resp.audio_udp_port = g_server.udp_audio_port;
            resp.capability_flags = CAP_OPUS | CAP_VAD | CAP_JITTER;
            resp.current_peers = (uint8_t)g_server.client_count;
            resp.max_peers = MAX_CLIENTS;
            strncpy(resp.server_name, g_server.name, MAX_NAME_LEN);
            strncpy(resp.version_str, APP_VERSION, sizeof(resp.version_str));
            
            // 单播回复
            Network_UdpSendTo(g_server.udp_discovery, &resp, sizeof(resp), &from);
            
            LOG_DEBUG("Discovery response sent to %s", inet_ntoa(from.sin_addr));
        }
    }
    
    LOG_DEBUG("Discovery thread stopped");
    return 0;
}

static DWORD WINAPI TcpAcceptThreadProc(LPVOID param) {
    LOG_DEBUG("TCP accept thread started");
    
    while (g_server.running) {
        SOCKADDR_IN client_addr;
        int addr_len = sizeof(client_addr);
        
        SOCKET client_socket = accept(g_server.tcp_control, (SOCKADDR*)&client_addr, &addr_len);
        if (client_socket == INVALID_SOCKET) {
            if (g_server.running) {
                LOG_WARN("Accept failed: %d", WSAGetLastError());
            }
            continue;
        }
        
        // 禁用 Nagle
        BOOL nodelay = TRUE;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
        
        // 查找空闲槽位
        MutexLock(&g_server.clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_server.clients[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot < 0) {
            MutexUnlock(&g_server.clients_mutex);
            LOG_WARN("Server full, rejecting connection");
            Network_CloseSocket(client_socket);
            continue;
        }
        
        // 初始化客户端会话
        ClientSession* session = &g_server.clients[slot];
        memset(session, 0, sizeof(ClientSession));
        session->tcp_socket = client_socket;
        session->tcp_addr = client_addr;
        session->last_heartbeat = GetTickCount64Ms();
        session->active = true;
        g_server.client_count++;
        
        MutexUnlock(&g_server.clients_mutex);
        
        LOG_INFO("TCP connection from %s:%d (slot %d)",
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), slot);
    }
    
    LOG_DEBUG("TCP accept thread stopped");
    return 0;
}

static DWORD WINAPI TcpRecvThreadProc(LPVOID param) {
    LOG_DEBUG("TCP recv thread started");
    
    while (g_server.running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        
        SOCKET max_fd = 0;
        
        MutexLock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_server.clients[i].active) {
                FD_SET(g_server.clients[i].tcp_socket, &read_fds);
                if (g_server.clients[i].tcp_socket > max_fd) {
                    max_fd = g_server.clients[i].tcp_socket;
                }
            }
        }
        MutexUnlock(&g_server.clients_mutex);
        
        if (max_fd == 0) {
            Sleep(10);
            continue;
        }
        
        struct timeval tv = { 0, 100000 };  // 100ms
        int ret = select((int)max_fd + 1, &read_fds, NULL, NULL, &tv);
        
        if (ret <= 0) continue;
        
        MutexLock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            ClientSession* client = &g_server.clients[i];
            if (!client->active) continue;
            
            if (FD_ISSET(client->tcp_socket, &read_fds)) {
                int len = recv(client->tcp_socket, 
                               (char*)client->recv_buf + client->recv_len,
                               MAX_PACKET_SIZE - client->recv_len, 0);
                
                if (len <= 0) {
                    uint32_t client_id = client->client_id;
                    RemoveClient(i);
                    MutexUnlock(&g_server.clients_mutex);
                    
                    NotifyPeerLeave(client_id);
                    if (g_server.callbacks.onClientLeft) {
                        g_server.callbacks.onClientLeft(client_id, g_server.callbacks.userdata);
                    }
                    
                    MutexLock(&g_server.clients_mutex);
                    continue;
                }
                
                client->recv_len += len;
                client->last_heartbeat = GetTickCount64Ms();
                
                // 处理完整数据包
                while (client->recv_len >= (int)sizeof(PacketHeader)) {
                    PacketHeader* hdr = (PacketHeader*)client->recv_buf;
                    if (!PacketHeader_Validate(hdr)) {
                        client->recv_len = 0;
                        break;
                    }
                    
                    int pkt_len = sizeof(PacketHeader) + hdr->payload_len;
                    if (client->recv_len < pkt_len) break;
                    
                    HandleTcpPacket(client, client->recv_buf, pkt_len);
                    
                    memmove(client->recv_buf, client->recv_buf + pkt_len, client->recv_len - pkt_len);
                    client->recv_len -= pkt_len;
                }
            }
        }
        MutexUnlock(&g_server.clients_mutex);
        
        // 心跳超时检查
        uint64_t now = GetTickCount64Ms();
        MutexLock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_server.clients[i].active) {
                if (now - g_server.clients[i].last_heartbeat > HEARTBEAT_TIMEOUT) {
                    LOG_WARN("Client %u timeout", g_server.clients[i].client_id);
                    uint32_t client_id = g_server.clients[i].client_id;
                    RemoveClient(i);
                    MutexUnlock(&g_server.clients_mutex);
                    
                    NotifyPeerLeave(client_id);
                    if (g_server.callbacks.onClientLeft) {
                        g_server.callbacks.onClientLeft(client_id, g_server.callbacks.userdata);
                    }
                    
                    MutexLock(&g_server.clients_mutex);
                }
            }
        }
        MutexUnlock(&g_server.clients_mutex);
    }
    
    LOG_DEBUG("TCP recv thread stopped");
    return 0;
}

static DWORD WINAPI UdpAudioThreadProc(LPVOID param) {
    LOG_DEBUG("UDP audio thread started");
    
    uint8_t payload[OPUS_MAX_PACKET];
    RtpHeader rtp;
    int16_t pcm[AUDIO_FRAME_SAMPLES];
    
    Network_SetRecvTimeout(g_server.udp_audio, 100);
    
    while (g_server.running) {
        SOCKADDR_IN from;
        int payload_len = Network_RecvRtpPacket(g_server.udp_audio, &rtp, payload, 
                                                 sizeof(payload), &from);
        
        if (payload_len < 0) continue;
        
        // 查找发送者
        ClientSession* sender = FindClientBySSRC(rtp.ssrc);
        if (sender) {
            sender->is_talking = RtpHeader_GetVadActive(&rtp);
        }
        
        // 解码 (用于本地监听或回调)
        if (g_server.opus_decoder && g_server.callbacks.onAudioReceived) {
            int samples = OpusCodec_Decode(g_server.opus_decoder, payload, payload_len,
                                            pcm, AUDIO_FRAME_SAMPLES, 0);
            if (samples > 0 && sender) {
                g_server.callbacks.onAudioReceived(sender->client_id, pcm, samples,
                                                    g_server.callbacks.userdata);
            }
        }
        
        // 转发给其他客户端
        BroadcastUdpAudio(&rtp, payload, payload_len, rtp.ssrc);
    }
    
    LOG_DEBUG("UDP audio thread stopped");
    return 0;
}

static void HandleTcpPacket(ClientSession* client, const uint8_t* data, int len) {
    PacketHeader* hdr = (PacketHeader*)data;
    
    switch (hdr->msg_type) {
    case MSG_HELLO: {
        HelloRequest* req = (HelloRequest*)data;
        client->client_id = req->client_id ? req->client_id : 
                            (uint32_t)time(NULL) ^ (uint32_t)(intptr_t)client;
        client->ssrc = client->client_id;  // SSRC = client_id
        strncpy(client->name, req->client_name, MAX_NAME_LEN - 1);
        
        // 发送 HELLO_ACK
        HelloAck ack;
        PacketHeader_Init(&ack.header, MSG_HELLO_ACK, sizeof(HelloAck) - sizeof(PacketHeader));
        ack.result = 0;
        ack.assigned_id = client->client_id;
        ack.audio_udp_port = g_server.udp_audio_port;
        ack.server_time = GetTickCount64Ms();
        Network_TcpSend(client->tcp_socket, &ack, sizeof(ack));
        
        LOG_INFO("Client HELLO: %s (id=%u, ssrc=%u)", client->name, client->client_id, client->ssrc);
        break;
    }
    
    case MSG_JOIN_SESSION: {
        JoinSessionRequest* req = (JoinSessionRequest*)data;
        client->udp_port = req->local_udp_port;
        
        // 设置客户端 UDP 地址 (IP 来自 TCP 连接, 端口来自请求)
        client->udp_addr = client->tcp_addr;
        client->udp_addr.sin_port = htons(client->udp_port);
        client->audio_active = true;
        
        // 发送 JOIN_SESSION_ACK
        JoinSessionAck ack;
        PacketHeader_Init(&ack.header, MSG_JOIN_SESSION + 1, sizeof(JoinSessionAck) - sizeof(PacketHeader));
        ack.result = 0;
        ack.ssrc = client->ssrc;
        ack.base_timestamp = GetTickCount64Ms() * (AUDIO_SAMPLE_RATE / 1000);
        Network_TcpSend(client->tcp_socket, &ack, sizeof(ack));
        
        // 发送用户列表
        uint8_t list_buf[MAX_PACKET_SIZE];
        PeerListPacket* list = (PeerListPacket*)list_buf;
        PeerInfo* peers = (PeerInfo*)(list_buf + sizeof(PeerListPacket));
        int count = 0;
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_server.clients[i].active && g_server.clients[i].client_id != client->client_id) {
                peers[count].client_id = g_server.clients[i].client_id;
                peers[count].ssrc = g_server.clients[i].ssrc;
                strncpy(peers[count].name, g_server.clients[i].name, MAX_NAME_LEN);
                peers[count].is_talking = g_server.clients[i].is_talking;
                peers[count].is_muted = g_server.clients[i].is_muted;
                peers[count].audio_active = g_server.clients[i].audio_active;
                count++;
            }
        }
        
        PacketHeader_Init(&list->header, MSG_PEER_LIST,
                          sizeof(PeerListPacket) - sizeof(PacketHeader) + count * sizeof(PeerInfo));
        list->peer_count = count;
        Network_TcpSend(client->tcp_socket, list_buf, sizeof(PeerListPacket) + count * sizeof(PeerInfo));
        
        LOG_INFO("Client joined session: %s (UDP port %d)", client->name, client->udp_port);
        
        // 通知其他客户端
        PeerInfo peer = {0};
        peer.client_id = client->client_id;
        peer.ssrc = client->ssrc;
        strncpy(peer.name, client->name, MAX_NAME_LEN);
        peer.audio_active = true;
        NotifyPeerJoin(&peer);
        
        if (g_server.callbacks.onClientJoined) {
            g_server.callbacks.onClientJoined(client->client_id, client->name, g_server.callbacks.userdata);
        }
        break;
    }
    
    case MSG_LEAVE_SESSION:
        client->audio_active = false;
        LOG_INFO("Client left session: %s", client->name);
        break;
    
    case MSG_HEARTBEAT: {
        // 回复心跳
        HeartbeatPacket resp;
        PacketHeader_Init(&resp.header, MSG_HEARTBEAT, sizeof(HeartbeatPacket) - sizeof(PacketHeader));
        resp.client_id = client->client_id;
        resp.local_time = GetTickCount64Ms();
        Network_TcpSend(client->tcp_socket, &resp, sizeof(resp));
        break;
    }
    
    case MSG_AUDIO_START:
        client->audio_active = true;
        LOG_DEBUG("Client %s audio started", client->name);
        break;
    
    case MSG_AUDIO_STOP:
        client->audio_active = false;
        client->is_talking = false;
        LOG_DEBUG("Client %s audio stopped", client->name);
        break;
    
    case MSG_AUDIO_MUTE:
        client->is_muted = true;
        break;
    
    case MSG_AUDIO_UNMUTE:
        client->is_muted = false;
        break;
    }
}

static void BroadcastTcpMessage(const void* data, int len, uint32_t exclude_id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_server.clients[i].active && g_server.clients[i].client_id != exclude_id) {
            Network_TcpSend(g_server.clients[i].tcp_socket, data, len);
        }
    }
}

static void BroadcastUdpAudio(const RtpHeader* rtp, const uint8_t* payload, 
                               uint16_t payload_len, uint32_t exclude_ssrc) {
    MutexLock(&g_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ClientSession* c = &g_server.clients[i];
        if (c->active && c->audio_active && c->ssrc != exclude_ssrc) {
            Network_SendRtpPacket(g_server.udp_audio, rtp, payload, payload_len, &c->udp_addr);
        }
    }
    MutexUnlock(&g_server.clients_mutex);
}

static void NotifyPeerJoin(const PeerInfo* peer) {
    PeerNotifyPacket pkt;
    PacketHeader_Init(&pkt.header, MSG_PEER_JOIN, sizeof(PeerNotifyPacket) - sizeof(PacketHeader));
    pkt.peer = *peer;
    BroadcastTcpMessage(&pkt, sizeof(pkt), peer->client_id);
}

static void NotifyPeerLeave(uint32_t client_id) {
    PeerNotifyPacket pkt;
    PacketHeader_Init(&pkt.header, MSG_PEER_LEAVE, sizeof(PeerNotifyPacket) - sizeof(PacketHeader));
    pkt.peer.client_id = client_id;
    BroadcastTcpMessage(&pkt, sizeof(pkt), client_id);
}

static void RemoveClient(int index) {
    ClientSession* client = &g_server.clients[index];
    if (client->active) {
        Network_CloseSocket(client->tcp_socket);
        client->active = false;
        g_server.client_count--;
        LOG_INFO("Client removed: %s (id=%u)", client->name, client->client_id);
    }
}

static ClientSession* FindClientBySSRC(uint32_t ssrc) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_server.clients[i].active && g_server.clients[i].ssrc == ssrc) {
            return &g_server.clients[i];
        }
    }
    return NULL;
}
