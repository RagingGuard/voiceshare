@echo off
chcp 65001 >nul
setlocal

echo ========================================
echo   SharedVoice 构建脚本
echo ========================================
echo.

:: 设置变量
set "PROJECT_DIR=%~dp0"
set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
set "DESKTOP=%USERPROFILE%\Desktop"

:: 检查 MSBuild 是否存在
if not exist "%MSBUILD_PATH%" (
    echo [错误] 未找到 MSBuild，请检查 Visual Studio 是否正确安装
    echo 路径: %MSBUILD_PATH%
    pause
    exit /b 1
)

:: 切换到项目目录
cd /d "%PROJECT_DIR%"

echo [1/4] 清理旧的编译文件...
if exist "bin" rd /s /q "bin"
if exist "obj" rd /s /q "obj"
echo      完成

echo.
echo [2/4] 编译 Release x64 版本...
"%MSBUILD_PATH%" SharedVoice.sln /p:Configuration=Release /p:Platform=x64 /t:Build /v:minimal
if errorlevel 1 (
    echo [错误] 编译失败！
    pause
    exit /b 1
)
echo      编译成功

echo.
echo [3/4] 复制到桌面...
if exist "bin\x64\Release\SharedVoice.exe" (
    copy /y "bin\x64\Release\SharedVoice.exe" "%DESKTOP%\SharedVoice.exe" >nul
    echo      已复制到: %DESKTOP%\SharedVoice.exe
) else (
    echo [错误] 未找到生成的 exe 文件
    pause
    exit /b 1
)

echo.
echo [4/4] 清理编译中间文件...
if exist "bin" rd /s /q "bin"
if exist "obj" rd /s /q "obj"
echo      完成

echo.
echo ========================================
echo   构建完成！
echo   可执行文件: %DESKTOP%\SharedVoice.exe
echo ========================================
echo.

pause
