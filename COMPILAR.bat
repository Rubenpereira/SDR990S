@echo off
chcp 65001 >nul 2>&1
echo.
echo SDR-990S - compilar (roda no Windows 7 ate 11, sem instalar)
echo =============================================================
echo.
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set VS_PATH=%%i
if not defined VS_PATH (
  echo [ERRO] Visual Studio / Build Tools nao encontrado!
  if /i not "%1"=="nopause" pause
  exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :falhou
cmake --build . --config Release
if errorlevel 1 goto :falhou

rem --- pasta pronta para usar/levar: SDR990S.exe + DLLs do dongle + decoders ---
set PRONTO=%~dp0pronto
if not exist "%PRONTO%" mkdir "%PRONTO%"
copy /y SDR990S.exe "%PRONTO%\" >nul
for %%d in (librtlsdr.dll rtlsdr.dll libusb-1.0.dll pthreadVC2.dll) do (
  if not exist "%PRONTO%\%%d" if exist "%~dp0third_party\librtlsdr\lib\%%d" copy /y "%~dp0third_party\librtlsdr\lib\%%d" "%PRONTO%\" >nul
)
rem --- as DLLs tambem ao lado do exe da pasta build (se alguem abrir por la, o radio abre) ---
for %%d in (librtlsdr.dll rtlsdr.dll libusb-1.0.dll pthreadVC2.dll) do (
  if exist "%~dp0third_party\librtlsdr\lib\%%d" copy /y "%~dp0third_party\librtlsdr\lib\%%d" "%~dp0build\" >nul
)
if not exist "%PRONTO%\bookmarks.json" copy /y "%~dp0bookmarks.json" "%PRONTO%\" >nul
for %%f in (LICENSE.txt LICENSE-DSD.txt LEIA-ME.txt TERCEIROS.txt) do if exist "%~dp0%%f" copy /y "%~dp0%%f" "%PRONTO%\" >nul
echo.
echo [OK] Pronto: %PRONTO%\SDR990S.exe
echo      E so copiar a pasta "pronto" para qualquer PC e rodar o SDR990S.exe
echo.
if /i not "%1"=="nopause" pause
exit /b 0

:falhou
echo.
echo [ERRO] A compilacao falhou - veja as mensagens acima.
if /i not "%1"=="nopause" pause
exit /b 1
