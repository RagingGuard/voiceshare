/**
 * @file dll_loader.c
 * @brief 嵌入式DLL加载器实现 - 从资源中提取并加载DLL
 */

#include "dll_loader.h"
#include "../res/resource.h"
#include <stdio.h>
#include <shlobj.h>

static HMODULE g_opus_module = NULL;
static char g_dll_path[MAX_PATH] = {0};

bool extract_and_load_opus_dll(void) {
    // 如果已经加载，直接返回
    if (g_opus_module != NULL) {
        return true;
    }
    
    // 获取临时目录
    char temp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        return false;
    }
    
    // 创建一个子目录用于存放DLL
    char dll_dir[MAX_PATH];
    snprintf(dll_dir, MAX_PATH, "%sSharedVoice", temp_dir);
    CreateDirectoryA(dll_dir, NULL);
    
    // 构建DLL路径
    snprintf(g_dll_path, MAX_PATH, "%s\\opus.dll", dll_dir);
    
    // 检查DLL是否已经存在且可用
    g_opus_module = LoadLibraryA(g_dll_path);
    if (g_opus_module != NULL) {
        // 验证DLL是否有效 - 检查导出函数
        if (GetProcAddress(g_opus_module, "opus_encoder_create") != NULL) {
            return true;
        }
        // DLL无效，重新提取
        FreeLibrary(g_opus_module);
        g_opus_module = NULL;
    }
    
    // 从资源中提取DLL
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_OPUS_DLL), (LPCSTR)RT_RCDATA);
    if (hRes == NULL) {
        return false;
    }
    
    HGLOBAL hResData = LoadResource(NULL, hRes);
    if (hResData == NULL) {
        return false;
    }
    
    void* pResData = LockResource(hResData);
    if (pResData == NULL) {
        return false;
    }
    
    DWORD dwResSize = SizeofResource(NULL, hRes);
    if (dwResSize == 0) {
        return false;
    }
    
    // 写入到临时文件
    HANDLE hFile = CreateFileA(g_dll_path, GENERIC_WRITE, 0, NULL, 
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DWORD dwWritten;
    BOOL bWriteOK = WriteFile(hFile, pResData, dwResSize, &dwWritten, NULL);
    CloseHandle(hFile);
    
    if (!bWriteOK || dwWritten != dwResSize) {
        DeleteFileA(g_dll_path);
        return false;
    }
    
    // 加载DLL
    g_opus_module = LoadLibraryA(g_dll_path);
    if (g_opus_module == NULL) {
        DeleteFileA(g_dll_path);
        return false;
    }
    
    return true;
}

void cleanup_opus_dll(void) {
    if (g_opus_module != NULL) {
        FreeLibrary(g_opus_module);
        g_opus_module = NULL;
    }
    
    // 尝试删除临时文件（可能会失败如果还在使用）
    if (g_dll_path[0] != '\0') {
        DeleteFileA(g_dll_path);
        g_dll_path[0] = '\0';
    }
}

HMODULE get_opus_module(void) {
    return g_opus_module;
}
