@echo off
rem 在 "x64 Native Tools Command Prompt for VS" 里运行这个脚本
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    echo [错误] 找不到 cl.exe。
    echo 请打开 "x64 Native Tools Command Prompt for VS" 再运行本脚本。
    exit /b 1
)

cl /nologo /std:c++17 /EHsc /O2 /utf-8 snake.cpp /Fe:snake.exe
if errorlevel 1 (
    echo [错误] 编译失败。
    exit /b 1
)

echo.
echo 编译完成: snake.exe
echo 运行: snake.exe    自测: snake.exe --selftest
