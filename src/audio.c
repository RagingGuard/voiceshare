/**
 * @file audio.c
 * @brief 音频引擎实现 (Windows WaveIn/WaveOut)
 */

#include "audio.h"
#include "audio_dsp.h"

//=============================================================================
// 内部常量
//=============================================================================
#define WAVE_BUFFER_COUNT   4
#define WAVE_BUFFER_SIZE    AUDIO_FRAME_BYTES

//=============================================================================
// 内部状态
//=============================================================================
typedef struct {
    // 采集
    HWAVEIN     hWaveIn;
    WAVEHDR     waveInHdr[WAVE_BUFFER_COUNT];
    char        waveInBuffer[WAVE_BUFFER_COUNT][WAVE_BUFFER_SIZE];
    bool        capturing;
    AudioCaptureCallback captureCallback;
    void*       captureUserdata;
    bool        captureMute;
    float       captureVolume;
    float       captureLevel;
    
    // 播放
    HWAVEOUT    hWaveOut;
    WAVEHDR     waveOutHdr[WAVE_BUFFER_COUNT];
    char        waveOutBuffer[WAVE_BUFFER_COUNT][WAVE_BUFFER_SIZE];
    bool        playing;
    float       playbackVolume;
    float       playbackLevel;
    int         waveOutBufIndex;
    
    // 播放队列
    Mutex       playbackMutex;
    int16_t     playbackQueue[AUDIO_SAMPLE_RATE];  // 1秒缓冲
    int         playbackQueueHead;
    int         playbackQueueTail;
    int         playbackQueueSize;
    
    // DSP 处理器 (用于采集端噪声门限)
    AudioDsp*   captureDsp;
    bool        captureDspEnabled;
    
    bool        initialized;
} AudioState;

static AudioState g_audio = {0};

//=============================================================================
// 内部函数
//=============================================================================

static void CALLBACK WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
                                 DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WIM_DATA) {
        WAVEHDR* hdr = (WAVEHDR*)dwParam1;
        
        if (g_audio.capturing && hdr->dwBytesRecorded > 0) {
            int16_t* samples = (int16_t*)hdr->lpData;
            int count = hdr->dwBytesRecorded / sizeof(int16_t);
            
            // DSP 处理 (噪声门限)
            float dsp_gain = 1.0f;
            if (g_audio.captureDspEnabled && g_audio.captureDsp) {
                AudioAnalysis analysis;
                dsp_gain = AudioDsp_Process(g_audio.captureDsp, samples, count, &analysis);
                g_audio.captureLevel = AudioDsp_DbToLinear(analysis.rms_db);
            } else {
                // 计算电平 (原有方式)
                float level = 0;
                for (int i = 0; i < count; i++) {
                    float s = (float)abs(samples[i]) / 32768.0f;
                    if (s > level) level = s;
                }
                g_audio.captureLevel = level;
            }
            
            // 应用音量
            if (!g_audio.captureMute && g_audio.captureVolume != 1.0f) {
                for (int i = 0; i < count; i++) {
                    float s = samples[i] * g_audio.captureVolume;
                    samples[i] = (int16_t)CLAMP(s, -32768, 32767);
                }
            }
            
            // 回调
            if (g_audio.captureCallback && !g_audio.captureMute) {
                g_audio.captureCallback(samples, count, g_audio.captureUserdata);
            }
        }
        
        // 重新提交缓冲区
        if (g_audio.capturing) {
            waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
        }
    }
}

static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance,
                                  DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    // 可用于跟踪播放完成
}

//=============================================================================
// 公共接口
//=============================================================================

bool Audio_Init(void) {
    if (g_audio.initialized) return true;
    
    memset(&g_audio, 0, sizeof(g_audio));
    MutexInit(&g_audio.playbackMutex);
    g_audio.captureVolume = 1.0f;
    g_audio.playbackVolume = 1.0f;
    
    // 创建采集端 DSP 处理器
    AudioDspConfig dsp_config;
    AudioDsp_GetDefaultConfig(&dsp_config);
    g_audio.captureDsp = AudioDsp_Create(&dsp_config);
    g_audio.captureDspEnabled = true;  // 默认启用
    
    g_audio.initialized = true;
    
    LOG_INFO("Audio engine initialized (DSP enabled)");
    return true;
}

