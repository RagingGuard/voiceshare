/**
 * @file jitter_buffer.c
 * @brief Jitter Buffer (抖动缓冲) 实现
 * 
 * 工作原理:
 * 1. UDP 接收数据包 -> 放入环形缓冲区
 * 2. 按序列号排序
 * 3. 延迟一定时间后输出
 * 4. 丢包时使用 PLC 补偿
 */

#include "jitter_buffer.h"
#include <stdio.h>

//=============================================================================
// 内部结构
//=============================================================================
struct JitterBuffer {
    // 配置
    JitterConfig config;
    
    // 环形缓冲区
    JitterSlot   slots[JITTER_BUFFER_SLOTS];
    int          head;              // 读取位置
    int          tail;              // 写入位置
    int          count;             // 当前包数
    
    // 序列号跟踪
    uint16_t     next_seq;          // 期望的下一个序列号
    bool         seq_initialized;   // 序列号是否初始化
    
    // 时间戳跟踪
    uint32_t     base_timestamp;    // 基准时间戳
    uint64_t     base_time;         // 基准本地时间
    bool         time_initialized;  // 时间是否初始化
    
    // 抖动计算
    float        jitter;            // 当前抖动估计
    uint64_t     last_recv_time;    // 上次接收时间
    uint32_t     last_timestamp;    // 上次时间戳
    
    // 统计
    JitterStats  stats;
    
    // 解码器
    void*        decoder;
    OpusDecodeFunc decode_func;
    PlcFunc      plc_func;
    
    // 同步
    Mutex        mutex;
};

//=============================================================================
// 内部函数
//=============================================================================

/**
 * @brief 序列号比较 (处理回绕)
 * @return <0 a在b之前, 0 相等, >0 a在b之后
 */
static inline int seq_compare(uint16_t a, uint16_t b) {
    int16_t diff = (int16_t)(a - b);
    return diff;
}

/**
 * @brief 计算序列号距离
 */
static inline int seq_distance(uint16_t from, uint16_t to) {
    return (int16_t)(to - from);
}

/**
 * @brief 查找序列号对应的槽位置
 */
static int find_slot_for_seq(JitterBuffer* jb, uint16_t seq) {
    if (!jb->seq_initialized) {
        return jb->tail;
    }
    
    int distance = seq_distance(jb->next_seq, seq);
    
    // 太旧的包 (已经播放过)
    if (distance < -JITTER_BUFFER_SLOTS / 2) {
        return -1;  // 丢弃
    }
    
    // 太新的包
    if (distance >= JITTER_BUFFER_SLOTS) {
        return -2;  // 缓冲区溢出
    }
    
    // 计算槽索引
    int slot = (jb->head + distance + JITTER_BUFFER_SLOTS) % JITTER_BUFFER_SLOTS;
    return slot;
}

/**
 * @brief 更新抖动估计 (RFC 3550 算法)
 */
static void update_jitter(JitterBuffer* jb, uint32_t timestamp, uint64_t recv_time) {
    if (jb->last_recv_time == 0) {
        jb->last_recv_time = recv_time;
        jb->last_timestamp = timestamp;
        return;
    }
    
    // 到达间隔 (毫秒)
    int64_t d_recv = (int64_t)(recv_time - jb->last_recv_time);
    
    // 时间戳间隔 (转换为毫秒, 48kHz -> ms)
    int64_t d_ts = ((int64_t)(timestamp - jb->last_timestamp) * 1000) / AUDIO_SAMPLE_RATE;
    
    // 差异
    int64_t diff = d_recv - d_ts;
    if (diff < 0) diff = -diff;
    
    // 指数移动平均
    jb->jitter = jb->jitter + ((float)diff - jb->jitter) / 16.0f;
    jb->stats.avg_jitter_ms = jb->jitter;
    
    jb->last_recv_time = recv_time;
    jb->last_timestamp = timestamp;
}

/**
 * @brief 解码槽中的数据
 */
static int decode_slot(JitterBuffer* jb, JitterSlot* slot) {
    if (slot->state != JB_SLOT_FILLED) {
        return -1;
    }
    
    if (jb->decode_func && jb->decoder) {
        slot->decoded_samples = jb->decode_func(
            jb->decoder,
            slot->payload,
            slot->payload_len,
            slot->decoded,
            AUDIO_FRAME_SAMPLES,
            0  // no FEC
        );
        
        if (slot->decoded_samples > 0) {
            slot->state = JB_SLOT_DECODED;
            return slot->decoded_samples;
        }
    }
    
    return -1;
}

