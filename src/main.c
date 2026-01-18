/**
 * @file main.c
 * @brief Main entry point (TCP control + UDP audio + Opus)
 */

#include "common.h"
#include "network.h"
#include "audio.h"
#include "server.h"
#include "client.h"
#include "gui.h"
#include "opus_codec.h"
#include "opus_dynamic.h"

static bool g_isServerMode = false;
static bool g_running = true;
static OpusCodec* g_opusEncoder = NULL;
static uint32_t g_rtpTimestamp = 0;

static void OnAudioCapture(const int16_t* samples, int count, void* userdata) {
    if (!g_opusEncoder) return;
    uint8_t opus_data[OPUS_MAX_PACKET];
    int opus_len = OpusCodec_Encode(g_opusEncoder, samples, count, opus_data, sizeof(opus_data));
    if (opus_len > 0) {
        if (g_isServerMode) {
            Server_SendOpusAudio(opus_data, opus_len, g_rtpTimestamp);
        } else {
            Client_SendOpusAudio(opus_data, opus_len, g_rtpTimestamp);
        }
        g_rtpTimestamp += count;
    }
}

static void OnServerStarted(void* userdata) {
    Gui_SetServerRunning(true);
    Gui_AddLog("Server started");
}

static void OnServerStopped(void* userdata) {
    Gui_SetServerRunning(false);
    Gui_AddLog("Server stopped");
}

static void OnClientJoined(uint32_t client_id, const char* name, void* userdata) {
    char msg[128];
    snprintf(msg, sizeof(msg), "User joined: %s", name);
    Gui_AddLog(msg);
    PeerInfo peers[MAX_CLIENTS];
    int count = Server_GetClients(peers, MAX_CLIENTS);
    Gui_UpdatePeerList(peers, count);
}

static void OnClientLeft(uint32_t client_id, void* userdata) {
    Gui_AddLog("User left");
    PeerInfo peers[MAX_CLIENTS];
    int count = Server_GetClients(peers, MAX_CLIENTS);
    Gui_UpdatePeerList(peers, count);
}

static void OnServerError(const char* msg, void* userdata) {
    Gui_ShowError(msg);
}

static void OnConnected(void* userdata) {
    ServerInfo server;
    if (Client_GetCurrentServer(&server)) {
        char info[128];
        snprintf(info, sizeof(info), "%s (%s TCP:%d UDP:%d)", 
                 server.name, server.ip, server.tcp_port, server.audio_udp_port);
        Gui_SetConnected(true, info);
    } else {
        Gui_SetConnected(true, NULL);
    }
    Gui_AddLog("Connected to server (TCP control)");
}

static void OnDisconnected(void* userdata) {
    Gui_SetConnected(false, NULL);
    Gui_AddLog("Disconnected");
    Gui_UpdatePeerList(NULL, 0);
}

static void OnServerFound(const ServerInfo* server, void* userdata) {
    ServerInfo servers[MAX_SERVERS];
    int count = Client_GetServers(servers, MAX_SERVERS);
    Gui_UpdateServerList(servers, count);
}

static void OnPeerJoined(const PeerInfo* peer, void* userdata) {
    char msg[128];
    snprintf(msg, sizeof(msg), "User joined: %s", peer->name);
    Gui_AddLog(msg);
    PeerInfo peers[MAX_CLIENTS];
    int count = Client_GetPeers(peers, MAX_CLIENTS);
    Gui_UpdatePeerList(peers, count);
}

static void OnPeerLeft(uint32_t client_id, void* userdata) {
    Gui_AddLog("User left");
    PeerInfo peers[MAX_CLIENTS];
    int count = Client_GetPeers(peers, MAX_CLIENTS);
    Gui_UpdatePeerList(peers, count);
}

static void OnPeerListReceived(const PeerInfo* peers, int count, void* userdata) {
    Gui_UpdatePeerList(peers, count);
}

static void OnClientError(const char* msg, void* userdata) {
    Gui_ShowError(msg);
}

