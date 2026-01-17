/**
 * @file dll_loader.h
 * @brief 嵌入式DLL加载器 - 从资源中提取并加载DLL
 */

#ifndef DLL_LOADER_H
#define DLL_LOADER_H

#include <windows.h>
#include <stdbool.h>

/**
 * @brief 从资源中提取opus.dll并加载
 * @return 成功返回true，失败返回false
 */
bool extract_and_load_opus_dll(void);

/**
 * @brief 清理并卸载opus.dll
 */
void cleanup_opus_dll(void);

/**
 * @brief 获取opus.dll的HMODULE
 * @return opus.dll的模块句柄
 */
HMODULE get_opus_module(void);

#endif // DLL_LOADER_H
