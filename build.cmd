set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
echo on
del disktrim-x64.exe disktrim.res
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x64.exe disktrim.c disktrim.res

set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64_x86
echo on
del disktrim-x86.exe disktrim.res
rc /D _UNICODE /D UNICODE disktrim.rc
cl /Fedisktrim-x86.exe disktrim.c disktrim.res

set INCLUDE=
set LIB=
set LIBPATH=
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64_arm
echo on
del disktrim-arm.exe disktrim.res
rc /D _UNICODE /D UNICODE disktrim.rc
cl /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /Fedisktrim-arm.exe disktrim.c disktrim.res

pause
