# Cursor Usage Extension

Minimal GNOME Shell extension that shows your Cursor usage in the top panel.

Made by [669px](https://github.com/669px)

## What it shows

- Auto and API usage percentages
- Plan tier
- Billing-cycle reset countdown
- Optional billing line (included + on-demand)

## How auth works

It does **not** call the Cursor CLI as a subprocess. It reads the session token the CLI (or Cursor desktop) already saved locally, then queries Cursor’s usage API:

1. `~/.config/cursor/auth.json` — Cursor Agent / CLI login (preferred)
2. `CURSOR_SESSION_TOKEN` env var — optional override
3. `~/.config/Cursor/User/globalStorage/state.vscdb` — Cursor desktop app (needs `python3`)

Usage is fetched from `https://cursor.com/api/usage-summary`.

## Requirements

- GNOME Shell 46–50
- Signed in with Cursor CLI (`cursor-agent` / Cursor login) so `~/.config/cursor/auth.json` exists

## Install

```bash
./update
gnome-extensions enable cursor-usage@669px
```

Then reload GNOME Shell (Wayland: log out/in, X11: `Alt+F2` → `r`).

Manual:

```bash
install_dir="$HOME/.local/share/gnome-shell/extensions/cursor-usage@669px"
rm -rf "$install_dir"
mkdir -p "$(dirname "$install_dir")"
cp -rT "$PWD" "$install_dir"
glib-compile-schemas "$install_dir/schemas"
gnome-extensions enable cursor-usage@669px
```

## Disclaimer

Not affiliated with, funded by, or associated with Cursor or Anysphere.

## License

MIT
