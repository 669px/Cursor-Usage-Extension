import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import Soup from 'gi://Soup';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import Cairo from 'cairo';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const CURSOR_API_URL = 'https://cursor.com/api/usage-summary';
const CURSOR_PAGE_URL = 'https://cursor.com/dashboard/usage';
const CLAUDE_API_URL = 'https://api.anthropic.com/api/oauth/usage';
const CLAUDE_PAGE_URL = 'https://claude.ai/settings/usage';
const CLAUDE_TOKEN_URL = 'https://console.anthropic.com/v1/oauth/token';
// Public client id the Claude Code CLI uses for its own OAuth flow.
const CLAUDE_CLIENT_ID = '9d1c250a-e61b-44d9-88ed-5944d1962f5e';
// Renew slightly early so a refresh mid-request cannot 401 us.
const CLAUDE_EXPIRY_SKEW_MS = 120 * 1000;
const CODEX_API_URL = 'https://chatgpt.com/backend-api/wham/usage';
const CODEX_PAGE_URL = 'https://chatgpt.com/codex';
const TRACK_WIDTH = 280;
const RING_SIZE = 16;
const RING_WIDTH = 2.5;

function severity(util) {
    if (util >= 90)
        return 'usage-critical';
    if (util >= 75)
        return 'usage-high';
    return 'usage-low';
}

function severityRgb(util) {
    if (util >= 90)
        return [0.88, 0.11, 0.14];
    if (util >= 75)
        return [1.0, 0.47, 0.0];
    return [0.2, 0.82, 0.48];
}

function colorRgb(c) {
    const scale = Math.max(c.red, c.green, c.blue) > 1 ? 255 : 1;
    return [c.red / scale, c.green / scale, c.blue / scale];
}

function humanDuration(seconds) {
    const s = Math.max(0, Math.floor(seconds));
    if (s < 60)
        return `${s}s`;
    const mins = Math.round(s / 60);
    if (mins < 60)
        return `${mins}m`;
    const hrs = Math.floor(mins / 60);
    if (hrs < 24)
        return `${hrs}h ${mins % 60}m`;
    const days = Math.floor(hrs / 24);
    return `${days}d ${hrs % 24}h`;
}

function relativeReset(isoOrUnix) {
    let target;
    if (typeof isoOrUnix === 'number' && Number.isFinite(isoOrUnix))
        target = isoOrUnix * 1000;
    else
        target = Date.parse(isoOrUnix ?? '');
    if (Number.isNaN(target))
        return '';
    const diff = target - Date.now();
    if (diff <= 0)
        return 'resetting';
    return `resets in ${humanDuration(diff / 1000)}`;
}

function statusLabel(status) {
    if (status === 401 || status === 403)
        return 'Session expired';
    if (status === 404)
        return 'Not available';
    if (status === 429)
        return 'Rate limited';
    if (status >= 500)
        return 'Service down';
    return `HTTP ${status}`;
}

function planLabel(value, fallback = 'AI') {
    const raw = `${value ?? ''}`.trim();
    if (!raw)
        return fallback;
    const n = raw.toLowerCase().replace(/[\s_-]/g, '');
    const known = [
        ['ultra', 'ULTRA'], ['proplus', 'PRO+'], ['business', 'BUSINESS'],
        ['enterprise', 'ENT'], ['pro', 'PRO'], ['max', 'MAX'], ['team', 'TEAM'],
        ['student', 'STUDENT'], ['free', 'FREE'], ['go', 'GO'], ['plus', 'PLUS'],
    ];
    for (const [key, label] of known) {
        if (n.includes(key))
            return label;
    }
    return raw.toUpperCase();
}

function decodeJwtPayload(token) {
    try {
        const parts = `${token}`.split('.');
        if (parts.length < 2)
            return null;
        let payload = parts[1].replace(/-/g, '+').replace(/_/g, '/');
        while (payload.length % 4)
            payload += '=';
        return JSON.parse(new TextDecoder('utf-8').decode(GLib.base64_decode(payload)));
    } catch (_e) {
        return null;
    }
}

function sessionCookieFromToken(token) {
    const payload = decodeJwtPayload(token);
    if (!payload?.sub)
        return null;
    let sub = `${payload.sub}`;
    if (sub.includes('|'))
        sub = sub.split('|').pop();
    return `${sub}%3A%3A${token}`;
}

function centsLabel(cents) {
    if (typeof cents !== 'number' || !Number.isFinite(cents))
        return '-';
    return `$${(cents / 100).toFixed(2)}`;
}

function pct(value) {
    const n = typeof value === 'number' && Number.isFinite(value) ? value : 0;
    return Math.min(100, Math.max(0, n));
}

function pickPool(a, b, mode) {
    const known = p => (p && !p.missing ? p : null);
    const ka = known(a);
    const kb = known(b);
    if (mode === 'auto')
        return ka ?? kb;
    if (mode === 'api')
        return kb ?? ka;
    if (!ka)
        return kb;
    if (!kb)
        return ka;
    return (ka.utilization ?? 0) >= (kb.utilization ?? 0) ? ka : kb;
}

