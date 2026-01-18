/**
 * @file opus_codec.c
 * @brief Opus 编解码器封装实现
 * 
 * 使用动态加载的 libopus 库进行音频编解码
 */

#include "opus_codec.h"
#include "opus_dynamic.h"

//=============================================================================
// 内部结构
//=============================================================================
struct OpusCodec {
    OpusEncoder* encoder;
    OpusDecoder* decoder;
    
    OpusEncoderConfig enc_config;
    OpusDecoderConfig dec_config;
    
    bool has_encoder;
    bool has_decoder;
};

//=============================================================================
// 公共接口实现
//=============================================================================

void OpusCodec_GetDefaultEncoderConfig(OpusEncoderConfig* config) {
    if (!config) return;
    
    config->sample_rate = AUDIO_SAMPLE_RATE;
    config->channels = AUDIO_CHANNELS;
    config->bitrate = OPUS_BITRATE;
    config->complexity = OPUS_COMPLEXITY;
    config->frame_ms = AUDIO_FRAME_MS;
    config->vbr = true;
    config->fec = true;      // 启用前向纠错
    config->dtx = false;     // 关闭 DTX (工业场景不需要)
}

void OpusCodec_GetDefaultDecoderConfig(OpusDecoderConfig* config) {
    if (!config) return;
    
    config->sample_rate = AUDIO_SAMPLE_RATE;
    config->channels = AUDIO_CHANNELS;
}

OpusCodec* OpusCodec_Create(const OpusEncoderConfig* enc_config, 
                             const OpusDecoderConfig* dec_config) {
    // 确保Opus已加载
    if (!opus_is_loaded()) {
        if (!opus_dynamic_init()) {
            LOG_ERROR("Failed to initialize Opus dynamic loader");
            return NULL;
        }
    }
    
    OpusCodec* codec = (OpusCodec*)calloc(1, sizeof(OpusCodec));
    if (!codec) return NULL;
    
    int error;
    
    // 创建编码器
    if (enc_config) {
        codec->enc_config = *enc_config;
        
        codec->encoder = p_opus_encoder_create(
            enc_config->sample_rate,
            enc_config->channels,
            OPUS_APPLICATION_VOIP,  // 语音模式
            &error
        );
        
        if (error != OPUS_OK || !codec->encoder) {
            LOG_ERROR("Failed to create Opus encoder: %s", p_opus_strerror(error));
            free(codec);
            return NULL;
        }
        
        // 配置编码器
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(enc_config->bitrate));
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_COMPLEXITY(enc_config->complexity));
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_VBR(enc_config->vbr ? 1 : 0));
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_INBAND_FEC(enc_config->fec ? 1 : 0));
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_DTX(enc_config->dtx ? 1 : 0));
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        
        // 设置丢包率预期 (用于 FEC 决策)
        p_opus_encoder_ctl(codec->encoder, OPUS_SET_PACKET_LOSS_PERC(5));
        
        codec->has_encoder = true;
        
        LOG_INFO("Opus encoder created: %dHz, %dch, %dbps, complexity=%d",
                 enc_config->sample_rate, enc_config->channels,
                 enc_config->bitrate, enc_config->complexity);
    }
    
    // 创建解码器
    if (dec_config) {
        codec->dec_config = *dec_config;
        
        codec->decoder = p_opus_decoder_create(
            dec_config->sample_rate,
            dec_config->channels,
            &error
        );
        
        if (error != OPUS_OK || !codec->decoder) {
            LOG_ERROR("Failed to create Opus decoder: %s", p_opus_strerror(error));
            if (codec->encoder) {
                p_opus_encoder_destroy(codec->encoder);
            }
            free(codec);
            return NULL;
        }
        
        codec->has_decoder = true;
        
        LOG_INFO("Opus decoder created: %dHz, %dch",
                 dec_config->sample_rate, dec_config->channels);
    }
    
    return codec;
}

void OpusCodec_Destroy(OpusCodec* codec) {
    if (!codec) return;
    
    if (codec->encoder) {
        p_opus_encoder_destroy(codec->encoder);
        LOG_INFO("Opus encoder destroyed");
    }
    
    if (codec->decoder) {
        p_opus_decoder_destroy(codec->decoder);
        LOG_INFO("Opus decoder destroyed");
    }
    
    free(codec);
}