/**
 * @brief PLC 补偿丢失帧
 */
static int plc_frame(JitterBuffer* jb, int16_t* samples, int frame_size) {
    if (jb->plc_func && jb->decoder) {
        return jb->plc_func(jb->decoder, samples, frame_size);
    }
    
    // 默认: 静音
    memset(samples, 0, frame_size * sizeof(int16_t));
    return frame_size;
}

//=============================================================================
// 公共接口实现
//=============================================================================

JitterBuffer* JitterBuffer_Create(const JitterConfig* config) {
    JitterBuffer* jb = (JitterBuffer*)calloc(1, sizeof(JitterBuffer));
    if (!jb) return NULL;
    
    // 默认配置
    if (config) {
        jb->config = *config;
    } else {
        jb->config.min_delay_ms = JITTER_MIN_MS;
        jb->config.max_delay_ms = JITTER_MAX_MS;
        jb->config.target_delay_ms = JITTER_BUFFER_MS;
        jb->config.adaptive = true;
    }
    
    MutexInit(&jb->mutex);
    
    LOG_INFO("JitterBuffer created: target=%dms, min=%dms, max=%dms",
             jb->config.target_delay_ms, jb->config.min_delay_ms, jb->config.max_delay_ms);
    
    return jb;
}

void JitterBuffer_Destroy(JitterBuffer* jb) {
    if (!jb) return;
    
    MutexDestroy(&jb->mutex);
    free(jb);
    
    LOG_INFO("JitterBuffer destroyed");
}

void JitterBuffer_Reset(JitterBuffer* jb) {
    if (!jb) return;
    
    MutexLock(&jb->mutex);
    
    for (int i = 0; i < JITTER_BUFFER_SLOTS; i++) {
        jb->slots[i].state = JB_SLOT_EMPTY;
    }
    
    jb->head = 0;
    jb->tail = 0;
    jb->count = 0;
    jb->seq_initialized = false;
    jb->time_initialized = false;
    jb->jitter = 0;
    jb->last_recv_time = 0;
    jb->last_timestamp = 0;
    
    memset(&jb->stats, 0, sizeof(jb->stats));
    
    MutexUnlock(&jb->mutex);
    
    LOG_DEBUG("JitterBuffer reset");
}

int JitterBuffer_Put(JitterBuffer* jb, const RtpHeader* rtp,
                     const uint8_t* payload, uint16_t payload_len) {
    if (!jb || !rtp || !payload || payload_len == 0) {
        return -1;
    }
    
    MutexLock(&jb->mutex);
    
    uint64_t now = GetTickCount64Ms();
    
    // 更新抖动
    update_jitter(jb, rtp->timestamp, now);
    
    // 初始化序列号
    if (!jb->seq_initialized) {
        jb->next_seq = rtp->sequence;
        jb->seq_initialized = true;
        jb->base_timestamp = rtp->timestamp;
        jb->base_time = now;
        jb->time_initialized = true;
        LOG_DEBUG("JitterBuffer: seq initialized to %u", rtp->sequence);
    }
    
    // 查找槽
    int slot_idx = find_slot_for_seq(jb, rtp->sequence);
    
    if (slot_idx == -1) {
        // 包太旧
        jb->stats.packets_late++;
        MutexUnlock(&jb->mutex);
        return -2;  // 丢弃
    }
    
    if (slot_idx == -2) {
        // 缓冲区溢出
        jb->stats.overruns++;
        MutexUnlock(&jb->mutex);
        return -3;
    }
    
    JitterSlot* slot = &jb->slots[slot_idx];
    
    // 检查是否重复
    if (slot->state != JB_SLOT_EMPTY && slot->sequence == rtp->sequence) {
        MutexUnlock(&jb->mutex);
        return 0;  // 重复包
    }
    
    // 检查乱序
    if (seq_compare(rtp->sequence, jb->next_seq) != 0 && jb->count > 0) {
        jb->stats.packets_reorder++;
    }
    
    // 填充槽
    slot->state = JB_SLOT_FILLED;
    slot->sequence = rtp->sequence;
    slot->timestamp = rtp->timestamp;
    slot->ssrc = rtp->ssrc;
    slot->payload_len = MIN(payload_len, OPUS_MAX_PACKET);
    memcpy(slot->payload, payload, slot->payload_len);
    slot->recv_time = now;
    slot->decoded_samples = 0;
    
    jb->count++;
    jb->stats.packets_received++;
    
    MutexUnlock(&jb->mutex);
    return 0;
}

