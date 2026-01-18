/**
 * @file jitter_buffer.h
 * @brief Jitter Buffer (抖动缓冲) 接口
 * 
 * Jitter Buffer 用于:
 * 1. 吸收网络抖动
 * 2. 重排序乱序数据包
 * 3. 保证音频连续播放
 * 4. 处理丢包
 */

#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include "common.h"
#include "protocol.h"

//=============================================================================
// 常量定义
//=============================================================================
#define JB_SLOT_EMPTY       0
#define JB_SLOT_FILLED      1
#define JB_SLOT_DECODED     2

//=============================================================================
// 数据结构
//=============================================================================

/**
 * @brief Jitter Buffer 槽
 */
typedef struct {
    uint8_t  state;                         // 槽状态
    uint16_t sequence;                      // 序列号
    uint32_t timestamp;                     // 采样时间戳
    uint32_t ssrc;                          // 来源标识
    uint16_t payload_len;                   // 负载长度
    uint8_t  payload[OPUS_MAX_PACKET];      // Opus 编码数据
    int16_t  decoded[AUDIO_FRAME_SAMPLES];  // 解码后 PCM
    int      decoded_samples;               // 解码采样数
    uint64_t recv_time;                     // 接收时间
} JitterSlot;

/**
 * @brief Jitter Buffer 统计
 */
typedef struct {
    uint32_t packets_received;  // 接收包数
    uint32_t packets_lost;      // 丢包数
    uint32_t packets_late;      // 迟到包数
    uint32_t packets_reorder;   // 乱序包数
    uint32_t underruns;         // 欠载次数 (缓冲区空)
    uint32_t overruns;          // 过载次数 (缓冲区满)
    float    avg_jitter_ms;     // 平均抖动 (毫秒)
    float    loss_rate;         // 丢包率
} JitterStats;

/**
 * @brief Jitter Buffer 配置
 */
typedef struct {
    uint32_t min_delay_ms;      // 最小延迟
    uint32_t max_delay_ms;      // 最大延迟
    uint32_t target_delay_ms;   // 目标延迟
    bool     adaptive;          // 自适应延迟
} JitterConfig;

/**
 * @brief Jitter Buffer 实例
 */
typedef struct JitterBuffer JitterBuffer;

//=============================================================================
// 公共接口
//=============================================================================

/**
 * @brief 创建 Jitter Buffer
 * @param config 配置 (NULL 使用默认配置)
 * @return JitterBuffer 实例
 */
JitterBuffer* JitterBuffer_Create(const JitterConfig* config);

/**
 * @brief 销毁 Jitter Buffer
 */
void JitterBuffer_Destroy(JitterBuffer* jb);

/**
 * @brief 重置 Jitter Buffer
 */
void JitterBuffer_Reset(JitterBuffer* jb);

/**
 * @brief 放入 RTP 包
 * @param jb JitterBuffer 实例
 * @param rtp RTP 包头
 * @param payload 编码数据
 * @param payload_len 数据长度
 * @return 0 成功, <0 失败
 */
int JitterBuffer_Put(JitterBuffer* jb, const RtpHeader* rtp, 
                     const uint8_t* payload, uint16_t payload_len);

/**
 * @brief 获取解码后的音频帧
 * @param jb JitterBuffer 实例
 * @param samples 输出 PCM 缓冲区
 * @param max_samples 缓冲区最大采样数
 * @return 实际采样数, 0 表示无数据, <0 表示错误
 */
int JitterBuffer_Get(JitterBuffer* jb, int16_t* samples, int max_samples);

/**
 * @brief 获取当前缓冲级别 (毫秒)
 */
int JitterBuffer_GetLevel(JitterBuffer* jb);

/**
 * @brief 获取统计信息
 */
void JitterBuffer_GetStats(JitterBuffer* jb, JitterStats* stats);

/**
 * @brief 设置 Opus 解码器 (外部提供)
 * @param jb JitterBuffer 实例
 * @param decoder Opus 解码器指针
 * @param decode_func 解码函数
 */
typedef int (*OpusDecodeFunc)(void* decoder, const uint8_t* data, int len,
                               int16_t* pcm, int frame_size, int decode_fec);
void JitterBuffer_SetDecoder(JitterBuffer* jb, void* decoder, OpusDecodeFunc decode_func);

/**
 * @brief 设置 PLC (Packet Loss Concealment) 回调
 */
typedef int (*PlcFunc)(void* decoder, int16_t* pcm, int frame_size);
void JitterBuffer_SetPlc(JitterBuffer* jb, PlcFunc plc_func);

//=============================================================================
// 多流 Jitter Buffer (per-SSRC)
//=============================================================================

/**
 * @brief 单个流的信息
 */
typedef struct {
    uint32_t        ssrc;           // 流标识
    JitterBuffer*   jitter_buffer;  // 对应的 JitterBuffer
    void*           decoder;        // 独立的 Opus 解码器
    uint64_t        last_active;    // 最后活跃时间
    bool            active;         // 是否活跃
} StreamInfo;

/**
 * @brief 多流 Jitter Buffer 管理器
 */
typedef struct MultiStreamJitterBuffer MultiStreamJitterBuffer;

/**
 * @brief 创建多流 JitterBuffer 管理器
 * @param max_streams 最大流数量（建议设为 MAX_CLIENTS）
 * @param config 每个流的 JitterBuffer 配置
 * @return 管理器实例
 */
MultiStreamJitterBuffer* MultiStreamJB_Create(int max_streams, const JitterConfig* config);

/**
 * @brief 销毁多流 JitterBuffer 管理器
 */
void MultiStreamJB_Destroy(MultiStreamJitterBuffer* msjb);

/**
 * @brief 重置所有流
 */
void MultiStreamJB_Reset(MultiStreamJitterBuffer* msjb);

/**
 * @brief 放入 RTP 包（自动按 SSRC 分流）
 * @return 0 成功, <0 失败
 */
int MultiStreamJB_Put(MultiStreamJitterBuffer* msjb, const RtpHeader* rtp,
                      const uint8_t* payload, uint16_t payload_len);

/**
 * @brief 获取混音后的音频帧
 * @param msjb 管理器实例
 * @param samples 输出 PCM 缓冲区
 * @param max_samples 缓冲区最大采样数
 * @return 实际采样数, 0 表示无数据
 */
int MultiStreamJB_GetMixed(MultiStreamJitterBuffer* msjb, int16_t* samples, int max_samples);

/**
 * @brief 获取当前活跃流数量
 */
int MultiStreamJB_GetActiveStreams(MultiStreamJitterBuffer* msjb);

/**
 * @brief 获取总体统计（所有流汇总）
 */
void MultiStreamJB_GetStats(MultiStreamJitterBuffer* msjb, JitterStats* stats);

/**
 * @brief 设置解码器创建/销毁回调
 */
typedef void* (*DecoderCreateFunc)(void);
typedef void (*DecoderDestroyFunc)(void* decoder);
void MultiStreamJB_SetDecoderFactory(MultiStreamJitterBuffer* msjb,
                                      DecoderCreateFunc create_func,
                                      DecoderDestroyFunc destroy_func,
                                      OpusDecodeFunc decode_func,
                                      PlcFunc plc_func);

/**
 * @brief 清理超时不活跃的流
 * @param timeout_ms 超时时间（毫秒）
 */
void MultiStreamJB_CleanupInactive(MultiStreamJitterBuffer* msjb, uint32_t timeout_ms);

#endif // JITTER_BUFFER_H