int OpusCodec_Encode(OpusCodec* codec, const int16_t* pcm, int frame_size,
                      uint8_t* output, int max_output) {
    if (!codec || !codec->has_encoder || !pcm || !output) {
        return -1;
    }
    
    int encoded = p_opus_encode(codec->encoder, pcm, frame_size, output, max_output);
    
    if (encoded < 0) {
        LOG_ERROR("Opus encode error: %s", p_opus_strerror(encoded));
        return -1;
    }
    
    return encoded;
}

int OpusCodec_Decode(OpusCodec* codec, const uint8_t* data, int len,
                      int16_t* pcm, int max_samples, int decode_fec) {
    if (!codec || !codec->has_decoder || !pcm) {
        return -1;
    }
    
    int decoded = p_opus_decode(codec->decoder, data, len, pcm, max_samples, decode_fec);
    
    if (decoded < 0) {
        LOG_ERROR("Opus decode error: %s", p_opus_strerror(decoded));
        return -1;
    }
    
    return decoded;
}

int OpusCodec_Plc(OpusCodec* codec, int16_t* pcm, int frame_size) {
    if (!codec || !codec->has_decoder || !pcm) {
        return -1;
    }
    
    // 调用解码器的 PLC (data=NULL)
    int decoded = p_opus_decode(codec->decoder, NULL, 0, pcm, frame_size, 0);
    
    if (decoded < 0) {
        LOG_ERROR("Opus PLC error: %s", p_opus_strerror(decoded));
        // 静音填充
        memset(pcm, 0, frame_size * sizeof(int16_t));
        return frame_size;
    }
    
    return decoded;
}

int OpusCodec_SetBitrate(OpusCodec* codec, int bitrate) {
    if (!codec || !codec->has_encoder) return -1;
    
    int ret = p_opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(bitrate));
    if (ret == OPUS_OK) {
        codec->enc_config.bitrate = bitrate;
        LOG_INFO("Opus bitrate set to %d", bitrate);
    }
    return ret;
}

int OpusCodec_GetBitrate(OpusCodec* codec) {
    if (!codec || !codec->has_encoder) return -1;
    
    opus_int32 bitrate;
    p_opus_encoder_ctl(codec->encoder, OPUS_GET_BITRATE(&bitrate));
    return bitrate;
}

int OpusCodec_SetComplexity(OpusCodec* codec, int complexity) {
    if (!codec || !codec->has_encoder) return -1;
    
    int ret = p_opus_encoder_ctl(codec->encoder, OPUS_SET_COMPLEXITY(complexity));
    if (ret == OPUS_OK) {
        codec->enc_config.complexity = complexity;
    }
    return ret;
}

void* OpusCodec_GetDecoder(OpusCodec* codec) {
    if (!codec) return NULL;
    return codec->decoder;
}

//=============================================================================
// JitterBuffer 兼容函数
//=============================================================================

int OpusCodec_JitterDecode(void* decoder, const uint8_t* data, int len,
                            int16_t* pcm, int frame_size, int decode_fec) {
    if (!decoder || !pcm) return -1;
    
    OpusDecoder* dec = (OpusDecoder*)decoder;
    return p_opus_decode(dec, data, len, pcm, frame_size, decode_fec);
}

int OpusCodec_JitterPlc(void* decoder, int16_t* pcm, int frame_size) {
    if (!decoder || !pcm) return -1;
    
    OpusDecoder* dec = (OpusDecoder*)decoder;
    int ret = p_opus_decode(dec, NULL, 0, pcm, frame_size, 0);
    
    if (ret < 0) {
        memset(pcm, 0, frame_size * sizeof(int16_t));
        return frame_size;
    }
    
    return ret;
}

//=============================================================================
// 独立解码器创建/销毁 (用于 MultiStreamJitterBuffer)
//=============================================================================

void* OpusCodec_CreateDecoder(void) {
    if (!opus_is_loaded()) {
        if (!opus_dynamic_init()) {
            LOG_ERROR("Failed to initialize Opus for decoder creation");
            return NULL;
        }
    }
    
    int error;
    OpusDecoder* decoder = p_opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, &error);
    
    if (error != OPUS_OK || !decoder) {
        LOG_ERROR("Failed to create independent Opus decoder: %s", p_opus_strerror(error));
        return NULL;
    }
    
    LOG_DEBUG("Independent Opus decoder created");
    return decoder;
}

void OpusCodec_DestroyDecoder(void* decoder) {
    if (!decoder) return;
    
    p_opus_decoder_destroy((OpusDecoder*)decoder);
    LOG_DEBUG("Independent Opus decoder destroyed");
}