int JitterBuffer_Get(JitterBuffer* jb, int16_t* samples, int max_samples) {
    if (!jb || !samples || max_samples < AUDIO_FRAME_SAMPLES) {
        return -1;
    }
    
    MutexLock(&jb->mutex);
    
    if (!jb->seq_initialized) {
        // 还没有收到任何包
        MutexUnlock(&jb->mutex);
        return 0;
    }
    
    uint64_t now = GetTickCount64Ms();
    
    // 检查 head 位置的包是否应该播放
    JitterSlot* slot = &jb->slots[jb->head];
    
    // 快速启动：只需要 1 个包就开始播放（20ms 延迟）
    if (jb->count < 1) {
        MutexUnlock(&jb->mutex);
        return 0;  // 等待数据
    }
    
    // 检查当前槽
    if (slot->state == JB_SLOT_EMPTY) {
        // 期望的包没有到达 - PLC
        jb->stats.packets_lost++;
        jb->stats.underruns++;
        
        int plc_samples = plc_frame(jb, samples, AUDIO_FRAME_SAMPLES);
        
        // 移动到下一个序列号
        jb->next_seq++;
        jb->head = (jb->head + 1) % JITTER_BUFFER_SLOTS;
        
        // 更新丢包率
        if (jb->stats.packets_received > 0) {
            jb->stats.loss_rate = (float)jb->stats.packets_lost / 
                                   (jb->stats.packets_received + jb->stats.packets_lost);
        }
        
        MutexUnlock(&jb->mutex);
        return plc_samples;
    }
    
    // 解码
    if (slot->state == JB_SLOT_FILLED) {
        if (decode_slot(jb, slot) < 0) {
            // 解码失败 - PLC
            int plc_samples = plc_frame(jb, samples, AUDIO_FRAME_SAMPLES);
            
            slot->state = JB_SLOT_EMPTY;
            jb->next_seq++;
            jb->head = (jb->head + 1) % JITTER_BUFFER_SLOTS;
            jb->count--;
            
            MutexUnlock(&jb->mutex);
            return plc_samples;
        }
    }
    
    // 输出解码数据
    int output_samples = MIN(slot->decoded_samples, max_samples);
    memcpy(samples, slot->decoded, output_samples * sizeof(int16_t));
    
    // 清空槽
    slot->state = JB_SLOT_EMPTY;
    jb->next_seq++;
    jb->head = (jb->head + 1) % JITTER_BUFFER_SLOTS;
    jb->count--;
    
    MutexUnlock(&jb->mutex);
    return output_samples;
}

int JitterBuffer_GetLevel(JitterBuffer* jb) {
    if (!jb) return 0;
    
    // 简单计算: 包数 * 帧长
    return jb->count * AUDIO_FRAME_MS;
}

void JitterBuffer_GetStats(JitterBuffer* jb, JitterStats* stats) {
    if (!jb || !stats) return;
    
    MutexLock(&jb->mutex);
    *stats = jb->stats;
    MutexUnlock(&jb->mutex);
}

void JitterBuffer_SetDecoder(JitterBuffer* jb, void* decoder, OpusDecodeFunc decode_func) {
    if (!jb) return;
    
    MutexLock(&jb->mutex);
    jb->decoder = decoder;
    jb->decode_func = decode_func;
    MutexUnlock(&jb->mutex);
}

void JitterBuffer_SetPlc(JitterBuffer* jb, PlcFunc plc_func) {
    if (!jb) return;
    
    MutexLock(&jb->mutex);
    jb->plc_func = plc_func;
    MutexUnlock(&jb->mutex);
}

//=============================================================================
// 多流 Jitter Buffer 实现
//=============================================================================

struct MultiStreamJitterBuffer {
    int             max_streams;
    StreamInfo*     streams;
    JitterConfig    config;
    Mutex           mutex;
    
    // 解码器工厂
    DecoderCreateFunc   decoder_create;
    DecoderDestroyFunc  decoder_destroy;
    OpusDecodeFunc      decode_func;
    PlcFunc             plc_func;
    
    // 混音缓冲区
    int32_t         mix_buffer[AUDIO_FRAME_SAMPLES];
    int16_t         stream_buffer[AUDIO_FRAME_SAMPLES];
};

/**
 * @brief 查找或创建 SSRC 对应的流
 */
