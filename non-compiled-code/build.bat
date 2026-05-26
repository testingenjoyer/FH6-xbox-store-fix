@echo off
REM ============================================================
REM  FH6Fix - build d3d12.dll proxy (MinGW-w64 via MSYS2)
REM  Wymagania: MSYS2 z zainstalowanym mingw-w64-x86_64-toolchain
REM  https://www.msys2.org/
REM ============================================================

set MINGW=C:\msys64\mingw64\bin
set GCC=%MINGW%\x86_64-w64-mingw32-g++.exe

if not exist "%GCC%" (
    echo [!] Nie znaleziono MinGW w %MINGW%
    echo     Zainstaluj MSYS2 ze strony https://www.msys2.org/
    echo     Potem w terminalu MSYS2 uruchom:
    echo       pacman -S mingw-w64-x86_64-toolchain
    pause
    exit /b 1
)

echo [*] Kompilacja...

"%GCC%" ^
    -O2 ^
    -shared ^
    -std=c++17 ^
    -o d3d12.dll ^
    d3d12_proxy.cpp ^
    -ld3d12 -ldxgi -lkernel32 ^
    -static-libgcc -static-libstdc++

if exist d3d12.dll (
    echo [+] Sukces: d3d12.dll
) else (
    echo [!] BLAD kompilacji
    pause
    exit /b 1
)

echo.
echo [*] Gotowe! Skopiuj d3d12.dll do folderu z gra.
echo     Xbox Game Pass: C:\XboxGames\Forza Horizon 6\Content\
echo     Steam:          [Steam]\steamapps\common\Forza Horizon 6\
pause