static void OnGuiStartServer(const char* name, uint16_t tcp_port, uint16_t udp_port, uint16_t discovery_port, void* userdata) {
    LOG_INFO("OnGuiStartServer called: name=%s, tcp_port=%d, udp_port=%d, discovery_port=%d", 
             name, tcp_port, udp_port, discovery_port);
    
    g_isServerMode = true;
    Client_StopDiscovery();
    Client_Disconnect();
    
    if (!g_opusEncoder) {
        LOG_INFO("Creating Opus encoder...");
        OpusEncoderConfig enc_config;
        OpusCodec_GetDefaultEncoderConfig(&enc_config);
        g_opusEncoder = OpusCodec_Create(&enc_config, NULL);
        if (!g_opusEncoder) {
            LOG_ERROR("Failed to create Opus encoder");
            Gui_ShowError("Cannot create Opus encoder, check if opus.dll exists");
            return;
        }
        LOG_INFO("Opus encoder created successfully");
    }
    
    ServerCallbacks cb = {
        .onStarted = OnServerStarted,
        .onStopped = OnServerStopped,
        .onClientJoined = OnClientJoined,
        .onClientLeft = OnClientLeft,
        .onError = OnServerError
    };
    
    LOG_INFO("Starting server...");
    if (!Server_Start(name, tcp_port, udp_port, discovery_port, &cb)) {
        LOG_ERROR("Server_Start failed");
        Gui_ShowError("Failed to start server, check if port is in use");
        return;
    }
    
    LOG_INFO("Server started, starting audio capture...");
    g_rtpTimestamp = 0;
    Audio_StartCapture(OnAudioCapture, NULL);
    Audio_StartPlayback();
    LOG_INFO("Server startup complete");
}

static void OnGuiStopServer(void* userdata) {
    Audio_StopCapture();
    Audio_StopPlayback();
    Server_Stop();
    g_isServerMode = false;
    
    if (g_opusEncoder) {
        OpusCodec_Destroy(g_opusEncoder);
        g_opusEncoder = NULL;
    }
    
    Client_StartDiscovery();
}

static void OnGuiConnect(const char* ip, uint16_t tcp_port, uint16_t udp_port, void* userdata) {
    LOG_INFO("OnGuiConnect called: ip=%s, tcp_port=%d, udp_port=%d", ip, tcp_port, udp_port);
    
    g_isServerMode = false;
    Server_Stop();
    
    if (!g_opusEncoder) {
        OpusEncoderConfig enc_config;
        OpusCodec_GetDefaultEncoderConfig(&enc_config);
        g_opusEncoder = OpusCodec_Create(&enc_config, NULL);
        if (!g_opusEncoder) {
            Gui_ShowError("Failed to create Opus encoder");
            return;
        }
    }
    
    // 直接使用传入的 udp_port（从GUI获取，可能是自动发现或手动输入的）
    if (!Client_Connect(ip, tcp_port, udp_port)) {
        Gui_ShowError("Failed to connect to server");
        return;
    }
    
    if (!Client_JoinSession()) {
        Client_Disconnect();
        Gui_ShowError("Failed to join voice session");
        return;
    }
    
    g_rtpTimestamp = 0;
    Audio_StartCapture(OnAudioCapture, NULL);
    Audio_StartPlayback();
    Gui_AddLog("Joined voice session (UDP audio)");
}

static void OnGuiDisconnect(void* userdata) {
    Audio_StopCapture();
    Audio_StopPlayback();
    Client_Disconnect();
    
    if (g_opusEncoder) {
        OpusCodec_Destroy(g_opusEncoder);
        g_opusEncoder = NULL;
    }
}

static void OnGuiRefreshServers(uint16_t discovery_port, void* userdata) {
    LOG_INFO("OnGuiRefreshServers called with discovery_port=%d", discovery_port);
    // 设置发现端口并刷新
    Client_SetDiscoveryPort(discovery_port);
    ServerInfo servers[MAX_SERVERS];
    int count = Client_GetServers(servers, MAX_SERVERS);
    Gui_UpdateServerList(servers, count);
}

