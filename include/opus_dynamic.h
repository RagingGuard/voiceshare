/**
 * @file opus_dynamic.h
 * @brief Opus动态加载接口
 * 
 * 提供运行时加载opus.dll的函数指针
 */

#ifndef OPUS_DYNAMIC_H
#define OPUS_DYNAMIC_H

#include <windows.h>
#include <stdint.h>

// Opus类型定义
typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;
typedef int32_t opus_int32;

// Opus常量
#define OPUS_OK                 0
#define OPUS_APPLICATION_VOIP   2048
#define OPUS_SIGNAL_VOICE       3001

// Opus CTL请求
#define OPUS_SET_BITRATE_REQUEST        4002
#define OPUS_GET_BITRATE_REQUEST        4003
#define OPUS_SET_VBR_REQUEST            4006
#define OPUS_SET_COMPLEXITY_REQUEST     4010
#define OPUS_SET_INBAND_FEC_REQUEST     4012
#define OPUS_SET_PACKET_LOSS_PERC_REQUEST 4014
#define OPUS_SET_DTX_REQUEST            4016
#define OPUS_SET_SIGNAL_REQUEST         4024

// Opus CTL宏
#define OPUS_SET_BITRATE(x)         OPUS_SET_BITRATE_REQUEST, (opus_int32)(x)
#define OPUS_GET_BITRATE(x)         OPUS_GET_BITRATE_REQUEST, (opus_int32*)(x)
#define OPUS_SET_VBR(x)             OPUS_SET_VBR_REQUEST, (opus_int32)(x)
#define OPUS_SET_COMPLEXITY(x)      OPUS_SET_COMPLEXITY_REQUEST, (opus_int32)(x)
#define OPUS_SET_INBAND_FEC(x)      OPUS_SET_INBAND_FEC_REQUEST, (opus_int32)(x)
#define OPUS_SET_PACKET_LOSS_PERC(x) OPUS_SET_PACKET_LOSS_PERC_REQUEST, (opus_int32)(x)
#define OPUS_SET_DTX(x)             OPUS_SET_DTX_REQUEST, (opus_int32)(x)
#define OPUS_SET_SIGNAL(x)          OPUS_SET_SIGNAL_REQUEST, (opus_int32)(x)

// 函数指针类型定义
typedef OpusEncoder* (*opus_encoder_create_fn)(int32_t Fs, int channels, int application, int *error);
typedef void (*opus_encoder_destroy_fn)(OpusEncoder *st);
typedef int (*opus_encode_fn)(OpusEncoder *st, const int16_t *pcm, int frame_size, unsigned char *data, int32_t max_data_bytes);
typedef int (*opus_encoder_ctl_fn)(OpusEncoder *st, int request, ...);

typedef OpusDecoder* (*opus_decoder_create_fn)(int32_t Fs, int channels, int *error);
typedef void (*opus_decoder_destroy_fn)(OpusDecoder *st);
typedef int (*opus_decode_fn)(OpusDecoder *st, const unsigned char *data, int32_t len, int16_t *pcm, int frame_size, int decode_fec);

typedef const char* (*opus_strerror_fn)(int error);
typedef const char* (*opus_get_version_string_fn)(void);

// 全局函数指针
extern opus_encoder_create_fn       p_opus_encoder_create;
extern opus_encoder_destroy_fn      p_opus_encoder_destroy;
extern opus_encode_fn               p_opus_encode;
extern opus_encoder_ctl_fn          p_opus_encoder_ctl;
extern opus_decoder_create_fn       p_opus_decoder_create;
extern opus_decoder_destroy_fn      p_opus_decoder_destroy;
extern opus_decode_fn               p_opus_decode;
extern opus_strerror_fn             p_opus_strerror;
extern opus_get_version_string_fn   p_opus_get_version_string;

/**
 * @brief 初始化Opus动态加载（从嵌入的DLL资源加载）
 * @return 成功返回1，失败返回0
 */
int opus_dynamic_init(void);

/**
 * @brief 清理Opus动态加载资源
 */
void opus_dynamic_cleanup(void);

/**
 * @brief 检查Opus是否已加载
 * @return 已加载返回1，未加载返回0
 */
int opus_is_loaded(void);

#endif // OPUS_DYNAMIC_H
