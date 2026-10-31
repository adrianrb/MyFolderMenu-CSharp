@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe nao encontrado.
    echo Abra o "x64 Native Tools Command Prompt for VS" e execute este script novamente.
    exit /b 1
)

echo Compilando recursos...
rc /nologo /fo MyFolderMenu.res MyFolderMenu.rc
if errorlevel 1 exit /b 1

echo Compilando MyFolderMenu.exe...
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W4 /DUNICODE /D_UNICODE ^
   MyFolderMenu.cpp MyFolderMenu.res ^
   /Fe:MyFolderMenu.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   user32.lib gdi32.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib ^
   comctl32.lib uxtheme.lib dwmapi.lib version.lib
if errorlevel 1 exit /b 1

echo.
echo OK: %~dp0MyFolderMenu.exe
exit /b 0
