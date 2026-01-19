/**
 * @file dll_loader.c
 * @brief 嵌入式DLL加载器实现 - 从资源中提取并加载DLL
 */

#include "dll_loader.h"
#include "resource_ids.h"
#include <stdio.h>
#include <shlobj.h>

static HMODULE g_opus_module = NULL;
static char g_opus_dll_path[MAX_PATH] = {0};
static char g_dll_dir[MAX_PATH] = {0};

// 内部函数：获取DLL目录
static bool ensure_dll_directory(void) {
    if (g_dll_dir[0] != '\0') {
        return true;
    }
    
    char temp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        return false;
    }
    
    snprintf(g_dll_dir, MAX_PATH, "%sSharedVoice", temp_dir);
    CreateDirectoryA(g_dll_dir, NULL);
    return true;
}

// 内部函数：从资源提取DLL
static bool extract_dll_from_resource(int resource_id, const char* dll_name, char* out_path, HMODULE* out_module) {
    if (!ensure_dll_directory()) {
        return false;
    }
    
    snprintf(out_path, MAX_PATH, "%s\\%s", g_dll_dir, dll_name);
    
    // 检查DLL是否已经存在且可用
    *out_module = LoadLibraryA(out_path);
    if (*out_module != NULL) {
        return true;  // 已存在，直接使用
    }
    
    // 从资源中提取DLL
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resource_id), (LPCSTR)RT_RCDATA);
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
    HANDLE hFile = CreateFileA(out_path, GENERIC_WRITE, 0, NULL, 
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DWORD dwWritten;
    BOOL bWriteOK = WriteFile(hFile, pResData, dwResSize, &dwWritten, NULL);
    CloseHandle(hFile);
    
    if (!bWriteOK || dwWritten != dwResSize) {
        DeleteFileA(out_path);
        return false;
    }
    
    // 加载DLL
    *out_module = LoadLibraryA(out_path);
    if (*out_module == NULL) {
        DeleteFileA(out_path);
        return false;
    }
    
    return true;
}

bool extract_and_load_opus_dll(void) {
    if (g_opus_module != NULL) {
        return true;
    }
    return extract_dll_from_resource(IDR_OPUS_DLL, "opus.dll", g_opus_dll_path, &g_opus_module);
}

void cleanup_opus_dll(void) {
    if (g_opus_module != NULL) {
        FreeLibrary(g_opus_module);
        g_opus_module = NULL;
    }
    
    if (g_opus_dll_path[0] != '\0') {
        DeleteFileA(g_opus_dll_path);
        g_opus_dll_path[0] = '\0';
    }
}

HMODULE get_opus_module(void) {
    return g_opus_module;
}
