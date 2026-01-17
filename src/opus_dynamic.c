/**
 * @file opus_dynamic.c
 * @brief Opus动态加载实现
 */

#include "opus_dynamic.h"
#include "dll_loader.h"
#include <stdio.h>

// 全局函数指针定义
opus_encoder_create_fn       p_opus_encoder_create = NULL;
opus_encoder_destroy_fn      p_opus_encoder_destroy = NULL;
opus_encode_fn               p_opus_encode = NULL;
opus_encoder_ctl_fn          p_opus_encoder_ctl = NULL;
opus_decoder_create_fn       p_opus_decoder_create = NULL;
opus_decoder_destroy_fn      p_opus_decoder_destroy = NULL;
opus_decode_fn               p_opus_decode = NULL;
opus_strerror_fn             p_opus_strerror = NULL;
opus_get_version_string_fn   p_opus_get_version_string = NULL;

static int g_opus_loaded = 0;

// 默认错误字符串函数
static const char* default_opus_strerror(int error) {
    (void)error;
    return "Opus not loaded";
}

int opus_dynamic_init(void) {
    if (g_opus_loaded) {
        return 1;
    }
    
    // 从嵌入资源中提取并加载DLL
    if (!extract_and_load_opus_dll()) {
        fprintf(stderr, "Failed to extract opus.dll from resources\n");
        return 0;
    }
    
    HMODULE hModule = get_opus_module();
    if (!hModule) {
        fprintf(stderr, "Failed to get opus module handle\n");
        return 0;
    }
    
    // 加载函数指针
    p_opus_encoder_create = (opus_encoder_create_fn)GetProcAddress(hModule, "opus_encoder_create");
    p_opus_encoder_destroy = (opus_encoder_destroy_fn)GetProcAddress(hModule, "opus_encoder_destroy");
    p_opus_encode = (opus_encode_fn)GetProcAddress(hModule, "opus_encode");
    p_opus_encoder_ctl = (opus_encoder_ctl_fn)GetProcAddress(hModule, "opus_encoder_ctl");
    p_opus_decoder_create = (opus_decoder_create_fn)GetProcAddress(hModule, "opus_decoder_create");
    p_opus_decoder_destroy = (opus_decoder_destroy_fn)GetProcAddress(hModule, "opus_decoder_destroy");
    p_opus_decode = (opus_decode_fn)GetProcAddress(hModule, "opus_decode");
    p_opus_strerror = (opus_strerror_fn)GetProcAddress(hModule, "opus_strerror");
    p_opus_get_version_string = (opus_get_version_string_fn)GetProcAddress(hModule, "opus_get_version_string");
    
    // 检查必要的函数是否都加载成功
    if (!p_opus_encoder_create || !p_opus_encoder_destroy || 
        !p_opus_encode || !p_opus_encoder_ctl ||
        !p_opus_decoder_create || !p_opus_decoder_destroy || !p_opus_decode) {
        fprintf(stderr, "Failed to load required opus functions\n");
        cleanup_opus_dll();
        return 0;
    }
    
    // 如果strerror没加载成功，使用默认实现
    if (!p_opus_strerror) {
        p_opus_strerror = default_opus_strerror;
    }
    
    g_opus_loaded = 1;
    
    if (p_opus_get_version_string) {
        printf("Opus loaded: %s\n", p_opus_get_version_string());
    }
    
    return 1;
}

void opus_dynamic_cleanup(void) {
    if (g_opus_loaded) {
        cleanup_opus_dll();
        
        p_opus_encoder_create = NULL;
        p_opus_encoder_destroy = NULL;
        p_opus_encode = NULL;
        p_opus_encoder_ctl = NULL;
        p_opus_decoder_create = NULL;
        p_opus_decoder_destroy = NULL;
        p_opus_decode = NULL;
        p_opus_strerror = NULL;
        p_opus_get_version_string = NULL;
        
        g_opus_loaded = 0;
    }
}

int opus_is_loaded(void) {
    return g_opus_loaded;
}
