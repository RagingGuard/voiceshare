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
    
    // 计算目标延迟内应该播放的包
    // 检查 head 位置的包是否应该播放
    JitterSlot* slot = &jb->slots[jb->head];
    
    // 自适应: 检查缓冲级别
    int level_ms = JitterBuffer_GetLevel(jb);
    
    // 缓冲区还没达到目标延迟, 继续等待
    if (level_ms < (int)jb->config.target_delay_ms && jb->count < 3) {
        MutexUnlock(&jb->mutex);
        return 0;  // 等待更多数据
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
