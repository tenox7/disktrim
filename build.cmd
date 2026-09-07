@echo off
setlocal enabledelayedexpansion

rem NOTE: The lookup variable is intentionally NOT named "VSINSTALLDIR" (or any other name "VsDevCmd.bat" /
rem "vcvarsall.bat" use internally, e.g. "VCINSTALLDIR", "WindowsSdkDir"). "vswhere.exe" returns the install path
rem WITHOUT a trailing backslash; VS's own scripts assume "VSINSTALLDIR" already has one (since they normally set it
rem themselves) and blindly concatenate onto it. If "VSINSTALLDIR" is defined here, "VsDevCmd.bat" sees it already
rem defined, skips setting it correctly, and every path built from it downstream (VC Tools include/lib, FSharp, etc.)
rem comes out corrupted -- e.g. "...\18\CommunityVC\" instead of "...\18\Community\VC\", which silently drops the MSVC
rem compiler's own include directory from "INCLUDE".
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "_DISKTRIM_VS_PATH=%%i"
)

if not defined _DISKTRIM_VS_PATH (
    echo ERROR: Could not locate a Visual Studio installation with the C++ build tools.
    echo Make sure the "Desktop development with C++" workload is installed.
    exit /b 1
)

set "_VCVARSALL=%_DISKTRIM_VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%_VCVARSALL%" (
    echo ERROR: "vcvarsall.bat" not found at "%_VCVARSALL%"
    exit /b 1
)

rem Pin the build to an installed Windows 11 SDK (build 22000+) rather than letting "vcvarsall.bat" silently fall
rem back to an older Windows 10 SDK when several are installed side by side. Only accept a version that has both
rem its headers AND its import libraries present -- an Include-only folder (e.g. a partially installed or leftover
rem Insider/preview SDK) will find <windows.h> but then fail deeper inside it, or fail at link time.
set "_SDK_INCLUDE_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Include"
set "_SDK_LIB_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Lib"
set "_DISKTRIM_WIN11_SDK_PATH="

if exist "%_SDK_INCLUDE_ROOT%" (
    for /f "delims=" %%v in ('dir /b /ad /o-n "%_SDK_INCLUDE_ROOT%" 2^>nul') do (
        if not defined _DISKTRIM_WIN11_SDK_PATH (
            for /f "tokens=3 delims=." %%b in ("%%v") do (
                set "_SDK_BUILD=%%b"
                if defined _SDK_BUILD if !_SDK_BUILD! GEQ 22000 (
                    if exist "%_SDK_LIB_ROOT%\%%v\um\x64\kernel32.lib" (
                        set "_DISKTRIM_WIN11_SDK_PATH=%%v"
                    ) else (
                        echo Skipping incomplete SDK %%v ^(missing um\x64\kernel32.lib in Lib^)
                    )
                )
            )
        )
    )
)

if not defined _DISKTRIM_WIN11_SDK_PATH (
    echo ERROR: No complete Windows 11 SDK ^(build 22000 or later, with matching Lib files^) found under "%_SDK_INCLUDE_ROOT%"
    echo Install it via the Visual Studio Installer, or from
    echo "https://developer.microsoft.com/windows/downloads/windows-sdk/". If you have a partial/Insider SDK installed,
    echo repair or remove it via "Add or remove programs" and reinstall the standard Windows 11 SDK.
    exit /b 1
)

echo Using Visual Studio at %_DISKTRIM_VS_PATH%
echo Using Windows 11 SDK %_DISKTRIM_WIN11_SDK_PATH%

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64 %_DISKTRIM_WIN11_SDK_PATH%
echo on
del disktrim-x64.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x64.exe disktrim.c disktrim.res
@echo off

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64_x86 %_DISKTRIM_WIN11_SDK_PATH%
echo on
del disktrim-x86.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x86.exe disktrim.c disktrim.res
@echo off

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64_arm64 %_DISKTRIM_WIN11_SDK_PATH%
echo on
del disktrim-arm64.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-arm64.exe disktrim.c disktrim.res
@echo off

pause
