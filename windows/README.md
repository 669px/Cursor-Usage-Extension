# Cursor Usage (Windows widget)

Floating desktop widget for Windows that shows your [Cursor](https://cursor.com) Auto / API usage. Same idea as the GNOME Shell extension, built with Win32 + GDI+.

## Features

- Compact pill (ring + %) that expands into full Auto / API meters
- Plan tier, reset countdown, optional billing line
- Draggable, edge-snapping, always-on-top, opacity
- Per-monitor DPI awareness
- System tray icon that mirrors usage severity
- Single instance (launching again focuses the existing widget)
- Preferences for refresh interval, pool, remaining %, compact start, tray start, proxy

## Requirements

- Windows 10 or later
- Visual Studio 2019+ (or Build Tools) with C++ workload, **or** MinGW-w64 with a recent g++
- CMake 3.16+
- A local Cursor session (CLI login preferred)
- `sqlite3.exe` on `PATH` only if you rely on the Cursor **desktop** app token (not needed for CLI auth)

## Build

```powershell
cd windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Binary: `build\Release\CursorUsage.exe`

A prebuilt MinGW cross-compile is also produced at `windows/dist/CursorUsage.exe` when building from Linux with:

```bash
cd windows
cmake -B build-mingw -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

## Use

- Click the compact pill to expand
- Drag to move (snaps to screen edges)
- Use ▾ / ▴ to toggle compact / expanded
- × hides to tray
- Tray left-click shows the widget; right-click for menu

## Auth

The widget does **not** spawn the Cursor CLI. It reads a session token already saved locally, then calls `https://cursor.com/api/usage-summary`.

| Priority | Source |
| --- | --- |
| 1 | `CURSOR_SESSION_TOKEN` environment variable |
| 2 | `%USERPROFILE%\.cursor\auth.json` (CLI / Agent) |
| 3 | `%APPDATA%\cursor\auth.json` / `%LOCALAPPDATA%\cursor\auth.json` |
| 4 | `%APPDATA%\Cursor\User\globalStorage\state.vscdb` via `sqlite3` |

Sign in with the Cursor CLI / Agent first so `auth.json` exists.

## Config

Saved to `%APPDATA%\CursorUsage\config.ini`.

## Disclaimer

Not affiliated with, funded by, or associated with Cursor or Anysphere.

## License

MIT (same as the rest of this repository).