class Meter {
    constructor(name) {
        this.root = new St.BoxLayout({vertical: true, style_class: 'cu-meter'});
        const row = new St.BoxLayout({style_class: 'cu-meter-row'});
        this._name = new St.Label({text: name, style_class: 'cu-meter-name', x_expand: true});
        this._pct = new St.Label({text: '...', style_class: 'cu-meter-pct'});
        row.add_child(this._name);
        row.add_child(this._pct);
        this._track = new St.BoxLayout({style_class: 'cu-track'});
        this._fill = new St.Widget({style_class: 'cu-fill usage-low'});
        this._track.add_child(this._fill);
        this._ratio = 0;
        // The fill used to be sized from a hardcoded 260px constant, so it
        // drifted out of the track under any text-scaling factor.
        this._trackNotifyId = this._track.connect('notify::width', () => this._applyFill());
        this._caption = new St.Label({text: '', style_class: 'cu-caption'});
        this.root.add_child(row);
        this.root.add_child(this._track);
        this.root.add_child(this._caption);
    }

    _applyFill() {
        const width = this._track.get_width() || TRACK_WIDTH;
        this._fill.set_width(Math.round(this._ratio * width));
    }

    setValue(util, caption, displayValue = util, suffix = 'used') {
        const clamped = Math.max(0, Math.min(100, util));
        this._pct.text = `${displayValue.toFixed(0)}% ${suffix}`;
        this._ratio = clamped / 100;
        this._applyFill();
        this._fill.style_class = `cu-fill ${severity(util)}`;
        this._caption.text = caption ?? '';
        this._caption.visible = !!caption;
    }

    setMuted(detail = '-') {
        this._pct.text = detail;
        this._ratio = 0;
        this._applyFill();
        this._fill.style_class = 'cu-fill';
        this._caption.visible = false;
    }

    setVisible(v) {
        this.root.visible = v;
    }

    destroy() {
        if (this._trackNotifyId) {
            this._track.disconnect(this._trackNotifyId);
            this._trackNotifyId = null;
        }
        this.root?.destroy();
        this.root = null;
    }
}

