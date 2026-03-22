@echo off
setlocal

echo ========================================
echo  Starting Test Deployment
echo ========================================

REM Define paths
set "BIN_DIR=out\build\x64-Debug"
set "INFO_DIR=info"

REM Check if binaries directory exists
if not exist "%BIN_DIR%" (
    echo [ERROR] Binaries directory not found: %BIN_DIR%
    exit /b 1
)

echo [OK] Binaries directory: %BIN_DIR%

REM Check if freeai.exe exists in binaries directory
if not exist "%BIN_DIR%\freeai.exe" (
    echo [ERROR] freeai.exe not found in %BIN_DIR%!
    exit /b 1
)

echo [OK] freeai.exe found in %BIN_DIR%

REM ========================================
REM Kill only instances 2 and 3 (using PowerShell)
REM ========================================
echo [INFO] Checking for existing instances 2 and 3...

powershell -Command "Get-Process | Where-Object { $_.Path -like '*%BIN_DIR%\2\freeai.exe' } | Stop-Process -Force -ErrorAction SilentlyContinue" 2>nul
echo [OK] Instance 2 processes checked

powershell -Command "Get-Process | Where-Object { $_.Path -like '*%BIN_DIR%\3\freeai.exe' } | Stop-Process -Force -ErrorAction SilentlyContinue" 2>nul
echo [OK] Instance 3 processes checked

REM Small delay to ensure files are unlocked
timeout /T 1 /NOBREAK > nul

REM Create subdirectories if they don't exist
if not exist "%BIN_DIR%\2" mkdir "%BIN_DIR%\2"
if not exist "%BIN_DIR%\3" mkdir "%BIN_DIR%\3"

REM ========================================
REM Copy config files (only if not exists)
REM ========================================

REM config.ini
if not exist "%BIN_DIR%\config.ini" (
    if exist "%INFO_DIR%\config.ini" (
        copy /Y "%INFO_DIR%\config.ini" "%BIN_DIR%\config.ini" > nul
        echo [OK] config.ini copied
    )
) else (
    echo [SKIP] config.ini already exists
)

REM config2.ini
if not exist "%BIN_DIR%\2\config.ini" (
    if exist "%INFO_DIR%\config2.ini" (
        copy /Y "%INFO_DIR%\config2.ini" "%BIN_DIR%\2\config.ini" > nul
        echo [OK] config2.ini copied
    )
) else (
    echo [SKIP] config2.ini already exists
)

REM config3.ini
if not exist "%BIN_DIR%\3\config.ini" (
    if exist "%INFO_DIR%\config3.ini" (
        copy /Y "%INFO_DIR%\config3.ini" "%BIN_DIR%\3\config.ini" > nul
        echo [OK] config3.ini copied
    )
) else (
    echo [SKIP] config3.ini already exists
)

REM ========================================
REM Copy freeai.exe to subdirectories
REM ========================================
echo [INFO] Copying freeai.exe to subdirectory "2"
copy /Y "%BIN_DIR%\freeai.exe" "%BIN_DIR%\2\freeai.exe" > nul
if %ERRORLEVEL% EQU 0 (
    echo [OK] Successfully copied to "2"
) else (
    echo [ERROR] Failed to copy to "2"
)

echo [INFO] Copying freeai.exe to subdirectory "3"
copy /Y "%BIN_DIR%\freeai.exe" "%BIN_DIR%\3\freeai.exe" > nul
if %ERRORLEVEL% EQU 0 (
    echo [OK] Successfully copied to "3"
) else (
    echo [ERROR] Failed to copy to "3"
)

REM ========================================
REM Start Executables
REM ========================================
echo ========================================
echo  Starting Executables
echo ========================================

if exist "%BIN_DIR%\2\freeai.exe" (
    echo [INFO] Starting instance 2
    start "" /D "%BIN_DIR%\2" "freeai.exe"
) else (
    echo [ERROR] Instance 2 executable not found
)

if exist "%BIN_DIR%\3\freeai.exe" (
    echo [INFO] Starting instance 3
    start "" /D "%BIN_DIR%\3" "freeai.exe"
) else (
    echo [ERROR] Instance 3 executable not found
)

echo ========================================
echo  Test Launch Complete
echo ========================================

endlocal