void Audio_Shutdown(void) {
    if (!g_audio.initialized) return;
    
    Audio_StopCapture();
    Audio_StopPlayback();
    
    // 销毁 DSP 处理器
    if (g_audio.captureDsp) {
        AudioDsp_Destroy(g_audio.captureDsp);
        g_audio.captureDsp = NULL;
    }
    
    MutexDestroy(&g_audio.playbackMutex);
    g_audio.initialized = false;
    
    LOG_INFO("Audio engine shutdown");
}

bool Audio_StartCapture(AudioCaptureCallback callback, void* userdata) {
    if (!g_audio.initialized || g_audio.capturing) return false;
    
    g_audio.captureCallback = callback;
    g_audio.captureUserdata = userdata;
    
    // 设置波形格式
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = AUDIO_CHANNELS;
    wfx.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    wfx.wBitsPerSample = AUDIO_BITS;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    
    // 打开输入设备
    MMRESULT result = waveInOpen(&g_audio.hWaveIn, WAVE_MAPPER, &wfx,
                                  (DWORD_PTR)WaveInProc, 0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        LOG_ERROR("Failed to open wave input: %d", result);
        return false;
    }
    
    // 准备缓冲区
    for (int i = 0; i < WAVE_BUFFER_COUNT; i++) {
        WAVEHDR* hdr = &g_audio.waveInHdr[i];
        hdr->lpData = g_audio.waveInBuffer[i];
        hdr->dwBufferLength = WAVE_BUFFER_SIZE;
        hdr->dwFlags = 0;
        
        waveInPrepareHeader(g_audio.hWaveIn, hdr, sizeof(WAVEHDR));
        waveInAddBuffer(g_audio.hWaveIn, hdr, sizeof(WAVEHDR));
    }
    
    // 开始采集
    g_audio.capturing = true;
    waveInStart(g_audio.hWaveIn);
    
    LOG_INFO("Audio capture started");
    return true;
}

void Audio_StopCapture(void) {
    if (!g_audio.capturing) return;
    
    g_audio.capturing = false;
    
    waveInStop(g_audio.hWaveIn);
    waveInReset(g_audio.hWaveIn);
    
    for (int i = 0; i < WAVE_BUFFER_COUNT; i++) {
        waveInUnprepareHeader(g_audio.hWaveIn, &g_audio.waveInHdr[i], sizeof(WAVEHDR));
    }
    
    waveInClose(g_audio.hWaveIn);
    g_audio.hWaveIn = NULL;
    
    LOG_INFO("Audio capture stopped");
}

bool Audio_StartPlayback(void) {
    if (!g_audio.initialized || g_audio.playing) return false;
    
    // 设置波形格式
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = AUDIO_CHANNELS;
    wfx.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    wfx.wBitsPerSample = AUDIO_BITS;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    
    // 打开输出设备
    MMRESULT result = waveOutOpen(&g_audio.hWaveOut, WAVE_MAPPER, &wfx,
                                   (DWORD_PTR)WaveOutProc, 0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        LOG_ERROR("Failed to open wave output: %d", result);
        return false;
    }
    
    // 准备缓冲区
    for (int i = 0; i < WAVE_BUFFER_COUNT; i++) {
        WAVEHDR* hdr = &g_audio.waveOutHdr[i];
        hdr->lpData = g_audio.waveOutBuffer[i];
        hdr->dwBufferLength = WAVE_BUFFER_SIZE;
        hdr->dwFlags = 0;
        
        waveOutPrepareHeader(g_audio.hWaveOut, hdr, sizeof(WAVEHDR));
    }
    
    g_audio.playing = true;
    g_audio.waveOutBufIndex = 0;
    g_audio.playbackQueueHead = 0;
    g_audio.playbackQueueTail = 0;
    g_audio.playbackQueueSize = 0;
    
    LOG_INFO("Audio playback started");
    return true;
}

void Audio_StopPlayback(void) {
    if (!g_audio.playing) return;
    
    g_audio.playing = false;
    
    waveOutReset(g_audio.hWaveOut);
    
    for (int i = 0; i < WAVE_BUFFER_COUNT; i++) {
        waveOutUnprepareHeader(g_audio.hWaveOut, &g_audio.waveOutHdr[i], sizeof(WAVEHDR));
    }
    
    waveOutClose(g_audio.hWaveOut);
    g_audio.hWaveOut = NULL;
    
    LOG_INFO("Audio playback stopped");
}

