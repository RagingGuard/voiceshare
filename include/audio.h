/**
 * @file audio.h
 * @brief 音频引擎头文件
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

//=============================================================================
// 回调函数类型
//=============================================================================

/** 音频采集回调 */
typedef void (*AudioCaptureCallback)(const int16_t* samples, int count, void* userdata);

/** 音频播放请求回调 */
typedef int (*AudioPlaybackCallback)(int16_t* samples, int count, void* userdata);

//=============================================================================
// 音频引擎接口
//=============================================================================

/**
 * @brief 初始化音频引擎
 */
bool Audio_Init(void);

/**
 * @brief 关闭音频引擎
 */
void Audio_Shutdown(void);

/**
 * @brief 启动音频采集
 */
bool Audio_StartCapture(AudioCaptureCallback callback, void* userdata);

/**
 * @brief 停止音频采集
 */
void Audio_StopCapture(void);

/**
 * @brief 启动音频播放
 */
bool Audio_StartPlayback(void);

/**
 * @brief 停止音频播放
 */
void Audio_StopPlayback(void);

/**
 * @brief 提交音频数据用于播放
 */
bool Audio_SubmitPlaybackData(const int16_t* samples, int count);

/**
 * @brief 设置采集静音
 */
void Audio_SetCaptureMute(bool mute);

/**
 * @brief 获取采集静音状态
 */
bool Audio_GetCaptureMute(void);

/**
 * @brief 设置采集音量 (0.0 - 2.0)
 */
void Audio_SetCaptureVolume(float volume);

/**
 * @brief 设置播放音量 (0.0 - 2.0)
 */
void Audio_SetPlaybackVolume(float volume);

/**
 * @brief 获取采集电平 (0.0 - 1.0)
 */
float Audio_GetCaptureLevel(void);

/**
 * @brief 获取播放电平 (0.0 - 1.0)
 */
float Audio_GetPlaybackLevel(void);

/**
 * @brief 枚举输入设备
 */
int Audio_EnumCaptureDevices(char names[][64], int max_count);

/**
 * @brief 枚举输出设备
 */
int Audio_EnumPlaybackDevices(char names[][64], int max_count);

//=============================================================================
// 音频混合器
//=============================================================================

/**
 * @brief 混合多个音频流
 * @param output 输出缓冲区
 * @param inputs 输入缓冲区数组
 * @param input_count 输入数量
 * @param sample_count 采样数
 */
void Audio_Mix(int16_t* output, const int16_t** inputs, int input_count, int sample_count);

#endif // AUDIO_H