static StreamInfo* find_or_create_stream(MultiStreamJitterBuffer* msjb, uint32_t ssrc) {
    StreamInfo* inactive_slot = NULL;
    uint64_t oldest_time = UINT64_MAX;
    StreamInfo* oldest_slot = NULL;
    
    // 先查找已有的流
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (s->active && s->ssrc == ssrc) {
            s->last_active = GetTickCount64Ms();
            return s;
        }
        if (!s->active && !inactive_slot) {
            inactive_slot = s;
        }
        if (s->active && s->last_active < oldest_time) {
            oldest_time = s->last_active;
            oldest_slot = s;
        }
    }
    
    // 使用空闲槽或替换最老的槽
    StreamInfo* slot = inactive_slot ? inactive_slot : oldest_slot;
    if (!slot) return NULL;
    
    // 如果替换已有流，先清理
    if (slot->active) {
        LOG_DEBUG("MultiStreamJB: replacing stream SSRC=%u with SSRC=%u", slot->ssrc, ssrc);
        if (slot->jitter_buffer) {
            JitterBuffer_Destroy(slot->jitter_buffer);
        }
        if (slot->decoder && msjb->decoder_destroy) {
            msjb->decoder_destroy(slot->decoder);
        }
    }
    
    // 创建新流
    slot->ssrc = ssrc;
    slot->jitter_buffer = JitterBuffer_Create(&msjb->config);
    if (!slot->jitter_buffer) {
        return NULL;
    }
    
    // 创建解码器
    if (msjb->decoder_create) {
        slot->decoder = msjb->decoder_create();
        if (slot->decoder) {
            JitterBuffer_SetDecoder(slot->jitter_buffer, slot->decoder, msjb->decode_func);
            JitterBuffer_SetPlc(slot->jitter_buffer, msjb->plc_func);
        }
    }
    
    slot->last_active = GetTickCount64Ms();
    slot->active = true;
    
    LOG_INFO("MultiStreamJB: created stream for SSRC=%u", ssrc);
    return slot;
}

MultiStreamJitterBuffer* MultiStreamJB_Create(int max_streams, const JitterConfig* config) {
    if (max_streams <= 0) max_streams = MAX_CLIENTS;
    
    MultiStreamJitterBuffer* msjb = (MultiStreamJitterBuffer*)calloc(1, sizeof(MultiStreamJitterBuffer));
    if (!msjb) return NULL;
    
    msjb->streams = (StreamInfo*)calloc(max_streams, sizeof(StreamInfo));
    if (!msjb->streams) {
        free(msjb);
        return NULL;
    }
    
    msjb->max_streams = max_streams;
    
    if (config) {
        msjb->config = *config;
    } else {
        msjb->config.min_delay_ms = JITTER_MIN_MS;
        msjb->config.max_delay_ms = JITTER_MAX_MS;
        msjb->config.target_delay_ms = JITTER_BUFFER_MS;
        msjb->config.adaptive = true;
    }
    
    MutexInit(&msjb->mutex);
    
    LOG_INFO("MultiStreamJB created: max_streams=%d", max_streams);
    return msjb;
}

void MultiStreamJB_Destroy(MultiStreamJitterBuffer* msjb) {
    if (!msjb) return;
    
    MutexLock(&msjb->mutex);
    
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (s->active) {
            if (s->jitter_buffer) {
                JitterBuffer_Destroy(s->jitter_buffer);
            }
            if (s->decoder && msjb->decoder_destroy) {
                msjb->decoder_destroy(s->decoder);
            }
            s->active = false;
        }
    }
    
    MutexUnlock(&msjb->mutex);
    MutexDestroy(&msjb->mutex);
    
    free(msjb->streams);
    free(msjb);
    
    LOG_INFO("MultiStreamJB destroyed");
}

void MultiStreamJB_Reset(MultiStreamJitterBuffer* msjb) {
    if (!msjb) return;
    
    MutexLock(&msjb->mutex);
    
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (s->active && s->jitter_buffer) {
            JitterBuffer_Reset(s->jitter_buffer);
        }
    }
    
    MutexUnlock(&msjb->mutex);
    LOG_DEBUG("MultiStreamJB reset");
}

int MultiStreamJB_Put(MultiStreamJitterBuffer* msjb, const RtpHeader* rtp,
                      const uint8_t* payload, uint16_t payload_len) {
    if (!msjb || !rtp || !payload || payload_len == 0) {
        return -1;
    }
    
    MutexLock(&msjb->mutex);
    
    StreamInfo* stream = find_or_create_stream(msjb, rtp->ssrc);
    if (!stream || !stream->jitter_buffer) {
        MutexUnlock(&msjb->mutex);
        return -2;
    }
    
    int result = JitterBuffer_Put(stream->jitter_buffer, rtp, payload, payload_len);
    
    MutexUnlock(&msjb->mutex);
    return result;
}