bool Audio_SubmitPlaybackData(const int16_t* samples, int count) {
    if (!g_audio.playing || !samples || count <= 0) return false;
    
    // 找一个可用的缓冲区
    WAVEHDR* hdr = &g_audio.waveOutHdr[g_audio.waveOutBufIndex];
    
    // 等待缓冲区可用
    int timeout = 100;
    while ((hdr->dwFlags & WHDR_INQUEUE) && timeout-- > 0) {
        Sleep(1);
    }
    
    if (hdr->dwFlags & WHDR_INQUEUE) {
        return false;  // 超时
    }
    
    // 复制数据
    int bytes = count * sizeof(int16_t);
    if (bytes > WAVE_BUFFER_SIZE) bytes = WAVE_BUFFER_SIZE;
    memcpy(hdr->lpData, samples, bytes);
    hdr->dwBufferLength = bytes;
    
    // 应用音量
    int16_t* buf = (int16_t*)hdr->lpData;
    int bufCount = bytes / sizeof(int16_t);
    
    float level = 0;
    for (int i = 0; i < bufCount; i++) {
        float s = buf[i] * g_audio.playbackVolume;
        buf[i] = (int16_t)CLAMP(s, -32768, 32767);
        float abs_s = (float)abs(buf[i]) / 32768.0f;
        if (abs_s > level) level = abs_s;
    }
    g_audio.playbackLevel = level;
    
    // 提交播放
    waveOutWrite(g_audio.hWaveOut, hdr, sizeof(WAVEHDR));
    
    g_audio.waveOutBufIndex = (g_audio.waveOutBufIndex + 1) % WAVE_BUFFER_COUNT;
    return true;
}

void Audio_SetCaptureMute(bool mute) {
    g_audio.captureMute = mute;
}

bool Audio_GetCaptureMute(void) {
    return g_audio.captureMute;
}

void Audio_SetCaptureVolume(float volume) {
    g_audio.captureVolume = CLAMP(volume, 0.0f, 2.0f);
}

void Audio_SetPlaybackVolume(float volume) {
    g_audio.playbackVolume = CLAMP(volume, 0.0f, 2.0f);
}

float Audio_GetCaptureLevel(void) {
    return g_audio.captureLevel;
}

float Audio_GetPlaybackLevel(void) {
    return g_audio.playbackLevel;
}

int Audio_EnumCaptureDevices(char names[][64], int max_count) {
    int count = waveInGetNumDevs();
    int result = 0;
    
    for (int i = 0; i < count && result < max_count; i++) {
        WAVEINCAPSW caps;
        if (waveInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, names[result], 64, NULL, NULL);
            result++;
        }
    }
    
    return result;
}

int Audio_EnumPlaybackDevices(char names[][64], int max_count) {
    int count = waveOutGetNumDevs();
    int result = 0;
    
    for (int i = 0; i < count && result < max_count; i++) {
        WAVEOUTCAPSW caps;
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, names[result], 64, NULL, NULL);
            result++;
        }
    }
    
    return result;
}

void Audio_Mix(int16_t* output, const int16_t** inputs, int input_count, int sample_count) {
    if (!output || !inputs || input_count <= 0 || sample_count <= 0) return;
    
    for (int i = 0; i < sample_count; i++) {
        int32_t sum = 0;
        for (int j = 0; j < input_count; j++) {
            if (inputs[j]) {
                sum += inputs[j][i];
            }
        }
        // 软限幅
        output[i] = (int16_t)CLAMP(sum, -32768, 32767);
    }
}

void Audio_EnableCaptureDsp(bool enable) {
    g_audio.captureDspEnabled = enable;
    if (g_audio.captureDsp) {
        AudioDsp_Reset(g_audio.captureDsp);
    }
    LOG_INFO("Audio capture DSP %s", enable ? "enabled" : "disabled");
}

bool Audio_IsCaptureDspEnabled(void) {
    return g_audio.captureDspEnabled;
}
