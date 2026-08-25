# AI Usage (Windows widget)

Floating desktop widget for Windows showing **Cursor**, **Claude**, and **Codex** usage.

## Features

- Compact pill (ring + %) that expands to all enabled providers
- Cursor Auto/API, Claude 5h/7d, Codex primary/weekly
- Tray icon, drag + edge snap, always-on-top, opacity
- Prefs for which providers to show and which drives the compact %

## Auth

| Provider | Source |
| --- | --- |
| Cursor | `%USERPROFILE%\.cursor\auth.json`, `CURSOR_SESSION_TOKEN`, or `%APPDATA%\Cursor\...\state.vscdb` |
| Claude | `%USERPROFILE%\.claude\.credentials.json` or `CLAUDE_CODE_OAUTH_TOKEN` |
| Codex | `%USERPROFILE%\.codex\auth.json` |

## Build

```powershell
cd windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

MinGW cross-compile from Linux:

```bash
cd windows
cmake -B build-mingw -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

## License

MIT
