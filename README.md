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

- Auto and API usage percentages in the panel and menu
- Plan tier badge
- Billing-cycle reset countdown
- Optional billing line (included + on-demand)
- Preferences for refresh interval, panel layout, proxy, and more

## Requirements

- GNOME Shell **46–50**
- A local Cursor session (CLI login preferred)
- `sqlite3` only if you rely on the Cursor **desktop** app token (not needed for CLI auth)

## Install

### From source

```bash
git clone https://github.com/669px/Cursor-Usage-Extension.git
cd Cursor-Usage-Extension
./update
gnome-extensions enable cursor-usage@669px.github.io
```

Reload GNOME Shell afterward:

- **Wayland** — log out and back in
- **X11** — `Alt+F2`, type `r`, Enter

### Manual

```bash
uuid="cursor-usage@669px.github.io"
install_dir="$HOME/.local/share/gnome-shell/extensions/$uuid"
rm -rf "$install_dir"
mkdir -p "$(dirname "$install_dir")"
cp -rT "$PWD" "$install_dir"
rm -rf "$install_dir/.git"
glib-compile-schemas "$install_dir/schemas"
gnome-extensions enable "$uuid"
```

## Auth

The extension does **not** spawn the Cursor CLI. It reads a session token already saved locally, then calls Cursor’s usage API:

| Priority | Source |
| --- | --- |
| 1 | `~/.config/cursor/auth.json` — Cursor Agent / CLI login |
| 2 | `CURSOR_SESSION_TOKEN` — optional environment override |
| 3 | `~/.config/Cursor/User/globalStorage/state.vscdb` — desktop app (`sqlite3`) |

Usage endpoint: `https://cursor.com/api/usage-summary`

## Preferences

Open from the extension menu (**Prefs**) or GNOME Extensions:

- Refresh interval
- Ring vs percentage panel display
- Which pool the panel shows (max / auto / API / total)
- Used vs remaining values
- Show/hide icon, plan tier, and billing line
- Optional HTTP proxy URL

## Disclaimer

Not affiliated with, funded by, or associated with Cursor or Anysphere.

## License

[MIT](LICENSE)
