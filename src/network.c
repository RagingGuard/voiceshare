/**
 * @file network.c
 * @brief 网络层实现 (TCP控制 + UDP音频)
 */

#include "network.h"

static bool g_wsa_initialized = false;

bool Network_Init(void) {
    if (g_wsa_initialized) return true;
    
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_ERROR("WSAStartup failed: %d", WSAGetLastError());
        return false;
    }
    
    g_wsa_initialized = true;
    LOG_INFO("Network initialized (TCP control + UDP audio)");
    return true;
}

void Network_Shutdown(void) {
    if (g_wsa_initialized) {
        WSACleanup();
        g_wsa_initialized = false;
        LOG_INFO("Network shutdown");
    }
}

SOCKET Network_CreateUdpBroadcast(uint16_t port, bool bind_port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create UDP socket: %d", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    // 启用广播
    BOOL enable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&enable, sizeof(enable));
    
    // 允许地址重用
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
    
    if (bind_port) {
        SOCKADDR_IN addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("Failed to bind UDP port %d: %d", port, WSAGetLastError());
            closesocket(sock);
            return INVALID_SOCKET;
        }
    }
    
    return sock;
}

SOCKET Network_CreateUdpAudio(uint16_t port, uint16_t* out_port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create UDP audio socket: %d", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    // 允许地址重用
    BOOL enable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
    
    // 设置接收缓冲区 (减少丢包)
    int rcvbuf = 256 * 1024;  // 256KB
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));
    
    // 设置发送缓冲区
    int sndbuf = 128 * 1024;  // 128KB
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));
    
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind UDP audio port %d: %d", port, WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    // 获取实际绑定的端口
    if (out_port) {
        int addrlen = sizeof(addr);
        getsockname(sock, (SOCKADDR*)&addr, &addrlen);
        *out_port = ntohs(addr.sin_port);
    }
    
    LOG_INFO("UDP audio socket created on port %d", out_port ? *out_port : port);
    return sock;
}

SOCKET Network_CreateTcpListener(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create TCP socket: %d", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    // 允许地址重用
    BOOL enable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
    
    // 禁用 Nagle 算法
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&enable, sizeof(enable));
    
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind TCP port %d: %d", port, WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("Failed to listen on port %d: %d", port, WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    return sock;
}

SOCKET Network_TcpConnect(const char* ip, uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create TCP socket: %d", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    // 禁用 Nagle 算法
    BOOL enable = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&enable, sizeof(enable));
    
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
    
    if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to connect to %s:%d: %d", ip, port, WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    return sock;
}

int Network_UdpBroadcast(SOCKET sock, const void* data, int len, uint16_t port) {
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_BROADCAST;
    addr.sin_port = htons(port);
    
    return sendto(sock, (const char*)data, len, 0, (SOCKADDR*)&addr, sizeof(addr));
}

int Network_UdpSendTo(SOCKET sock, const void* data, int len, const SOCKADDR_IN* addr) {
    return sendto(sock, (const char*)data, len, 0, (const SOCKADDR*)addr, sizeof(*addr));
}

int Network_UdpRecvFrom(SOCKET sock, void* buf, int len, SOCKADDR_IN* from) {
    int fromLen = sizeof(SOCKADDR_IN);
    return recvfrom(sock, (char*)buf, len, 0, (SOCKADDR*)from, &fromLen);
}

int Network_TcpSend(SOCKET sock, const void* data, int len) {
    int sent = 0;
    const char* ptr = (const char*)data;
    
    while (sent < len) {
        int n = send(sock, ptr + sent, len - sent, 0);
        if (n <= 0) {
            if (n == 0) return sent;
            return SOCKET_ERROR;
        }
        sent += n;
    }
    
    return sent;
}

int Network_TcpRecv(SOCKET sock, void* buf, int len) {
    int received = 0;
    char* ptr = (char*)buf;
    
    while (received < len) {
        int n = recv(sock, ptr + received, len - received, 0);
        if (n <= 0) {
            if (n == 0) return received;
            return SOCKET_ERROR;
        }
        received += n;
    }
    
    return received;
}

int Network_TcpRecvPacket(SOCKET sock, void* buf, int max_len) {
    // 先读取头部
    PacketHeader hdr;
    int n = Network_TcpRecv(sock, &hdr, sizeof(hdr));
    if (n != sizeof(hdr)) {
        return -1;
    }
    
    // 验证头部
    if (!PacketHeader_Validate(&hdr)) {
        LOG_WARN("Invalid packet header");
        return -1;
    }
    
    int total = sizeof(hdr) + hdr.payload_len;
    if (total > max_len) {
        LOG_WARN("Packet too large: %d > %d", total, max_len);
        return -1;
    }
    
    // 复制头部
    memcpy(buf, &hdr, sizeof(hdr));
    
    // 读取负载
    if (hdr.payload_len > 0) {
        n = Network_TcpRecv(sock, (char*)buf + sizeof(hdr), hdr.payload_len);
        if (n != (int)hdr.payload_len) {
            return -1;
        }
    }
    
    return total;
}

bool Network_SetNonBlocking(SOCKET sock, bool nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

bool Network_SetRecvTimeout(SOCKET sock, int timeout_ms) {
    DWORD timeout = (DWORD)timeout_ms;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == 0;
}

void Network_CloseSocket(SOCKET sock) {
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
    }
}

bool Network_GetLocalIP(char* ip, int len) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return false;
    }
    
    struct hostent* host = gethostbyname(hostname);
    if (!host || !host->h_addr_list[0]) {
        return false;
    }
    
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(addr));
    strncpy(ip, inet_ntoa(addr), len - 1);
    ip[len - 1] = '\0';
    
    return true;
}

void Network_MakeAddr(SOCKADDR_IN* addr, const char* ip, uint16_t port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = inet_addr(ip);
    addr->sin_port = htons(port);
}

//=============================================================================
// RTP 音频包收发
//=============================================================================

int Network_SendRtpPacket(SOCKET sock, const RtpHeader* rtp, const uint8_t* payload,
                           uint16_t payload_len, const SOCKADDR_IN* addr) {
    // 构造完整 RTP 包
    uint8_t packet[sizeof(RtpHeader) + OPUS_MAX_PACKET];
    
    // 复制头部
    memcpy(packet, rtp, sizeof(RtpHeader));
    
    // 复制负载
    if (payload && payload_len > 0) {
        memcpy(packet + sizeof(RtpHeader), payload, payload_len);
    }
    
    int total = sizeof(RtpHeader) + payload_len;
    
    return sendto(sock, (const char*)packet, total, 0, (const SOCKADDR*)addr, sizeof(*addr));
}

int Network_RecvRtpPacket(SOCKET sock, RtpHeader* rtp, uint8_t* payload,
                           uint16_t max_len, SOCKADDR_IN* from) {
    uint8_t packet[sizeof(RtpHeader) + OPUS_MAX_PACKET];
    int fromLen = sizeof(SOCKADDR_IN);
    
    int n = recvfrom(sock, (char*)packet, sizeof(packet), 0, (SOCKADDR*)from, &fromLen);
    if (n < (int)sizeof(RtpHeader)) {
        return -1;  // 包太小
    }
    
    // 解析头部
    memcpy(rtp, packet, sizeof(RtpHeader));
    
    // 验证版本
    if (rtp->version != 2) {
        return -2;  // 版本错误
    }
    
    // 提取负载
    int payload_len = n - sizeof(RtpHeader);
    if (payload_len > 0 && payload && payload_len <= max_len) {
        memcpy(payload, packet + sizeof(RtpHeader), payload_len);
    }
    
    return payload_len;
}
