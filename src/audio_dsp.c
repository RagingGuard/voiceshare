/**
 * @file audio_dsp.c
 * @brief 音频数字信号处理模块实现
 * 
 * 轻量级实现，专注于:
 * 1. 快速能量检测
 * 2. 过零率计算 (区分人声/噪声)
 * 3. 简单噪声门限
 * 4. 平滑增益控制
 */

#include "audio_dsp.h"
#include <math.h>

//=============================================================================
// 内部结构
//=============================================================================

struct AudioDsp {
    AudioDspConfig  config;
    NoiseGateState  gate_state;
    
    // 平滑参数
    float           attack_coef;    // 攻击系数
    float           release_coef;   // 释放系数
    
    // 历史数据 (用于平滑)
    float           prev_rms_db;
    float           prev_zcr;
};

//=============================================================================
// 工具函数实现
//=============================================================================

float AudioDsp_LinearToDb(float linear) {
    if (linear <= 0.0f) return -100.0f;
    return 20.0f * log10f(linear);
}

float AudioDsp_DbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

float AudioDsp_CalcRms(const int16_t* samples, int count) {
    if (!samples || count <= 0) return 0.0f;
    
    int64_t sum_sq = 0;
    for (int i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum_sq += s * s;
    }
    
    float rms = sqrtf((float)sum_sq / count);
    return rms / 32768.0f;  // 归一化到 0-1
}

int16_t AudioDsp_CalcPeak(const int16_t* samples, int count) {
    if (!samples || count <= 0) return 0;
    
    int16_t peak = 0;
    for (int i = 0; i < count; i++) {
        int16_t abs_val = (samples[i] < 0) ? -samples[i] : samples[i];
        if (abs_val > peak) peak = abs_val;
    }
    return peak;
}

float AudioDsp_CalcZeroCrossingRate(const int16_t* samples, int count) {
    if (!samples || count <= 1) return 0.0f;
    
    int crossings = 0;
    for (int i = 1; i < count; i++) {
        // 检测符号变化
        if ((samples[i-1] >= 0 && samples[i] < 0) ||
            (samples[i-1] < 0 && samples[i] >= 0)) {
            crossings++;
        }
    }
    
    return (float)crossings / (count - 1);
}

void AudioDsp_ApplyGain(int16_t* samples, int count, float gain) {
    if (!samples || count <= 0 || gain == 1.0f) return;
    
    if (gain <= 0.0f) {
        // 完全静音
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }
    
    for (int i = 0; i < count; i++) {
        float val = samples[i] * gain;
        // 软限幅
        if (val > 32767.0f) val = 32767.0f;
        if (val < -32768.0f) val = -32768.0f;
        samples[i] = (int16_t)val;
    }
}

//=============================================================================
// 公共接口实现
//=============================================================================

void AudioDsp_GetDefaultConfig(AudioDspConfig* config) {
    if (!config) return;
    
    config->noise_gate_threshold_db = NOISE_GATE_THRESHOLD_DB;
    config->high_energy_threshold_db = HIGH_ENERGY_THRESHOLD_DB;
    config->zcr_low_threshold = ZERO_CROSSING_LOW;
    config->zcr_high_threshold = ZERO_CROSSING_HIGH;
    config->attenuation_factor = ATTENUATION_FACTOR;
    config->enable_noise_gate = true;
    config->enable_noise_detection = true;
}

AudioDsp* AudioDsp_Create(const AudioDspConfig* config) {
    AudioDsp* dsp = (AudioDsp*)calloc(1, sizeof(AudioDsp));
    if (!dsp) return NULL;
    
    if (config) {
        dsp->config = *config;
    } else {
        AudioDsp_GetDefaultConfig(&dsp->config);
    }
    
    // 计算平滑系数 (基于 48kHz, 20ms 帧)
    float frame_time = AUDIO_FRAME_MS / 1000.0f;
    dsp->attack_coef = 1.0f - expf(-frame_time / (ATTACK_TIME_MS / 1000.0f));
    dsp->release_coef = 1.0f - expf(-frame_time / (RELEASE_TIME_MS / 1000.0f));
    
    dsp->gate_state.current_gain = 1.0f;
    dsp->gate_state.target_gain = 1.0f;
    dsp->gate_state.gate_open = true;
    
    dsp->prev_rms_db = -100.0f;
    dsp->prev_zcr = 0.5f;
    
    LOG_INFO("AudioDsp created: gate_threshold=%.1fdB, high_energy=%.1fdB",
             dsp->config.noise_gate_threshold_db, dsp->config.high_energy_threshold_db);
    
    return dsp;
}

void AudioDsp_Destroy(AudioDsp* dsp) {
    if (!dsp) return;
    free(dsp);
    LOG_DEBUG("AudioDsp destroyed");
}