class ProviderBlock {
    constructor(title, id) {
        this.root = new St.BoxLayout({vertical: true, style_class: 'cu-provider'});
        const header = new St.BoxLayout({style_class: 'cu-header'});
        this._dot = new St.Widget({
            style_class: `cu-dot cu-dot-${id}`,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._title = new St.Label({text: title, style_class: 'cu-title', x_expand: true});
        this._tier = new St.Label({text: '', style_class: 'cu-tier'});
        header.add_child(this._dot);
        header.add_child(this._title);
        header.add_child(this._tier);
        this.root.add_child(header);
        this.meterA = new Meter('A');
        this.meterB = new Meter('B');
        this.root.add_child(this.meterA.root);
        this.root.add_child(this.meterB.root);
        this._billing = new St.Label({text: '', style_class: 'cu-billing'});
        this._billing.visible = false;
        this.root.add_child(this._billing);
        this._error = new St.Label({text: '', style_class: 'cu-error'});
        this._error.visible = false;
        this.root.add_child(this._error);
    }

    setMeterNames(a, b) {
        this.meterA._name.text = a;
        this.meterB._name.text = b;
    }

    setVisible(v) {
        this.root.visible = v;
    }

    destroy() {
        this.meterA?.destroy();
        this.meterB?.destroy();
        this.root?.destroy();
        this.root = null;
    }
}

const Ring = GObject.registerClass(
class Ring extends St.DrawingArea {
    _init() {
        super._init({
            style_class: 'cu-ring',
            width: RING_SIZE,
            height: RING_SIZE,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._util = null;
        this._color = null;
    }

    setValue(util) {
        this._util = Math.max(0, Math.min(100, util));
        this._color = severityRgb(util);
        this.queue_repaint();
    }

    setUnknown() {
        this._util = null;
        this._color = null;
        this.queue_repaint();
    }

    vfunc_repaint() {
        const cr = this.get_context();
        const [w, h] = this.get_surface_size();
        const cx = w / 2;
        const cy = h / 2;
        const radius = Math.min(w, h) / 2 - RING_WIDTH / 2;
        cr.setLineWidth(RING_WIDTH);
        cr.setLineCap(Cairo.LineCap.ROUND);
        const [fr, fg, fb] = colorRgb(this.get_theme_node().get_foreground_color());
        cr.setSourceRGBA(fr, fg, fb, 0.2);
        cr.arc(cx, cy, radius, 0, 2 * Math.PI);
        cr.stroke();
        if (this._util !== null && this._util > 0) {
            const [r, g, b] = this._color ?? severityRgb(this._util);
            cr.setSourceRGBA(r, g, b, 1);
            cr.arc(cx, cy, radius, -Math.PI / 2, -Math.PI / 2 + (this._util / 100) * 2 * Math.PI);
            cr.stroke();
        }
        cr.$dispose();
    }
});

const CursorUsageIndicator = GObject.registerClass(
class CursorUsageIndicator extends PanelMenu.Button {
    _init(extensionPath, settings, openPreferences) {
        super._init(0.0, 'AI Usage');
        this._extensionPath = extensionPath;
        this._settings = settings;
        this._openPreferences = openPreferences;
        this._session = this._createSession();
        this._last = {cursor: null, claude: null, codex: null};
        this._countdownTimer = null;
        this._timerId = null;
        this._settingsChangedId = null;
        this._cancellable = null;
        this._tokenProc = null;
        this._pending = 0;
        this._claudeRenewing = false;

        const box = new St.BoxLayout({style_class: 'cu-panel'});
        // A symbolic SVG recolours with the panel theme; the old PNG stayed a
        // fixed colour and looked foreign next to the other status icons.
        this._icon = new St.Icon({
            gicon: Gio.icon_new_for_string(
                GLib.build_filenamev([this._extensionPath, 'icons', 'ai-usage-symbolic.svg'])),
            style_class: 'cu-panel-icon system-status-icon',
            icon_size: 14,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._ring = new Ring();
        this._label = new St.Label({
            text: '...',
            y_align: Clutter.ActorAlign.CENTER,
            style_class: 'cu-panel-pct',
        });
        this._panelTier = new St.Label({
            text: '',
            y_align: Clutter.ActorAlign.CENTER,
            style_class: 'cu-panel-tier',
        });
        box.add_child(this._icon);
        box.add_child(this._ring);
        box.add_child(this._label);
        box.add_child(this._panelTier);
        this.add_child(box);

        this._createMenu();
        this._applyPanelChrome();

        this._settingsChangedId = this._settings.connect('changed', (_s, key) => {
            if (key === 'refresh-interval')
                this._restartTimer();
            else if (key === 'proxy-url')
                this._recreateSession();
            else if (key === 'show-icon' || key === 'show-tier' || key === 'display-mode')
                this._applyPanelChrome();
            else if (
                key === 'usage-display' || key === 'panel-window' || key === 'panel-provider' ||
                key === 'show-billing' || key === 'show-cursor' || key === 'show-claude' ||
                key === 'show-codex'
            ) {
                this._applyProviderVisibility();
                this._renderAll();
                if (key.startsWith('show-'))
                    this._refreshUsage();
            }
        });

        this.menu.connectObject('open-state-changed', (_menu, open) => {
            if (open)
                this._refreshUsage();
        }, this);

        this._refreshUsage();
        this._startTimer();
    }

    _cancelPending() {
        if (this._cancellable && !this._cancellable.is_cancelled())
            this._cancellable.cancel();
        this._cancellable = null;
        if (this._tokenProc) {
            try {
                this._tokenProc.force_exit();
            } catch (_e) {
                // ignore
            }
            this._tokenProc = null;
        }
        this._claudeRenewing = false;
    }

    _newCancellable() {
        this._cancelPending();
        this._cancellable = new Gio.Cancellable();
        return this._cancellable;
    }

    _applyPanelChrome() {
        const mode = this._settings.get_string('display-mode');
        this._icon.visible = this._settings.get_boolean('show-icon');
        this._ring.visible = mode === 'ring';
        this._panelTier.visible = this._settings.get_boolean('show-tier');
        this._renderPanel();
    }

    _applyProviderVisibility() {
        this._cursorBlock.setVisible(this._settings.get_boolean('show-cursor'));
        this._claudeBlock.setVisible(this._settings.get_boolean('show-claude'));
        this._codexBlock.setVisible(this._settings.get_boolean('show-codex'));
    }

    _createSession() {
        const session = new Soup.Session();
        const proxyUrl = this._settings.get_string('proxy-url').trim();
        if (proxyUrl !== '')
            session.set_proxy_resolver(Gio.SimpleProxyResolver.new(proxyUrl, null));
        return session;
    }

    _recreateSession() {
        this._cancelPending();
        this._session?.abort();
        this._session = this._createSession();
        this._refreshUsage();
    }

    _createMenu() {
        const item = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false});
        const root = new St.BoxLayout({vertical: true, style_class: 'cu-popup'});
        item.add_child(root);
        this.menu.addMenuItem(item);

        this._cursorBlock = new ProviderBlock('Cursor', 'cursor');
        this._cursorBlock.setMeterNames('Auto', 'API');
        this._claudeBlock = new ProviderBlock('Claude', 'claude');
        this._claudeBlock.setMeterNames('5h', '7d');
        this._codexBlock = new ProviderBlock('Codex', 'codex');
        this._codexBlock.setMeterNames('Primary', 'Weekly');
        root.add_child(this._cursorBlock.root);
        root.add_child(this._claudeBlock.root);
        root.add_child(this._codexBlock.root);
        this._applyProviderVisibility();

        const footer = new St.BoxLayout({style_class: 'cu-footer'});
        this._updated = new St.Label({
            text: '',
            style_class: 'cu-updated',
            x_expand: true,
            y_align: Clutter.ActorAlign.CENTER,
        });
        footer.add_child(this._updated);

        const mkLink = (label, fn) => {
            const btn = new St.Button({
                label,
                style_class: 'cu-link',
                can_focus: true,
                reactive: true,
                track_hover: true,
            });
            btn.connect('clicked', fn);
            footer.add_child(btn);
            return btn;
        };
        mkLink('Refresh', () => this._refreshUsage());
        mkLink('Open', () => {
            this.menu.close();
            const p = this._settings.get_string('panel-provider');
            const url = p === 'claude' ? CLAUDE_PAGE_URL
                : p === 'codex' ? CODEX_PAGE_URL
                    : CURSOR_PAGE_URL;
            Gio.AppInfo.launch_default_for_uri(url, null);
        });
        mkLink('Prefs', () => {
            this.menu.close();
            this._openPreferences();
        });
        root.add_child(footer);
    }

    _startTimer() {
        this._timerId = GLib.timeout_add_seconds(
            GLib.PRIORITY_DEFAULT,
            this._settings.get_int('refresh-interval'),
            () => {
                this._refreshUsage();
                return GLib.SOURCE_CONTINUE;
            }
        );
    }

    _stopTimer() {
        if (this._timerId) {
            GLib.source_remove(this._timerId);
            this._timerId = null;
        }
    }

    _restartTimer() {
        this._stopTimer();
        this._startTimer();
    }

    _refreshUsage() {
        const cancellable = this._newCancellable();
        this._pending = 0;
        this._markRefreshing();
        if (this._settings.get_boolean('show-cursor')) {
            this._pending++;
            this._refreshCursor(cancellable);
        }
        if (this._settings.get_boolean('show-claude')) {
            this._pending++;
            this._refreshClaude(cancellable);
        }
        if (this._settings.get_boolean('show-codex')) {
            this._pending++;
            this._refreshCodex(cancellable);
        }
        if (this._pending === 0) {
            this._label.text = '-';
            this._ring.setUnknown();
            this._stamp(false);
        }
    }

    _doneOne() {
        this._pending = Math.max(0, this._pending - 1);
        if (this._pending === 0)
            this._stamp(true);
    }

    // --- Cursor ---

    _cliAuthPath() {
        return GLib.build_filenamev([GLib.get_home_dir(), '.config', 'cursor', 'auth.json']);
    }

    _desktopDbPath() {
        return GLib.build_filenamev([
            GLib.get_home_dir(), '.config', 'Cursor', 'User', 'globalStorage', 'state.vscdb',
        ]);
    }

    _refreshCursor(cancellable) {
        const envToken = (GLib.getenv('CURSOR_SESSION_TOKEN') ?? '').trim();
        if (envToken) {
            const token = envToken.includes('::') || envToken.includes('%3A%3A')
                ? envToken.split(/::|%3A%3A/).pop()
                : envToken;
            this._fetchCursor(token, cancellable);
            return;
        }
        const authFile = Gio.File.new_for_path(this._cliAuthPath());
        if (authFile.query_exists(null)) {
            authFile.load_contents_async(cancellable, (file, result) => {
                try {
                    const [, contents] = file.load_contents_finish(result);
                    const auth = JSON.parse(new TextDecoder('utf-8').decode(contents));
                    const token = auth.accessToken ?? auth.access_token ?? null;
                    if (!token) {
                        this._tryDesktopToken(cancellable);
                        return;
                    }
                    this._fetchCursor(token, cancellable);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    this._tryDesktopToken(cancellable);
                }
            });
            return;
        }
        this._tryDesktopToken(cancellable);
    }

    _tryDesktopToken(cancellable) {
        const dbPath = this._desktopDbPath();
        if (!Gio.File.new_for_path(dbPath).query_exists(null)) {
            this._setProviderUnavailable('cursor', 'Login required');
            this._doneOne();
            return;
        }
        try {
            const proc = Gio.Subprocess.new(
                [
                    'sqlite3', dbPath,
                    "SELECT value FROM ItemTable WHERE key='cursorAuth/accessToken' LIMIT 1;",
                ],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE
            );
            this._tokenProc = proc;
            proc.communicate_utf8_async(null, cancellable, (_proc, result) => {
                this._tokenProc = null;
                try {
                    const [, stdout] = proc.communicate_utf8_finish(result);
                    const token = (stdout ?? '').trim();
                    if (!token) {
                        this._setProviderUnavailable('cursor', 'No auth');
                        this._doneOne();
                        return;
                    }
                    this._fetchCursor(token, cancellable);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    this._setProviderUnavailable('cursor', 'No auth');
                    this._doneOne();
                }
            });
        } catch (_e) {
            this._setProviderUnavailable('cursor', 'No auth');
            this._doneOne();
        }
    }

    _fetchCursor(accessToken, cancellable) {
        const cookie = sessionCookieFromToken(accessToken);
        if (!cookie) {
            this._setProviderUnavailable('cursor', 'Bad token');
            this._doneOne();
            return;
        }
        this._httpGet(CURSOR_API_URL, {
            Cookie: `WorkosCursorSessionToken=${cookie}`,
            'User-Agent': 'cursor-usage-extension',
        }, cancellable, (ok, data, err) => {
            if (!ok) {
                this._setProviderUnavailable('cursor', err);
                this._doneOne();
                return;
            }
            this._last.cursor = this._normalizeCursor(data);
            this._renderProvider('cursor');
            this._renderPanel();
            this._scheduleCountdown();
            this._doneOne();
        });
    }

    _normalizeCursor(data) {
        const plan = data.individualUsage?.plan ?? {};
        const onDemand = data.individualUsage?.onDemand ?? null;
        const end = data.billingCycleEnd ?? null;
        const fromMsg = msg => {
            const m = `${msg ?? ''}`.match(/([\d.]+)\s*%/);
            return m ? Number(m[1]) : null;
        };
        const auto = pct(plan.autoPercentUsed ?? fromMsg(data.autoModelSelectedDisplayMessage));
        const api = pct(plan.apiPercentUsed ?? fromMsg(data.namedModelSelectedDisplayMessage));
        const total = pct(plan.totalPercentUsed ?? Math.max(auto, api));
        return {
            id: 'cursor',
            tier: planLabel(data.membershipType, 'CURSOR'),
            isUnlimited: !!data.isUnlimited,
            a: {name: 'Auto', utilization: auto, resets_at: end},
            b: {name: 'API', utilization: api, resets_at: end},
            total: {utilization: total, resets_at: end},
            billing: (() => {
                if (!this._settings.get_boolean('show-billing'))
                    return '';
                const parts = [];
                if (plan.enabled && plan.breakdown?.included != null) {
                    let t = centsLabel(plan.breakdown.included);
                    if (plan.breakdown.bonus)
                        t += ` + ${centsLabel(plan.breakdown.bonus)} bonus`;
                    parts.push(t);
                }
                if (onDemand?.enabled) {
                    const lim = onDemand.limit == null ? '∞' : centsLabel(onDemand.limit);
                    parts.push(`on-demand ${centsLabel(onDemand.used ?? 0)} / ${lim}`);
                }
                return parts.join(' · ');
            })(),
        };
    }

    // --- Claude ---

    _claudeCredPath() {
        return GLib.build_filenamev([GLib.get_home_dir(), '.claude', '.credentials.json']);
    }

    _refreshClaude(cancellable) {
        const env = (GLib.getenv('CLAUDE_CODE_OAUTH_TOKEN') ?? '').trim();
        if (env) {
            this._fetchClaude(env, planLabel(null, 'CLAUDE'), cancellable);
            return;
        }
        const file = Gio.File.new_for_path(this._claudeCredPath());
        if (!file.query_exists(null)) {
            this._setProviderUnavailable('claude', 'Run claude to sign in');
            this._doneOne();
            return;
        }
        file.load_contents_async(cancellable, (_f, result) => {
            try {
                const [, contents] = file.load_contents_finish(result);
                const auth = JSON.parse(new TextDecoder('utf-8').decode(contents));
                const oauth = auth.claudeAiOauth ?? auth ?? null;
                const token = oauth?.accessToken ?? null;
                if (!token) {
                    this._setProviderUnavailable('claude', 'No OAuth');
                    this._doneOne();
                    return;
                }
                const tier = planLabel(oauth.subscriptionType, 'CLAUDE');
                const expired = typeof oauth.expiresAt === 'number' &&
                    oauth.expiresAt > 0 &&
                    oauth.expiresAt - CLAUDE_EXPIRY_SKEW_MS <= Date.now();
                // A stored token that has aged out used to leave the menu stuck
                // on "HTTP 401" until the user next ran the CLI by hand.
                if (expired && oauth.refreshToken) {
                    this._renewClaudeToken(auth, cancellable, (fresh, err) => {
                        if (fresh)
                            this._fetchClaude(fresh, tier, cancellable);
                        else if (!cancellable.is_cancelled()) {
                            this._setProviderUnavailable('claude', err ?? 'Session expired');
                            this._doneOne();
                        }
                    });
                    return;
                }
                this._fetchClaude(token, tier, cancellable, !expired);
            } catch (_e) {
                if (cancellable.is_cancelled())
                    return;
                this._setProviderUnavailable('claude', 'No auth');
                this._doneOne();
            }
        });
    }

    // Exchanges the stored refresh token for a new access token and saves the
    // rotated pair back. The server invalidates the old refresh token as soon
    // as it answers, so if the write fails we deliberately discard the new
    // token rather than silently log the CLI out on its next start.
    _renewClaudeToken(auth, cancellable, cb) {
        if (this._claudeRenewing) {
            cb(null, 'Refreshing');
            return;
        }
        const oauth = auth.claudeAiOauth ?? auth;
        this._claudeRenewing = true;
        const done = (token, err) => {
            this._claudeRenewing = false;
            cb(token, err);
        };

        const message = Soup.Message.new('POST', CLAUDE_TOKEN_URL);
        const payload = JSON.stringify({
            grant_type: 'refresh_token',
            refresh_token: oauth.refreshToken,
            client_id: CLAUDE_CLIENT_ID,
        });
        message.set_request_body_from_bytes(
            'application/json',
            new GLib.Bytes(new TextEncoder().encode(payload))
        );
        message.request_headers.append('Accept', 'application/json');
        message.request_headers.append('User-Agent', 'claude-code/2.1.72');

        this._session.send_and_read_async(
            message,
            GLib.PRIORITY_DEFAULT,
            cancellable,
            (session, result) => {
                try {
                    const bytes = session.send_and_read_finish(result);
                    if (message.status_code !== 200) {
                        done(null, message.status_code === 400 || message.status_code === 401
                            ? 'Session expired'
                            : `HTTP ${message.status_code}`);
                        return;
                    }
                    const data = JSON.parse(
                        new TextDecoder('utf-8').decode(bytes.get_data()));
                    if (!data.access_token) {
                        done(null, 'Refresh rejected');
                        return;
                    }
                    const next = {...oauth, accessToken: data.access_token};
                    if (data.refresh_token)
                        next.refreshToken = data.refresh_token;
                    next.expiresAt = Date.now() +
                        (Number(data.expires_in) || 8 * 3600) * 1000;

                    const updated = auth.claudeAiOauth
                        ? {...auth, claudeAiOauth: next}
                        : next;
                    if (!this._writeClaudeCreds(updated)) {
                        done(null, 'Cannot save token');
                        return;
                    }
                    done(data.access_token, null);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    done(null, 'Refresh failed');
                }
            }
        );
    }

    _writeClaudeCreds(auth) {
        try {
            const path = this._claudeCredPath();
            const file = Gio.File.new_for_path(path);
            const bytes = new TextEncoder().encode(`${JSON.stringify(auth, null, 2)}\n`);
            const [ok] = file.replace_contents(
                bytes, null, false, Gio.FileCreateFlags.PRIVATE, null);
            if (!ok)
                return false;
            // Keep the file owner-only, the way the CLI writes it.
            file.set_attribute_uint32(
                'unix::mode', 0o600, Gio.FileQueryInfoFlags.NONE, null);
            return true;
        } catch (_e) {
            return false;
        }
    }

    _fetchClaude(token, tier, cancellable, mayRetry = false) {
        this._httpGet(CLAUDE_API_URL, {
            Authorization: `Bearer ${token}`,
            'anthropic-beta': 'oauth-2025-04-20',
            'User-Agent': 'claude-code/2.1.72',
            'Content-Type': 'application/json',
        }, cancellable, (ok, data, err, status) => {
            if (!ok) {
                // The token can be refused before its recorded expiry when it
                // has been revoked or the clock is skewed; one forced renewal
                // turns that into a working request.
                if (mayRetry && (status === 401 || status === 403)) {
                    this._retryClaudeAfterRefresh(tier, cancellable, err);
                    return;
                }
                this._setProviderUnavailable('claude', err);
                this._doneOne();
                return;
            }
            this._last.claude = this._normalizeClaude(data, tier);
            this._renderProvider('claude');
            this._renderPanel();
            this._scheduleCountdown();
            this._doneOne();
        });
    }

    _retryClaudeAfterRefresh(tier, cancellable, previousError) {
        let auth = null;
        try {
            const [ok, contents] = GLib.file_get_contents(this._claudeCredPath());
            if (ok)
                auth = JSON.parse(new TextDecoder('utf-8').decode(contents));
        } catch (_e) {
            auth = null;
        }
        const oauth = auth?.claudeAiOauth ?? auth;
        if (!oauth?.refreshToken) {
            this._setProviderUnavailable('claude', previousError);
            this._doneOne();
            return;
        }
        this._renewClaudeToken(auth, cancellable, (fresh, err) => {
            if (fresh) {
                this._fetchClaude(fresh, tier, cancellable);
                return;
            }
            if (cancellable.is_cancelled())
                return;
            this._setProviderUnavailable('claude', err ?? previousError);
            this._doneOne();
        });
    }

    _normalizeClaude(data, tier) {
        const five = data.five_hour ?? null;
        const seven = data.seven_day ?? null;
        let billing = '';
        if (this._settings.get_boolean('show-billing') && data.extra_usage?.is_enabled) {
            const u = data.extra_usage;
            billing = `extra ${pct(u.utilization).toFixed(0)}%`;
            if (u.used_credits != null && u.monthly_limit != null)
                billing = `extra ${u.used_credits} / ${u.monthly_limit}`;
        }
        return {
            id: 'claude',
            tier,
            isUnlimited: false,
            a: {
                name: '5h',
                utilization: pct(five?.utilization),
                resets_at: five?.resets_at ?? null,
                missing: five?.utilization == null,
            },
            b: {
                name: '7d',
                utilization: pct(seven?.utilization),
                resets_at: seven?.resets_at ?? null,
                missing: seven?.utilization == null,
            },
            total: {
                utilization: Math.max(pct(five?.utilization), pct(seven?.utilization)),
                resets_at: seven?.resets_at ?? five?.resets_at ?? null,
                missing: five?.utilization == null && seven?.utilization == null,
            },
            billing,
        };
    }

    // --- Codex ---

    _codexAuthPath() {
        return GLib.build_filenamev([GLib.get_home_dir(), '.codex', 'auth.json']);
    }

    _refreshCodex(cancellable) {
        const file = Gio.File.new_for_path(this._codexAuthPath());
        if (!file.query_exists(null)) {
            this._setProviderUnavailable('codex', 'Login required');
            this._doneOne();
            return;
        }
        file.load_contents_async(cancellable, (_f, result) => {
            try {
                const [, contents] = file.load_contents_finish(result);
                const auth = JSON.parse(new TextDecoder('utf-8').decode(contents));
                const token = auth.tokens?.access_token ?? auth.access_token ?? null;
                const accountId = auth.tokens?.account_id ?? auth.account_id ?? null;
                if (!token) {
                    this._setProviderUnavailable('codex', 'No auth');
                    this._doneOne();
                    return;
                }
                this._fetchCodex(token, accountId, cancellable);
            } catch (_e) {
                if (cancellable.is_cancelled())
                    return;
                this._setProviderUnavailable('codex', 'No auth');
                this._doneOne();
            }
        });
    }

    _fetchCodex(token, accountId, cancellable) {
        const headers = {
            Authorization: `Bearer ${token}`,
            Accept: 'application/json',
            'User-Agent': 'cursor-usage-extension',
        };
        if (accountId)
            headers['ChatGPT-Account-Id'] = accountId;
        this._httpGet(CODEX_API_URL, headers, cancellable, (ok, data, err) => {
            if (!ok) {
                this._setProviderUnavailable('codex', err);
                this._doneOne();
                return;
            }
            this._last.codex = this._normalizeCodex(data);
            this._renderProvider('codex');
            this._renderPanel();
            this._scheduleCountdown();
            this._doneOne();
        });
    }

    _normalizeCodex(data) {
        const primary = data.rate_limit?.primary_window ?? null;
        const secondary = data.rate_limit?.secondary_window ?? null;
        const toPool = (win, name) => {
            if (!win)
                return null;
            return {
                name,
                utilization: pct(win.used_percent),
                resets_at: typeof win.reset_at === 'number' ? win.reset_at : null,
            };
        };
        const a = toPool(primary, 'Primary') ??
            {name: 'Primary', utilization: 0, resets_at: null, missing: true};
        const b = toPool(secondary, 'Weekly');
        let billing = '';
        if (this._settings.get_boolean('show-billing') && data.credits?.has_credits) {
            if (data.credits.unlimited)
                billing = 'credits unlimited';
            else if (data.credits.balance != null)
                billing = `credits $${Number(data.credits.balance).toFixed(2)}`;
        }
        return {
            id: 'codex',
            tier: planLabel(data.plan_type, 'CODEX'),
            isUnlimited: !!data.credits?.unlimited,
            a,
            b: b ?? {name: 'Weekly', utilization: 0, resets_at: null, missing: true},
            total: {
                utilization: Math.max(a.utilization, b?.utilization ?? 0),
                resets_at: b?.resets_at ?? a.resets_at,
            },
            billing,
        };
    }

    // --- HTTP ---

    _httpGet(url, headers, cancellable, cb) {
        const message = Soup.Message.new('GET', url);
        for (const [k, v] of Object.entries(headers))
            message.request_headers.append(k, v);
        this._session.send_and_read_async(
            message,
            GLib.PRIORITY_DEFAULT,
            cancellable,
            (session, result) => {
                try {
                    const bytes = session.send_and_read_finish(result);
                    if (message.status_code !== 200) {
                        cb(false, null, statusLabel(message.status_code),
                            message.status_code);
                        return;
                    }
                    const data = JSON.parse(new TextDecoder('utf-8').decode(bytes.get_data()));
                    cb(true, data, null, 200);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    cb(false, null, 'API failed', 0);
                }
            }
        );
    }

    // --- Render ---

    _blockFor(id) {
        return id === 'claude' ? this._claudeBlock
            : id === 'codex' ? this._codexBlock
                : this._cursorBlock;
    }

    _setProviderUnavailable(id, detail) {
        this._last[id] = null;
        const block = this._blockFor(id);
        block._tier.text = '';
        block._tier.visible = false;
        block.meterA.setVisible(true);
        block.meterB.setVisible(false);
        block.meterA.setMuted(detail);
        block._billing.visible = false;
        block._billing.text = '';
        block._error.text = '';
        block._error.visible = false;
        this._renderPanel();
    }

    _applyMeter(meter, win) {
        if (!win || win.missing) {
            meter.setVisible(false);
            return;
        }
        meter.setVisible(true);
        const util = pct(win.utilization);
        const display = this._settings.get_string('usage-display') === 'remaining'
            ? 100 - util
            : util;
        const suffix = this._settings.get_string('usage-display') === 'remaining'
            ? 'left'
            : 'used';
        meter.setValue(util, relativeReset(win.resets_at), display, suffix);
    }

    _renderProvider(id) {
        const data = this._last[id];
        const block = this._blockFor(id);
        if (!data) {
            this._setProviderUnavailable(id, '-');
            return;
        }
        block._error.visible = false;
        block._error.text = '';
        block._tier.text = data.tier ?? '';
        block._tier.visible = !!data.tier && this._settings.get_boolean('show-tier');
        if (data.isUnlimited) {
            block.meterA.setVisible(true);
            block.meterB.setVisible(true);
            block.meterA.setMuted('unlimited');
            block.meterB.setMuted('unlimited');
        } else {
            this._applyMeter(block.meterA, data.a);
            this._applyMeter(block.meterB, data.b);
            if (!block.meterA.root.visible && !block.meterB.root.visible) {
                block.meterA.setVisible(true);
                block.meterA.setMuted('no data');
            }
        }
        block._billing.text = data.billing ?? '';
        block._billing.visible = !!(data.billing && this._settings.get_boolean('show-billing'));
    }

    _renderAll() {
        for (const id of ['cursor', 'claude', 'codex']) {
            if (this._last[id])
                this._renderProvider(id);
        }
        this._renderPanel();
    }

    _selectedSnapshot() {
        const mode = this._settings.get_string('panel-provider');
        const enabled = [];
        if (this._settings.get_boolean('show-cursor') && this._last.cursor)
            enabled.push(this._last.cursor);
        if (this._settings.get_boolean('show-claude') && this._last.claude)
            enabled.push(this._last.claude);
        if (this._settings.get_boolean('show-codex') && this._last.codex)
            enabled.push(this._last.codex);
        if (!enabled.length)
            return null;
        if (mode !== 'max' && this._last[mode])
            return this._last[mode];
        // max across providers, and the fallback when the chosen one is down
        let best = null;
        let bestUtil = -1;
        for (const snap of enabled) {
            const win = this._poolFromSnap(snap);
            const u = win?.utilization ?? 0;
            if (u > bestUtil) {
                bestUtil = u;
                best = snap;
            }
        }
        return best;
    }

    _poolFromSnap(snap) {
        if (!snap)
            return null;
        const mode = this._settings.get_string('panel-window');
        if (snap.isUnlimited)
            return {utilization: 0, unlimited: true};
        if (mode === 'total')
            return snap.total && !snap.total.missing
                ? snap.total
                : pickPool(snap.a, snap.b, 'max');
        return pickPool(snap.a, snap.b, mode);
    }

    _renderPanel() {
        const snap = this._selectedSnapshot();
        if (!snap) {
            this._label.text = '-';
            this._label.style_class = 'cu-panel-pct';
            this._ring.setUnknown();
            this._panelTier.text = '';
            return;
        }
        this._panelTier.text = this._settings.get_boolean('show-tier') ? snap.tier : '';
        if (snap.isUnlimited) {
            this._label.text = '∞';
            this._label.style_class = 'cu-panel-pct usage-low';
            this._ring.setValue(0);
            return;
        }
        const win = this._poolFromSnap(snap);
        if (!win) {
            this._label.text = '-';
            this._label.style_class = 'cu-panel-pct';
            this._ring.setUnknown();
            return;
        }
        const util = pct(win.utilization);
        const display = this._settings.get_string('usage-display') === 'remaining'
            ? 100 - util
            : util;
        this._label.text = `${Math.round(display)}%`;
        this._label.style_class = `cu-panel-pct ${severity(util)}`;
        this._ring.setValue(util);
    }

    _scheduleCountdown() {
        if (this._countdownTimer) {
            GLib.source_remove(this._countdownTimer);
            this._countdownTimer = null;
        }
        const snap = this._selectedSnapshot();
        const end = this._poolFromSnap(snap)?.resets_at ??
            snap?.a?.resets_at ?? snap?.b?.resets_at;
        if (end == null)
            return;
        const target = typeof end === 'number' ? end * 1000 : Date.parse(end);
        if (Number.isNaN(target))
            return;
        const seconds = (target - Date.now()) / 1000;
        if (!(seconds > 0))
            return;
        this._countdownTimer = GLib.timeout_add_seconds(
            GLib.PRIORITY_DEFAULT,
            seconds < 90 ? 1 : 30,
            () => {
                this._countdownTimer = null;
                this._renderAll();
                this._scheduleCountdown();
                return GLib.SOURCE_REMOVE;
            }
        );
    }

    _stamp(ok) {
        const now = GLib.DateTime.new_now_local();
        this._updated.text = `${ok ? 'Updated' : 'Checked'} ${now.format('%H:%M')}`;
    }

    _markRefreshing() {
        this._updated.text = 'Refreshing…';
    }

    destroy() {
        this._stopTimer();
        if (this._countdownTimer) {
            GLib.source_remove(this._countdownTimer);
            this._countdownTimer = null;
        }
        this._cancelPending();
        this.menu.disconnectObject(this);
        this._session?.abort();
        this._session = null;
        if (this._settingsChangedId) {
            this._settings.disconnect(this._settingsChangedId);
            this._settingsChangedId = null;
        }
        this._settings = null;
        this._openPreferences = null;
        this._last = {cursor: null, claude: null, codex: null};
        this._cursorBlock?.destroy();
        this._claudeBlock?.destroy();
        this._codexBlock?.destroy();
        this._cursorBlock = null;
        this._claudeBlock = null;
        this._codexBlock = null;
        super.destroy();
    }
});

export default class CursorUsageExtension extends Extension {
    enable() {
        this._settings = this.getSettings();
        this._indicator = new CursorUsageIndicator(
            this.path,
            this._settings,
            () => this.openPreferences()
        );
        Main.panel.addToStatusArea(this.uuid, this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
        this._settings = null;
    }
}
