/**
 * @file audio_dsp.h
 * @brief 音频数字信号处理模块
 * 
 * 包含:
 * - 简单噪声门限 (Noise Gate)
 * - 非人声检测 (基于能量+过零率)
 * - 音量衰减/静音
 * - 预留 WebRTC/SpeexDSP 接口
 */

#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include "common.h"

//=============================================================================
// 常量定义
//=============================================================================

// 噪声检测阈值
#define NOISE_GATE_THRESHOLD_DB     -40.0f  // 静音门限 (dB)
#define HIGH_ENERGY_THRESHOLD_DB    -6.0f   // 高能量门限 (dB), 超过此值需要检测
#define ZERO_CROSSING_LOW           0.05f   // 低过零率阈值 (非人声特征)
#define ZERO_CROSSING_HIGH          0.40f   // 高过零率阈值 (人声特征)

// 衰减参数
#define ATTENUATION_FACTOR          0.1f    // 检测到噪声时的衰减系数
#define ATTACK_TIME_MS              5       // 攻击时间 (ms)
#define RELEASE_TIME_MS             50      // 释放时间 (ms)

//=============================================================================
// 数据结构
//=============================================================================

/**
 * @brief 音频帧分析结果
 */
typedef struct {
    float   rms_db;             // RMS 能量 (dB)
    float   peak_db;            // 峰值 (dB)
    float   zero_crossing_rate; // 过零率 (0-1)
    bool    is_silence;         // 是否静音
    bool    is_high_energy;     // 是否高能量
    bool    is_likely_noise;    // 是否可能是噪声 (高能量+低过零率)
    bool    is_likely_voice;    // 是否可能是人声
} AudioAnalysis;

/**
 * @brief 噪声门限状态
 */
typedef struct {
    float   current_gain;       // 当前增益 (0-1)
    float   target_gain;        // 目标增益
    int     hold_samples;       // 保持计数
    bool    gate_open;          // 门限是否打开
} NoiseGateState;

/**
 * @brief 音频 DSP 处理器配置
 */
typedef struct {
    float   noise_gate_threshold_db;    // 噪声门限 (dB)
    float   high_energy_threshold_db;   // 高能量检测门限 (dB)
    float   zcr_low_threshold;          // 低过零率阈值
    float   zcr_high_threshold;         // 高过零率阈值
    float   attenuation_factor;         // 噪声衰减系数
    bool    enable_noise_gate;          // 启用噪声门限
    bool    enable_noise_detection;     // 启用非人声检测
} AudioDspConfig;

/**
 * @brief 音频 DSP 处理器
 */
typedef struct AudioDsp AudioDsp;

//=============================================================================
// 公共接口
//=============================================================================

/**
 * @brief 获取默认配置
 */
void AudioDsp_GetDefaultConfig(AudioDspConfig* config);

/**
 * @brief 创建音频 DSP 处理器
 */
AudioDsp* AudioDsp_Create(const AudioDspConfig* config);

/**
 * @brief 销毁音频 DSP 处理器
 */
void AudioDsp_Destroy(AudioDsp* dsp);

/**
 * @brief 重置状态
 */
void AudioDsp_Reset(AudioDsp* dsp);

/**
 * @brief 分析音频帧 (不修改数据)
 * @param samples PCM 数据
 * @param count 采样数
 * @param analysis 输出分析结果
 */
void AudioDsp_Analyze(const int16_t* samples, int count, AudioAnalysis* analysis);

/**
 * @brief 处理音频帧 (可能修改数据)
 * @param dsp DSP 处理器
 * @param samples PCM 数据 (in/out)
 * @param count 采样数
 * @param analysis 输出分析结果 (可选, NULL 则不输出)
 * @return 处理后的增益系数 (0-1)
 */
float AudioDsp_Process(AudioDsp* dsp, int16_t* samples, int count, AudioAnalysis* analysis);

/**
 * @brief 仅检测是否需要衰减 (用于服务器端快速判断)
 * @param samples PCM 数据
 * @param count 采样数
 * @param threshold_db 高能量阈值 (dB)
 * @return 建议的增益系数 (0-1), 1.0 表示不需要衰减
 */
float AudioDsp_QuickNoiseCheck(const int16_t* samples, int count, float threshold_db);

//=============================================================================
// 工具函数
//=============================================================================

/**
 * @brief 计算 RMS (均方根)
 */
float AudioDsp_CalcRms(const int16_t* samples, int count);

/**
 * @brief 计算峰值
 */
int16_t AudioDsp_CalcPeak(const int16_t* samples, int count);

/**
 * @brief 计算过零率
 */
float AudioDsp_CalcZeroCrossingRate(const int16_t* samples, int count);

/**
 * @brief 线性值转 dB
 */
float AudioDsp_LinearToDb(float linear);

/**
 * @brief dB 转线性值
 */
float AudioDsp_DbToLinear(float db);

/**
 * @brief 应用增益
 */
void AudioDsp_ApplyGain(int16_t* samples, int count, float gain);

#endif // AUDIO_DSP_H