void AudioDsp_Reset(AudioDsp* dsp) {
    if (!dsp) return;
    
    dsp->gate_state.current_gain = 1.0f;
    dsp->gate_state.target_gain = 1.0f;
    dsp->gate_state.gate_open = true;
    dsp->gate_state.hold_samples = 0;
    dsp->prev_rms_db = -100.0f;
    dsp->prev_zcr = 0.5f;
}

void AudioDsp_Analyze(const int16_t* samples, int count, AudioAnalysis* analysis) {
    if (!samples || count <= 0 || !analysis) return;
    
    memset(analysis, 0, sizeof(AudioAnalysis));
    
    // 计算基本指标
    float rms = AudioDsp_CalcRms(samples, count);
    int16_t peak = AudioDsp_CalcPeak(samples, count);
    float zcr = AudioDsp_CalcZeroCrossingRate(samples, count);
    
    analysis->rms_db = AudioDsp_LinearToDb(rms);
    analysis->peak_db = AudioDsp_LinearToDb((float)peak / 32768.0f);
    analysis->zero_crossing_rate = zcr;
    
    // 判断特征
    analysis->is_silence = (analysis->rms_db < NOISE_GATE_THRESHOLD_DB);
    analysis->is_high_energy = (analysis->rms_db > HIGH_ENERGY_THRESHOLD_DB);
    
    // 非人声检测: 高能量 + 低过零率 = 可能是低频噪声/啸叫
    // 人声特征: 中等过零率 (0.1 - 0.4)
    if (analysis->is_high_energy && zcr < ZERO_CROSSING_LOW) {
        analysis->is_likely_noise = true;
        analysis->is_likely_voice = false;
    } else if (!analysis->is_silence && zcr >= ZERO_CROSSING_LOW && zcr <= ZERO_CROSSING_HIGH) {
        analysis->is_likely_noise = false;
        analysis->is_likely_voice = true;
    } else {
        analysis->is_likely_noise = false;
        analysis->is_likely_voice = false;
    }
}

float AudioDsp_Process(AudioDsp* dsp, int16_t* samples, int count, AudioAnalysis* analysis) {
    if (!dsp || !samples || count <= 0) return 1.0f;
    
    // 分析
    AudioAnalysis local_analysis;
    AudioDsp_Analyze(samples, count, &local_analysis);
    
    if (analysis) {
        *analysis = local_analysis;
    }
    
    // 确定目标增益
    float target_gain = 1.0f;
    
    // 噪声门限
    if (dsp->config.enable_noise_gate && local_analysis.is_silence) {
        target_gain = 0.0f;
    }
    
    // 非人声检测 (只在高能量时触发)
    if (dsp->config.enable_noise_detection && local_analysis.is_likely_noise) {
        target_gain = dsp->config.attenuation_factor;
        LOG_DEBUG("Noise detected: rms=%.1fdB, zcr=%.3f -> attenuation=%.2f",
                  local_analysis.rms_db, local_analysis.zero_crossing_rate, target_gain);
    }
    
    dsp->gate_state.target_gain = target_gain;
    
    // 平滑增益变化
    float coef = (target_gain < dsp->gate_state.current_gain) 
                 ? dsp->attack_coef : dsp->release_coef;
    dsp->gate_state.current_gain += coef * (target_gain - dsp->gate_state.current_gain);
    
    // 应用增益
    if (dsp->gate_state.current_gain < 0.99f) {
        AudioDsp_ApplyGain(samples, count, dsp->gate_state.current_gain);
    }
    
    // 更新历史
    dsp->prev_rms_db = local_analysis.rms_db;
    dsp->prev_zcr = local_analysis.zero_crossing_rate;
    
    return dsp->gate_state.current_gain;
}

float AudioDsp_QuickNoiseCheck(const int16_t* samples, int count, float threshold_db) {
    if (!samples || count <= 0) return 1.0f;
    
    // 快速 RMS 计算 (只取部分样本加速)
    int step = (count > 240) ? count / 120 : 1;  // 最多取 120 个样本
    int64_t sum_sq = 0;
    int sample_count = 0;
    
    for (int i = 0; i < count; i += step) {
        int32_t s = samples[i];
        sum_sq += s * s;
        sample_count++;
    }
    
    float rms = sqrtf((float)sum_sq / sample_count) / 32768.0f;
    float rms_db = AudioDsp_LinearToDb(rms);
    
    // 低于门限 -> 静音
    if (rms_db < NOISE_GATE_THRESHOLD_DB) {
        return 0.0f;
    }
    
    // 高能量时做进一步检测
    if (rms_db > threshold_db) {
        // 快速过零率计算
        int crossings = 0;
        for (int i = step; i < count; i += step) {
            if ((samples[i-step] >= 0 && samples[i] < 0) ||
                (samples[i-step] < 0 && samples[i] >= 0)) {
                crossings++;
            }
        }
        float zcr = (float)crossings / (sample_count - 1);
        
        // 高能量 + 低过零率 = 噪声
        if (zcr < ZERO_CROSSING_LOW) {
            return ATTENUATION_FACTOR;
        }
    }
    
    return 1.0f;
}
