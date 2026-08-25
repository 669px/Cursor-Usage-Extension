# Cursor Usage

<p align="center">
  <img src="icon.png" alt="Cursor Usage" width="64" height="64">
</p>

<p align="center">
  Minimal <strong>GNOME Shell</strong> extension that shows your <a href="https://cursor.com">Cursor</a> usage in the top panel.
</p>

<p align="center">
  <img alt="GNOME Shell" src="https://img.shields.io/badge/GNOME_Shell-46–50-4A86CF?logo=gnome&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/License-MIT-green">
  <a href="https://github.com/669px/Cursor-Usage-Extension/releases"><img alt="GitHub release" src="https://img.shields.io/github/v/release/669px/Cursor-Usage-Extension?include_prereleases&label=release"></a>
</p>

Made by [669px](https://github.com/669px)

---

## Features

- Auto and API usage in the panel and menu
- Plan tier badge
- Billing-cycle reset countdown
- Optional billing line (included + on-demand)
- Preferences for refresh interval, panel layout, proxy, and more

## Requirements

- GNOME Shell **46–50**
- A local Cursor session (CLI login preferred)
- `sqlite3` only if you rely on the Cursor **desktop** app token (not needed for CLI auth)

## Install

### From a release (recommended)

1. Download `cursor-usage@669px.github.io.shell-extension.zip` from the [latest release](https://github.com/669px/Cursor-Usage-Extension/releases/latest).
2. Install and enable:

```bash
gnome-extensions install -f cursor-usage@669px.github.io.shell-extension.zip
gnome-extensions enable cursor-usage@669px.github.io
```

3. Reload GNOME Shell:
   - **Wayland** — log out and back in
   - **X11** — `Alt+F2`, type `r`, Enter

### From source

```bash
git clone https://github.com/669px/Cursor-Usage-Extension.git
cd Cursor-Usage-Extension
./update
gnome-extensions enable cursor-usage@669px.github.io
```

Same reload step as above (Wayland logout, or X11 `r`).

On Wayland, `gnome-extensions enable` may say the extension does not exist until you log out and back in — that is expected. After login it should load if it is enabled.

### Pack + install

Useful when you want the same bundle shape as extensions.gnome.org:

```bash
./pack
gnome-extensions install -f cursor-usage@669px.github.io.shell-extension.zip
gnome-extensions enable cursor-usage@669px.github.io
```

Then reload Shell (Wayland logout / X11 `r`).

## Auth

The extension does **not** spawn the Cursor CLI. It reads a session token already saved locally, then calls Cursor’s usage API:

| Priority | Source |
| --- | --- |
| 1 | `~/.config/cursor/auth.json` — Cursor Agent / CLI login |
| 2 | `CURSOR_SESSION_TOKEN` — optional environment override |
| 3 | `~/.config/Cursor/User/globalStorage/state.vscdb` — desktop app (`sqlite3`) |

Sign in with the Cursor CLI / Agent first so `auth.json` exists. Usage endpoint: `https://cursor.com/api/usage-summary`.

## Preferences

Open from the extension menu (**Prefs**) or GNOME Extensions:

- Refresh interval
- Ring vs percentage panel display
- Which pool the panel shows (most used / auto / API / total)
- Used vs remaining values
- Show/hide icon, plan tier, and billing line
- Optional HTTP proxy URL

## Windows widget

A native Win32 desktop widget lives in [`windows/`](windows/). Same usage API and auth idea, as a floating card with a tray icon.

See [windows/README.md](windows/README.md) for build steps (CMake + MSVC or MinGW).

## Disclaimer

Not affiliated with, funded by, or associated with Cursor or Anysphere.

## License

[MIT](LICENSE)