static void OnGuiMuteChanged(bool muted, void* userdata) {
    Audio_SetCaptureMute(muted);
}

static void OnGuiVolumeChanged(int input, int output, void* userdata) {
    Audio_SetCaptureVolume(input / 100.0f);
    Audio_SetPlaybackVolume(output / 100.0f);
}

static VOID CALLBACK UpdateTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    float inputLevel = Audio_GetCaptureLevel();
    float outputLevel = Audio_GetPlaybackLevel();
    Gui_UpdateAudioLevel(inputLevel, outputLevel);
    
    if (!g_isServerMode && Client_IsInSession()) {
        float jitter_ms, loss_rate;
        int buffer_level;
        Client_GetJitterStats(&jitter_ms, &loss_rate, &buffer_level);
    }
    
    static DWORD lastRefresh = 0;
    if (time - lastRefresh > 3000) {
        lastRefresh = time;
        if (!g_isServerMode && !Client_IsConnected()) {
            ServerInfo servers[MAX_SERVERS];
            int count = Client_GetServers(servers, MAX_SERVERS);
            Gui_UpdateServerList(servers, count);
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // 初始化Opus动态加载 (从嵌入资源中提取DLL)
    if (!opus_dynamic_init()) {
        MessageBoxW(NULL, L"Failed to load Opus codec (opus.dll)", L"Error", MB_ICONERROR);
        return 1;
    }
    
    if (!Network_Init()) {
        MessageBoxW(NULL, L"Network init failed", L"Error", MB_ICONERROR);
        opus_dynamic_cleanup();
        return 1;
    }
    
    if (!Audio_Init()) {
        MessageBoxW(NULL, L"Audio init failed", L"Error", MB_ICONERROR);
        Network_Shutdown();
        opus_dynamic_cleanup();
        return 1;
    }
    
    if (!Server_Init()) {
        MessageBoxW(NULL, L"Server module init failed", L"Error", MB_ICONERROR);
        Audio_Shutdown();
        Network_Shutdown();
        opus_dynamic_cleanup();
        return 1;
    }
    
    if (!Client_Init()) {
        MessageBoxW(NULL, L"Client module init failed", L"Error", MB_ICONERROR);
        Server_Shutdown();
        Audio_Shutdown();
        Network_Shutdown();
        opus_dynamic_cleanup();
        return 1;
    }
    
    ClientCallbacks clientCb = {
        .onConnected = OnConnected,
        .onDisconnected = OnDisconnected,
        .onServerFound = OnServerFound,
        .onPeerJoined = OnPeerJoined,
        .onPeerLeft = OnPeerLeft,
        .onPeerListReceived = OnPeerListReceived,
        .onError = OnClientError
    };
    Client_SetCallbacks(&clientCb);
    
    GuiCallbacks guiCb = {
        .onStartServer = OnGuiStartServer,
        .onStopServer = OnGuiStopServer,
        .onConnect = OnGuiConnect,
        .onDisconnect = OnGuiDisconnect,
        .onRefreshServers = OnGuiRefreshServers,
        .onMuteChanged = OnGuiMuteChanged,
        .onVolumeChanged = OnGuiVolumeChanged
    };
    
    if (!Gui_Init(hInstance, &guiCb)) {
        MessageBoxW(NULL, L"GUI init failed", L"Error", MB_ICONERROR);
        Client_Shutdown();
        Server_Shutdown();
        Audio_Shutdown();
        Network_Shutdown();
        return 1;
    }
    
    Client_StartDiscovery();
    SetTimer(NULL, 0, 100, UpdateTimerProc);
    
    int result = Gui_Run();
    
    Audio_StopCapture();
    Audio_StopPlayback();
    Client_Disconnect();
    Client_StopDiscovery();
    Server_Stop();
    
    if (g_opusEncoder) {
        OpusCodec_Destroy(g_opusEncoder);
        g_opusEncoder = NULL;
    }
    
    Gui_Shutdown();
    Client_Shutdown();
    Server_Shutdown();
    Audio_Shutdown();
    Network_Shutdown();
    opus_dynamic_cleanup();
    
    return result;
}
