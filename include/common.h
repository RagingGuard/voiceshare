/**
 * @file common.h
 * @brief 公共定义、常量、工具宏
 */

#ifndef COMMON_H
#define COMMON_H

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mmsystem.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "winmm.lib")
    #pragma comment(lib, "comctl32.lib")
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

//=============================================================================
// 版本信息
//=============================================================================
#define APP_NAME            "SharedVoice"
#define APP_VERSION         "1.0.0"
#define APP_TITLE           "共享语音平台 v1.0"

//=============================================================================
// 网络常量
//=============================================================================
#define DISCOVERY_PORT      37020       // UDP 发现端口
#define CONTROL_PORT        5000        // TCP 控制端口
#define AUDIO_UDP_PORT      6000        // UDP 音频端口
#define PROTOCOL_MAGIC      0x53565043  // 'SVPC'
#define PROTOCOL_VERSION    0x0200      // v2.0 (Opus + RTP-like)

//=============================================================================
// 音频常量
//=============================================================================
#define AUDIO_SAMPLE_RATE   48000       // 采样率
#define AUDIO_CHANNELS      1           // 声道数
#define AUDIO_BITS          16          // 位深度
#define AUDIO_FRAME_MS      20          // 帧时长 (毫秒)
#define AUDIO_FRAME_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000)  // 960
#define AUDIO_FRAME_BYTES   (AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * (AUDIO_BITS / 8))  // 1920
#define AUDIO_BUFFER_COUNT  4           // 缓冲区数量

// Opus 编码常量
#define OPUS_BITRATE        32000       // Opus 码率 (32kbps)
#define OPUS_COMPLEXITY     5           // Opus 复杂度 (0-10)
#define OPUS_MAX_PACKET     512         // Opus 最大包大小

// Jitter Buffer 常量 - 低延迟配置
#define JITTER_BUFFER_MS    20          // 抖动缓冲时长 (毫秒) - 最低延迟
#define JITTER_MIN_MS       10          // 最小缓冲
#define JITTER_MAX_MS       60          // 最大缓冲
#define JITTER_BUFFER_SLOTS 16          // 缓冲槽数量 - 减少以降低延迟

//=============================================================================
// 限制常量
//=============================================================================
#define MAX_CLIENTS         16          // 最大客户端数
#define MAX_SERVERS         32          // 最大发现服务器数
#define MAX_NAME_LEN        32          // 名称最大长度
#define MAX_PACKET_SIZE     4096        // 最大数据包大小
#define HEARTBEAT_INTERVAL  3000        // 心跳间隔 (毫秒)
#define HEARTBEAT_TIMEOUT   10000       // 心跳超时 (毫秒)
#define DISCOVERY_TIMEOUT   2000        // 发现超时 (毫秒)
#define DISCOVERY_INTERVAL  3000        // 发现间隔 (毫秒)

//=============================================================================
// 工具宏
//=============================================================================
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX(v, lo), hi)

//=============================================================================
// 日志宏
//=============================================================================
#ifdef _DEBUG
    #define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...)
#endif
#define LOG_INFO(fmt, ...)  printf("[INFO]  " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  printf("[WARN]  " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

//=============================================================================
// 时间函数
//=============================================================================
static inline uint32_t GetTickCountMs(void) {
    return (uint32_t)GetTickCount();
}

static inline uint64_t GetTickCount64Ms(void) {
    return GetTickCount64();
}

//=============================================================================
// 原子操作
//=============================================================================
typedef volatile LONG AtomicInt;
#define AtomicRead(p)       InterlockedCompareExchange(p, 0, 0)
#define AtomicSet(p, v)     InterlockedExchange(p, v)
#define AtomicInc(p)        InterlockedIncrement(p)
#define AtomicDec(p)        InterlockedDecrement(p)

//=============================================================================
// 互斥锁
//=============================================================================
typedef CRITICAL_SECTION Mutex;
#define MutexInit(m)        InitializeCriticalSection(m)
#define MutexDestroy(m)     DeleteCriticalSection(m)
#define MutexLock(m)        EnterCriticalSection(m)
#define MutexUnlock(m)      LeaveCriticalSection(m)

//=============================================================================
// 线程
//=============================================================================
typedef HANDLE Thread;
typedef DWORD (WINAPI *ThreadFunc)(LPVOID);
#define ThreadCreate(t, f, a)  (*(t) = CreateThread(NULL, 0, f, a, 0, NULL))
#define ThreadJoin(t)          WaitForSingleObject(t, INFINITE)
#define ThreadClose(t)         CloseHandle(t)

//=============================================================================
// 事件
//=============================================================================
typedef HANDLE Event;
#define EventCreate()       CreateEvent(NULL, FALSE, FALSE, NULL)
#define EventDestroy(e)     CloseHandle(e)
#define EventSet(e)         SetEvent(e)
#define EventWait(e, ms)    WaitForSingleObject(e, ms)

#endif // COMMON_H
