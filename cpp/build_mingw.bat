@echo off
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ nao encontrado no PATH.
    echo Instale o MinGW-w64 ou use o build_msvc.bat.
    exit /b 1
)

echo Compilando recursos...
windres MyFolderMenu.rc -O coff -o MyFolderMenu.res.o
if errorlevel 1 exit /b 1

echo Compilando MyFolderMenu.exe...
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    -DUNICODE -D_UNICODE -finput-charset=UTF-8 ^
    MyFolderMenu.cpp MyFolderMenu.res.o -o MyFolderMenu.exe ^
    -luser32 -lgdi32 -lshell32 -lole32 -lcomdlg32 -ladvapi32 ^
    -lcomctl32 -luxtheme -ldwmapi -lversion
if errorlevel 1 exit /b 1

echo.
echo OK: %~dp0MyFolderMenu.exe
exit /b 0
