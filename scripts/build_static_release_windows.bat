@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build\windows-static-release"

call "%SCRIPT_DIR%configure_static_qt_windows.bat" %*
if errorlevel 1 exit /b %ERRORLEVEL%

if not defined AUTERM_MAKE_TOOL (
    where jom >nul 2>nul
    if not errorlevel 1 (
        set "AUTERM_MAKE_TOOL=jom"
    ) else (
        where nmake >nul 2>nul
        if not errorlevel 1 (
            set "AUTERM_MAKE_TOOL=nmake"
        ) else (
            where mingw32-make >nul 2>nul
            if not errorlevel 1 (
                set "AUTERM_MAKE_TOOL=mingw32-make"
            ) else (
                echo No make tool was found.
                echo Set AUTERM_MAKE_TOOL to jom, nmake, or mingw32-make.
                exit /b 1
            )
        )
    )
)

pushd "%BUILD_DIR%" || exit /b 1

"%AUTERM_MAKE_TOOL%"
set "MAKE_EXIT=%ERRORLEVEL%"

popd
exit /b %MAKE_EXIT%
