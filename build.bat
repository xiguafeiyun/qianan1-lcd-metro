@echo off
rem ============================================================
rem  metro-lcd 一键编译（Ninja 快编，命令行用）
rem  想用 Visual Studio 按 F5：直接用 VS 打开本文件夹即可，
rem  不用跑这个脚本（预设已在 CMakePresets.json 里）
rem ============================================================
setlocal

set "VS=D:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VS%\VC\Auxiliary\Build\vcvars64.bat" set "VS=D:\Program Files\Microsoft Visual Studio\2022\Enterprise"

if not exist "%VS%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [X] 找不到 vcvars64.bat，检查 VS 路径
    pause & exit /b 1
)

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo [X] vcvars64 初始化失败 & pause & exit /b 1 )

rem CMake / Ninja 是 VS 自带的，显式加进 PATH
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;D:\Qt\6.8.3\msvc2022_64\bin;%PATH%"

pushd "%~dp0"

echo [1/2] CMake 配置...
cmake --preset ninja-release
if errorlevel 1 ( echo [X] 配置失败 & popd & pause & exit /b 1 )

echo [2/2] 编译（Qt 运行库会自动拷过去）...
cmake --build --preset ninja-release
if errorlevel 1 ( echo [X] 编译失败 & popd & pause & exit /b 1 )

popd
echo.
echo [OK] %~dp0build\metro-lcd.exe
pause
