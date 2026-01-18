/**
 * @file opus_codec.h
 * @brief Opus 编解码器封装
 * 
 * Opus 是现代低延迟语音编解码器，特点:
 * - 支持 6kbps - 510kbps 码率
 * - 超低延迟 (2.5ms - 60ms 帧)
 * - 内置丢包补偿 (PLC)
 * - 适合实时语音通信
 */

#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include "common.h"

// 前向声明 (避免直接包含 opus.h)
typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

//=============================================================================
// 编码器配置
//=============================================================================
typedef struct {
    int sample_rate;        // 采样率 (8000/12000/16000/24000/48000)
    int channels;           // 声道数 (1/2)
    int bitrate;            // 目标码率 (bps)
    int complexity;         // 复杂度 (0-10)
    int frame_ms;           // 帧长度 (2.5/5/10/20/40/60 ms)
    bool vbr;               // 可变码率
    bool fec;               // 前向纠错
    bool dtx;               // 不连续传输 (静音检测)
} OpusEncoderConfig;

//=============================================================================
// 解码器配置
//=============================================================================
typedef struct {
    int sample_rate;        // 采样率
    int channels;           // 声道数
} OpusDecoderConfig;

//=============================================================================
// 编解码器句柄
//=============================================================================
typedef struct OpusCodec OpusCodec;

//=============================================================================
// 公共接口
//=============================================================================

/**
 * @brief 创建 Opus 编解码器
 * @param enc_config 编码器配置 (NULL 不创建编码器)
 * @param dec_config 解码器配置 (NULL 不创建解码器)
 * @return OpusCodec 实例
 */
OpusCodec* OpusCodec_Create(const OpusEncoderConfig* enc_config, 
                             const OpusDecoderConfig* dec_config);

/**
 * @brief 销毁 Opus 编解码器
 */
void OpusCodec_Destroy(OpusCodec* codec);

/**
 * @brief 编码 PCM 数据
 * @param codec OpusCodec 实例
 * @param pcm 输入 PCM 数据 (16-bit)
 * @param frame_size 输入采样数 (必须是 2.5/5/10/20/40/60ms 对应的采样数)
 * @param output 输出缓冲区
 * @param max_output 输出缓冲区大小
 * @return 编码后字节数, <0 表示错误
 */
int OpusCodec_Encode(OpusCodec* codec, const int16_t* pcm, int frame_size,
                      uint8_t* output, int max_output);

/**
 * @brief 解码 Opus 数据
 * @param codec OpusCodec 实例
 * @param data Opus 编码数据 (NULL 表示 PLC)
 * @param len 数据长度
 * @param pcm 输出 PCM 缓冲区
 * @param max_samples 最大输出采样数
 * @param decode_fec 是否解码 FEC (0=否, 1=是)
 * @return 解码后采样数, <0 表示错误
 */
int OpusCodec_Decode(OpusCodec* codec, const uint8_t* data, int len,
                      int16_t* pcm, int max_samples, int decode_fec);

/**
 * @brief PLC 丢包补偿 (调用 decode NULL)
 */
int OpusCodec_Plc(OpusCodec* codec, int16_t* pcm, int frame_size);

/**
 * @brief 设置编码器码率
 */
int OpusCodec_SetBitrate(OpusCodec* codec, int bitrate);

/**
 * @brief 获取编码器码率
 */
int OpusCodec_GetBitrate(OpusCodec* codec);

/**
 * @brief 设置编码器复杂度
 */
int OpusCodec_SetComplexity(OpusCodec* codec, int complexity);

/**
 * @brief 获取原始解码器指针 (用于 JitterBuffer)
 */
void* OpusCodec_GetDecoder(OpusCodec* codec);

/**
 * @brief 获取默认编码器配置
 */
void OpusCodec_GetDefaultEncoderConfig(OpusEncoderConfig* config);

/**
 * @brief 获取默认解码器配置
 */
void OpusCodec_GetDefaultDecoderConfig(OpusDecoderConfig* config);

//=============================================================================
// JitterBuffer 兼容函数
//=============================================================================

/**
 * @brief JitterBuffer 解码回调 (OpusDecodeFunc 签名)
 */
int OpusCodec_JitterDecode(void* decoder, const uint8_t* data, int len,
                            int16_t* pcm, int frame_size, int decode_fec);

/**
 * @brief JitterBuffer PLC 回调 (PlcFunc 签名)
 */
int OpusCodec_JitterPlc(void* decoder, int16_t* pcm, int frame_size);

/**
 * @brief 创建独立的 Opus 解码器 (用于 MultiStreamJitterBuffer)
 * @return 原始 OpusDecoder 指针
 */
void* OpusCodec_CreateDecoder(void);

/**
 * @brief 销毁独立的 Opus 解码器
 */
void OpusCodec_DestroyDecoder(void* decoder);

#endif // OPUS_CODEC_H
