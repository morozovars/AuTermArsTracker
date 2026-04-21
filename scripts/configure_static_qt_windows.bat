@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build\windows-static-release"

if not defined QMAKE (
    if defined AUTERM_STATIC_QT_DIR (
        set "QMAKE=%AUTERM_STATIC_QT_DIR%\bin\qmake.exe"
    ) else if defined QTDIR (
        set "QMAKE=%QTDIR%\bin\qmake.exe"
    ) else (
        set "QMAKE=qmake"
    )
)

where "%QMAKE%" >nul 2>nul
if errorlevel 1 (
    if not exist "%QMAKE%" (
        echo qmake was not found.
        echo Set QMAKE to the full path of the static Qt qmake.exe,
        echo or set AUTERM_STATIC_QT_DIR to the static Qt install prefix.
        exit /b 1
    )
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b 1

pushd "%BUILD_DIR%" || exit /b 1

"%QMAKE%" "%ROOT_DIR%\AuTerm-project.pro" -r CONFIG+=release CONFIG+=static CONFIG+=auterm_static %*
set "QMAKE_EXIT=%ERRORLEVEL%"

popd
exit /b %QMAKE_EXIT%
