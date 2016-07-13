set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
echo on
cl /Fedisktrim-x64.exe disktrim.c

set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64_x86
echo on
cl /Fedisktrim-x86.exe disktrim.c

set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64_arm
echo on
cl /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /Fedisktrim-arm.exe disktrim.c

pause
