# AI Usage (GNOME + Windows)

Show **Cursor**, **Claude**, and **Codex** usage from local sessions.

## GNOME Shell

Minimal top-panel extension for GNOME Shell 46–50.

### Features

- Cursor Auto / API meters
- Claude 5h / 7d OAuth usage
- Codex primary / weekly rate limits
- Plan tiers, reset countdown, optional billing/credits
- Prefs for providers, panel target, refresh, proxy

### Auth (local files only — no CLI spawn)

| Provider | Source |
| --- | --- |
| Cursor | `~/.config/cursor/auth.json`, `CURSOR_SESSION_TOKEN`, or desktop `state.vscdb` |
| Claude | `~/.claude/.credentials.json` or `CLAUDE_CODE_OAUTH_TOKEN` |
| Codex | `~/.codex/auth.json` |

### Install

From [releases](https://github.com/669px/AI-Usage-Extension/releases):

```bash
gnome-extensions install -f cursor-usage@669px.github.io.shell-extension.zip
gnome-extensions enable cursor-usage@669px.github.io
```

Reload Shell (Wayland logout, or X11 `Alt+F2` → `r`).

## Windows widget

Floating Win32 card + tray icon. Same providers and auth idea.

<<<<<<< HEAD
See [windows/README.md](windows/README.md) for build steps (CMake + MSVC or MinGW).
=======
Binary: `CursorUsage.exe` on the release page, or build under [`windows/`](windows/).
>>>>>>> 5ffb4f0 (Point docs at the renamed AI-Usage-Extension repository.)

## Disclaimer

Not affiliated with Cursor, Anthropic, or OpenAI.

## License

[MIT](LICENSE)