int MultiStreamJB_GetMixed(MultiStreamJitterBuffer* msjb, int16_t* samples, int max_samples) {
    if (!msjb || !samples || max_samples < AUDIO_FRAME_SAMPLES) {
        return -1;
    }
    
    MutexLock(&msjb->mutex);
    
    // 清空混音缓冲区
    memset(msjb->mix_buffer, 0, sizeof(msjb->mix_buffer));
    
    int active_count = 0;
    int output_samples = 0;
    
    // 从每个活跃流获取音频并混音
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (!s->active || !s->jitter_buffer) continue;
        
        int got = JitterBuffer_Get(s->jitter_buffer, msjb->stream_buffer, AUDIO_FRAME_SAMPLES);
        if (got > 0) {
            // 累加到混音缓冲区
            for (int j = 0; j < got; j++) {
                msjb->mix_buffer[j] += msjb->stream_buffer[j];
            }
            if (got > output_samples) {
                output_samples = got;
            }
            active_count++;
        }
    }
    
    MutexUnlock(&msjb->mutex);
    
    if (output_samples == 0) {
        return 0;
    }
    
    // 软限幅输出
    int out_count = MIN(output_samples, max_samples);
    for (int i = 0; i < out_count; i++) {
        int32_t val = msjb->mix_buffer[i];
        // 软限幅防止爆音
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        samples[i] = (int16_t)val;
    }
    
    return out_count;
}

int MultiStreamJB_GetActiveStreams(MultiStreamJitterBuffer* msjb) {
    if (!msjb) return 0;
    
    int count = 0;
    MutexLock(&msjb->mutex);
    for (int i = 0; i < msjb->max_streams; i++) {
        if (msjb->streams[i].active) {
            count++;
        }
    }
    MutexUnlock(&msjb->mutex);
    return count;
}

void MultiStreamJB_GetStats(MultiStreamJitterBuffer* msjb, JitterStats* stats) {
    if (!msjb || !stats) return;
    
    memset(stats, 0, sizeof(JitterStats));
    
    MutexLock(&msjb->mutex);
    
    int active_count = 0;
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (s->active && s->jitter_buffer) {
            JitterStats stream_stats;
            JitterBuffer_GetStats(s->jitter_buffer, &stream_stats);
            
            stats->packets_received += stream_stats.packets_received;
            stats->packets_lost += stream_stats.packets_lost;
            stats->packets_late += stream_stats.packets_late;
            stats->packets_reorder += stream_stats.packets_reorder;
            stats->underruns += stream_stats.underruns;
            stats->overruns += stream_stats.overruns;
            stats->avg_jitter_ms += stream_stats.avg_jitter_ms;
            active_count++;
        }
    }
    
    if (active_count > 0) {
        stats->avg_jitter_ms /= active_count;
    }
    
    if (stats->packets_received + stats->packets_lost > 0) {
        stats->loss_rate = (float)stats->packets_lost / 
                           (stats->packets_received + stats->packets_lost);
    }
    
    MutexUnlock(&msjb->mutex);
}

void MultiStreamJB_SetDecoderFactory(MultiStreamJitterBuffer* msjb,
                                      DecoderCreateFunc create_func,
                                      DecoderDestroyFunc destroy_func,
                                      OpusDecodeFunc decode_func,
                                      PlcFunc plc_func) {
    if (!msjb) return;
    
    MutexLock(&msjb->mutex);
    msjb->decoder_create = create_func;
    msjb->decoder_destroy = destroy_func;
    msjb->decode_func = decode_func;
    msjb->plc_func = plc_func;
    MutexUnlock(&msjb->mutex);
}

void MultiStreamJB_CleanupInactive(MultiStreamJitterBuffer* msjb, uint32_t timeout_ms) {
    if (!msjb) return;
    
    uint64_t now = GetTickCount64Ms();
    
    MutexLock(&msjb->mutex);
    
    for (int i = 0; i < msjb->max_streams; i++) {
        StreamInfo* s = &msjb->streams[i];
        if (s->active && (now - s->last_active) > timeout_ms) {
            LOG_INFO("MultiStreamJB: cleaning up inactive stream SSRC=%u", s->ssrc);
            
            if (s->jitter_buffer) {
                JitterBuffer_Destroy(s->jitter_buffer);
                s->jitter_buffer = NULL;
            }
            if (s->decoder && msjb->decoder_destroy) {
                msjb->decoder_destroy(s->decoder);
                s->decoder = NULL;
            }
            s->active = false;
        }
    }
    
    MutexUnlock(&msjb->mutex);
}
