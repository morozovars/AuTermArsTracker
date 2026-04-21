# Static Qt Build For Windows

This document describes the dedicated AuTerm build path for a real single-file
static Qt `AuTerm.exe`. It does not use `windeployqt`, self-extracting
wrappers, DLL deployment, or packaging tricks.

## What This Repository Provides

- The qmake profile `CONFIG+=auterm_static` adds `CONFIG+=static` and the
  `AUTERM_STATIC_QT_BUILD` define.
- The build project remains the top-level `AuTerm-project.pro`.
- `AuTerm/AuTerm.pro` adds the Qt `qwindows` plugin for Windows static builds.
- `AuTerm/main.cpp` conditionally imports `QWindowsIntegrationPlugin`.
- If the static Qt build exposes `style-windowsvista`, the project also adds
  `qwindowsvistastyle` and imports `QWindowsVistaStylePlugin`.
- Existing code in `AuTerm/AutPlugin.cpp` already imports AuTerm internal
  plugins when `QT_STATIC` is defined: `plugin_mcumgr`, `plugin_logger`,
  `plugin_nus_transport`, and `plugin_echo_transport`.

Normal dynamic builds are not changed. The static path is enabled only by
`CONFIG+=static` or `CONFIG+=auterm_static`.

## Qt Modules Used By The Project

The main application in `AuTerm/AuTerm.pro` uses:

- `core`
- `gui`
- `widgets`
- `serialport`
- `network`, unless `SKIPONLINE` is defined

`AuTerm-includes.pri` can add optional modules:

- `bluetooth`, if Qt Connectivity is available and Bluetooth transports are
  enabled
- `network`, if the MCUmgr UDP transport is enabled
- `mqtt`, only if the LoRaWAN transport is manually enabled

Your static Qt build must contain the modules that qmake finds through
`qtHaveModule(...)` for the selected configuration.

## Prerequisites

1. A static Qt build for Windows.

   It must be built with the same compiler, architecture, and runtime choices
   that you use to build AuTerm. Examples:

   - MSVC x64 static Qt + "x64 Native Tools Command Prompt for VS"
   - MinGW x64 static Qt + the matching MinGW toolchain in `PATH`

2. qmake from the static Qt build.

   Do not use qmake from a normal dynamic Qt installation.

3. A make tool.

   The scripts look for `jom`, then `nmake`, then `mingw32-make`. You can also
   set `AUTERM_MAKE_TOOL` explicitly.

4. Static runtime and third-party dependencies, if you need a strict one-file
   executable.

   This is controlled by your toolchain and by how Qt itself was built. The
   repository cannot make a dynamic CRT or dynamic OpenSSL dependency become
   static after the fact.

## Quick Build

Open a compiler-ready command prompt:

- MSVC: "x64 Native Tools Command Prompt for VS"
- MinGW: a shell where the matching compiler and `mingw32-make` are in `PATH`

Point the scripts at static Qt:

```bat
set AUTERM_STATIC_QT_DIR=C:\Qt\5.15.2-static-msvc2019_64
```

Or set qmake explicitly:

```bat
set QMAKE=C:\Qt\5.15.2-static-msvc2019_64\bin\qmake.exe
```

Build the release static configuration:

```bat
scripts\build_static_release_windows.bat
```

The script configures a shadow build in:

```text
build\windows-static-release
```

It invokes qmake like this:

```bat
qmake AuTerm-project.pro -r CONFIG+=release CONFIG+=static CONFIG+=auterm_static
```

With the current `.pro` output rules, the final executable is written to:

```text
release\AuTerm.exe
```

## Configure Only

To generate the Makefile without building:

```bat
scripts\configure_static_qt_windows.bat
```

Then run your make tool manually from `build\windows-static-release`.

Additional arguments are forwarded to qmake:

```bat
scripts\configure_static_qt_windows.bat DEFINES+=SKIPONLINE
```

## Qt Plugins

The project imports only plugins that are required or low-risk for the current
code:

- `QWindowsIntegrationPlugin` (`qwindows`) is required by a static Qt GUI
  application on Windows. Without a platform integration plugin, the
  application usually cannot start.
- `QWindowsVistaStylePlugin` (`qwindowsvistastyle`) is imported only when the
  static Qt build reports `style-windowsvista`. This keeps the regular Windows
  widget style when available, without making the build depend on a missing
  style plugin.

Image format plugins are intentionally not imported. AuTerm currently loads PNG
images from resources, and PNG support is part of QtGui rather than a separate
`imageformats` plugin in normal Qt builds. ICO files are used as Windows
resource icons and are present in the qrc file, but the code does not load them
through `QImageReader`.

If a future static Qt build needs extra plugin libraries passed to qmake, use:

```bat
scripts\configure_static_qt_windows.bat AUTERM_STATIC_QT_EXTRA_PLUGINS="qico qjpeg"
```

For plugins that require an explicit `Q_IMPORT_PLUGIN(...)`, also add the exact
plugin class name in code. Do not add these imports blindly: class names differ
between plugin types and Qt versions.

## TLS, OpenSSL, And Network

AuTerm uses `QNetworkAccessManager`, `QSslSocket`, and HTTPS for online checks
unless `SKIPONLINE` is defined. The MCUmgr LoRaWAN transport, if manually
enabled, also uses encrypted network connections.

For a single executable, TLS depends on how QtNetwork was built:

- QtNetwork can use the Windows Schannel backend. In that case OpenSSL DLLs may
  not be required.
- QtNetwork can be built against OpenSSL that is loaded dynamically. In that
  case `libssl` and `libcrypto` DLLs remain external dependencies.
- QtNetwork can be built with static OpenSSL. In that case OpenSSL must be built
  and linked statically while building Qt itself.
- In Qt 6, TLS backends may be separate plugins. This repository does not import
  them because the required plugin names and classes depend on the exact Qt
  build.

If you need `AuTerm.exe` without OpenSSL DLLs, solve that while building static
Qt: use Schannel or static OpenSSL.

## Verifying The Result

For MSVC:

```bat
dumpbin /dependents release\AuTerm.exe
```

For MinGW:

```bat
objdump -p release\AuTerm.exe | findstr /i "DLL Name"
```

The dependency list should not contain Qt DLLs, for example:

- `Qt5Core.dll` or `Qt6Core.dll`
- `Qt5Gui.dll` or `Qt6Gui.dll`
- `Qt5Widgets.dll` or `Qt6Widgets.dll`
- `Qt5Network.dll` or `Qt6Network.dll`
- `Qt5SerialPort.dll` or `Qt6SerialPort.dll`

Windows system DLLs such as `KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, and
`WS2_32.dll` are normal. If you still see `libssl*.dll`, `libcrypto*.dll`,
`libgcc_s*.dll`, `libstdc++*.dll`, or MSVC runtime DLLs, those parts are still
dynamic and must be handled in the toolchain or static Qt build.

## Limitations

- The scripts do not build static Qt. They use an existing static Qt
  installation.
- Do not mix a static Qt build from one compiler with another compiler.
- A single exe is not possible if Qt or the toolchain was built with dynamic
  runtime, OpenSSL, or third-party dependencies.
- Re-run qmake after changing Qt modules, defines, or plugin settings.
- Check the licensing requirements for static Qt and all statically linked
  dependencies before distributing the result.
