@echo off
setlocal enabledelayedexpansion

rem This is intentionally NOT named "VSINSTALLDIR"; VS's own scripts assume that name already has a trailing backslash
rem (they normally set it themselves) and corrupt every path built from it downstream if it's pre-set.
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath 2^>nul`) do (
    set "_DISKTRIM_VS_PATH=%%i"
)

if not defined _DISKTRIM_VS_PATH (
    echo ERROR: Could not locate a Visual Studio installation with the x86/x64 AND ARM64 C++ build tools.
    echo Make sure the "Desktop development with C++" workload is installed, along with its "MSVC ... ARM64/ARM64EC
    echo build tools" optional component.
    pause
    exit /b 1
)

set "_VCVARSALL=%_DISKTRIM_VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%_VCVARSALL%" (
    echo ERROR: "vcvarsall.bat" not found at "%_VCVARSALL%"
    pause
    exit /b 1
)

rem Pin the build to an installed Windows 11 SDK (build 22000+) rather than letting "vcvarsall.bat" silently fall back
rem to an older Windows 10 SDK when several are installed side by side. Only accept a version that has both its headers
rem AND its import libraries present -- an Include-only folder (e.g. a partially installed or leftover Insider / preview
rem SDK) will find <windows.h> but then fail deeper inside it, or fail at link time.
set "_SDK_INCLUDE_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Include"
set "_SDK_LIB_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Lib"
set "_DISKTRIM_WIN11_SDK_VERSION="

if exist "%_SDK_INCLUDE_ROOT%" (
    for /f "delims=" %%v in ('dir /b /ad /o-n "%_SDK_INCLUDE_ROOT%" 2^>nul') do (
        if not defined _DISKTRIM_WIN11_SDK_VERSION (
            set "_SDK_BUILD="
            for /f "tokens=3 delims=." %%b in ("%%v") do (
                set "_SDK_BUILD=%%b"
                if defined _SDK_BUILD if !_SDK_BUILD! GEQ 22000 (
                    if exist "%_SDK_LIB_ROOT%\%%v\um\x64\kernel32.lib" (
                        if exist "%_SDK_LIB_ROOT%\%%v\um\x86\kernel32.lib" (
                            if exist "%_SDK_LIB_ROOT%\%%v\um\arm64\kernel32.lib" (
                                set "_DISKTRIM_WIN11_SDK_VERSION=%%v"
                            ) else (
                                echo Skipping incomplete SDK %%v ^(missing "um\arm64\kernel32.lib" in Lib; Install the ARM64 build tools^)
                            )
                        ) else (
                            echo Skipping incomplete SDK %%v ^(missing "um\x86\kernel32.lib" in Lib^)
                        )
                    ) else (
                        echo Skipping incomplete SDK %%v ^(missing "um\x64\kernel32.lib" in Lib^)
                    )
                )
            )
        )
    )
)

if not defined _DISKTRIM_WIN11_SDK_VERSION (
    echo ERROR: No complete Windows 11 SDK ^(build 22000 or later, with matching Lib files^) found under "%_SDK_INCLUDE_ROOT%"
    echo Install it via the Visual Studio Installer, or from
    echo "https://developer.microsoft.com/windows/downloads/windows-sdk/". If you have a partial/Insider SDK installed,
    echo repair or remove it via "Add or remove programs" and reinstall the standard Windows 11 SDK.
    pause
    exit /b 1
)

echo Using Visual Studio at "%_DISKTRIM_VS_PATH%"
echo Using Windows 11 SDK v%_DISKTRIM_WIN11_SDK_VERSION%

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64 %_DISKTRIM_WIN11_SDK_VERSION%
echo on
del disktrim-x64.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x64.exe disktrim.c disktrim.res
@echo off

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64_x86 %_DISKTRIM_WIN11_SDK_VERSION%
echo on
del disktrim-x86.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x86.exe disktrim.c disktrim.res
@echo off

set INCLUDE=
set LIB=
set LIBPATH=
call "%_VCVARSALL%" amd64_arm64 %_DISKTRIM_WIN11_SDK_VERSION%
echo on
del disktrim-arm64.exe disktrim.res 2>nul
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-arm64.exe disktrim.c disktrim.res
@echo off

pause
