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

const USAGE_API_URL = 'https://cursor.com/api/usage-summary';
const USAGE_PAGE_URL = 'https://cursor.com/dashboard/usage';
const TRACK_WIDTH = 260;
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

function relativeReset(iso) {
    const target = Date.parse(iso ?? '');
    if (Number.isNaN(target))
        return '';
    const diff = target - Date.now();
    if (diff <= 0)
        return 'resetting';
    return `resets in ${humanDuration(diff / 1000)}`;
}

function planLabel(value) {
    const raw = `${value ?? ''}`.trim();
    if (!raw)
        return 'CURSOR';
    const n = raw.toLowerCase().replace(/[\s_-]/g, '');
    const known = [
        ['ultra', 'ULTRA'],
        ['proplus', 'PRO+'],
        ['business', 'BUSINESS'],
        ['enterprise', 'ENT'],
        ['pro', 'PRO'],
        ['free', 'FREE'],
        ['team', 'TEAM'],
        ['student', 'STUDENT'],
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
        this._caption = new St.Label({text: '', style_class: 'cu-caption'});
        this.root.add_child(row);
        this.root.add_child(this._track);
        this.root.add_child(this._caption);
    }

    setValue(util, caption, displayValue = util, suffix = 'used') {
        const clamped = Math.max(0, Math.min(100, util));
        this._pct.text = `${displayValue.toFixed(0)}% ${suffix}`;
        this._fill.set_width(Math.round((clamped / 100) * TRACK_WIDTH));
        this._fill.style_class = `cu-fill ${severity(util)}`;
        this._caption.text = caption ?? '';
        this._caption.visible = !!caption;
    }

    setMuted(detail = '-') {
        this._pct.text = detail;
        this._fill.set_width(0);
        this._fill.style_class = 'cu-fill';
        this._caption.visible = false;
    }

    destroy() {
        this._caption?.destroy();
        this._caption = null;
        this._fill?.destroy();
        this._fill = null;
        this._name?.destroy();
        this._name = null;
        this._pct?.destroy();
        this._pct = null;
        this._track?.destroy();
        this._track = null;
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
        super._init(0.0, 'Cursor Usage');
        this._extensionPath = extensionPath;
        this._settings = settings;
        this._openPreferences = openPreferences;
        this._session = this._createSession();
        this._lastUsage = null;
        this._countdownTimer = null;
        this._timerId = null;
        this._settingsChangedId = null;
        this._cancellable = null;
        this._tokenProc = null;

        const box = new St.BoxLayout({style_class: 'cu-panel'});
        this._icon = new St.Icon({
            gicon: Gio.icon_new_for_string(GLib.build_filenamev([this._extensionPath, 'icon-22.png'])),
            style_class: 'cu-panel-icon',
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
            else if (key === 'usage-display' || key === 'panel-window' || key === 'show-billing')
                this._renderFromLastUsage();
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
        this._label.visible = true;
        this._panelTier.visible = this._settings.get_boolean('show-tier');
        this._renderPanel();
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

        const header = new St.BoxLayout({style_class: 'cu-header'});
        this._title = new St.Label({text: 'Cursor', style_class: 'cu-title', x_expand: true});
        this._tier = new St.Label({text: '', style_class: 'cu-tier'});
        header.add_child(this._title);
        header.add_child(this._tier);
        root.add_child(header);

        this._autoMeter = new Meter('Auto');
        this._apiMeter = new Meter('API');
        root.add_child(this._autoMeter.root);
        root.add_child(this._apiMeter.root);

        this._billing = new St.Label({text: '', style_class: 'cu-billing'});
        this._billing.visible = false;
        root.add_child(this._billing);

        this._error = new St.Label({text: '', style_class: 'cu-error'});
        this._error.visible = false;
        root.add_child(this._error);

        const footer = new St.BoxLayout({style_class: 'cu-footer'});
        this._updated = new St.Label({
            text: '',
            style_class: 'cu-updated',
            x_expand: true,
            y_align: Clutter.ActorAlign.CENTER,
        });
        footer.add_child(this._updated);

        const refresh = new St.Button({
            label: 'Refresh',
            style_class: 'cu-link',
            can_focus: true,
            reactive: true,
            track_hover: true,
        });
        refresh.connect('clicked', () => this._refreshUsage());
        footer.add_child(refresh);

        const open = new St.Button({
            label: 'Open',
            style_class: 'cu-link',
            can_focus: true,
            reactive: true,
            track_hover: true,
        });
        open.connect('clicked', () => {
            this.menu.close();
            Gio.AppInfo.launch_default_for_uri(USAGE_PAGE_URL, null);
        });
        footer.add_child(open);

        const prefs = new St.Button({
            label: 'Prefs',
            style_class: 'cu-link',
            can_focus: true,
            reactive: true,
            track_hover: true,
        });
        prefs.connect('clicked', () => {
            this.menu.close();
            this._openPreferences();
        });
        footer.add_child(prefs);
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

    _cliAuthPath() {
        return GLib.build_filenamev([GLib.get_home_dir(), '.config', 'cursor', 'auth.json']);
    }

    _desktopDbPath() {
        return GLib.build_filenamev([
            GLib.get_home_dir(), '.config', 'Cursor', 'User', 'globalStorage', 'state.vscdb',
        ]);
    }

    _refreshUsage() {
        const cancellable = this._newCancellable();
        const envToken = (GLib.getenv('CURSOR_SESSION_TOKEN') ?? '').trim();
        if (envToken) {
            const token = envToken.includes('::') || envToken.includes('%3A%3A')
                ? envToken.split(/::|%3A%3A/).pop()
                : envToken;
            this._fetchUsage(token, cancellable);
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
                    this._fetchUsage(token, cancellable);
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
            this._setUnavailable('-', 'Login required');
            return;
        }

        // Prefer sqlite3 over embedding another language runtime (EGO review).
        try {
            const proc = Gio.Subprocess.new(
                [
                    'sqlite3',
                    dbPath,
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
                        this._setUnavailable('-', 'No auth');
                        return;
                    }
                    this._fetchUsage(token, cancellable);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    this._setUnavailable('-', 'No auth');
                }
            });
        } catch (_e) {
            this._setUnavailable('-', 'No auth');
        }
    }

    _fetchUsage(accessToken, cancellable) {
        const cookie = sessionCookieFromToken(accessToken);
        if (!cookie) {
            this._setUnavailable('!', 'Bad token');
            return;
        }

        const message = Soup.Message.new('GET', USAGE_API_URL);
        message.request_headers.append('Cookie', `WorkosCursorSessionToken=${cookie}`);
        message.request_headers.append('User-Agent', 'cursor-usage-extension');

        this._session.send_and_read_async(
            message,
            GLib.PRIORITY_DEFAULT,
            cancellable,
            (session, result) => {
                try {
                    const bytes = session.send_and_read_finish(result);
                    if (message.status_code !== 200) {
                        this._setUnavailable('!', `HTTP ${message.status_code}`);
                        return;
                    }
                    const data = JSON.parse(new TextDecoder('utf-8').decode(bytes.get_data()));
                    this._render(this._normalize(data));
                    this._stamp(true);
                } catch (_e) {
                    if (cancellable.is_cancelled())
                        return;
                    this._setUnavailable('!', 'API failed');
                }
            }
        );
    }

    _normalize(data) {
        const plan = data.individualUsage?.plan ?? {};
        const onDemand = data.individualUsage?.onDemand ?? null;
        const end = data.billingCycleEnd ?? null;
        const startMs = Date.parse(data.billingCycleStart ?? '');
        const endMs = Date.parse(end ?? '');
        const cycleSeconds = !Number.isNaN(startMs) && !Number.isNaN(endMs) && endMs > startMs
            ? (endMs - startMs) / 1000
            : null;

        const fromMsg = msg => {
            const m = `${msg ?? ''}`.match(/([\d.]+)\s*%/);
            return m ? Number(m[1]) : null;
        };

        const auto = this._pct(plan.autoPercentUsed ?? fromMsg(data.autoModelSelectedDisplayMessage));
        const api = this._pct(plan.apiPercentUsed ?? fromMsg(data.namedModelSelectedDisplayMessage));
        const total = this._pct(plan.totalPercentUsed ?? Math.max(auto, api));

        return {
            tier: planLabel(data.membershipType),
            isUnlimited: !!data.isUnlimited,
            cycleSeconds,
            auto: {utilization: auto, resets_at: end},
            api: {utilization: api, resets_at: end},
            total: {utilization: total, resets_at: end},
            onDemand: onDemand?.enabled
                ? {used: onDemand.used ?? 0, limit: onDemand.limit}
                : null,
            planSpend: plan.enabled
                ? {
                    included: plan.breakdown?.included ?? null,
                    bonus: plan.breakdown?.bonus ?? null,
                }
                : null,
        };
    }

    _setUnavailable(label, detail) {
        this._lastUsage = null;
        this._label.text = label;
        this._label.style_class = 'cu-panel-pct usage-high';
        this._ring.setUnknown();
        this._autoMeter.setMuted(detail);
        this._apiMeter.setMuted('-');
        this._billing.visible = false;
        this._error.text = detail;
        this._error.visible = true;
        this._stamp(false);
        this._scheduleCountdown();
    }

    _render(data) {
        this._lastUsage = data;
        this._error.visible = false;
        this._tier.text = data.tier;
        this._panelTier.text = data.tier;

        if (data.isUnlimited) {
            this._autoMeter.setMuted('unlimited');
            this._apiMeter.setMuted('unlimited');
        } else {
            this._applyMeter(this._autoMeter, data.auto);
            this._applyMeter(this._apiMeter, data.api);
        }

        this._renderBilling(data);
        this._renderPanel();
        this._scheduleCountdown();
    }

    _renderFromLastUsage() {
        if (this._lastUsage)
            this._render(this._lastUsage);
    }

    _renderBilling(data) {
        if (!this._settings.get_boolean('show-billing')) {
            this._billing.visible = false;
            return;
        }
        const parts = [];
        if (data.planSpend?.included != null) {
            let t = centsLabel(data.planSpend.included);
            if (data.planSpend.bonus)
                t += ` + ${centsLabel(data.planSpend.bonus)} bonus`;
            parts.push(t);
        }
        if (data.onDemand) {
            const lim = data.onDemand.limit == null ? '∞' : centsLabel(data.onDemand.limit);
            parts.push(`on-demand ${centsLabel(data.onDemand.used)} / ${lim}`);
        }
        this._billing.text = parts.join(' · ');
        this._billing.visible = parts.length > 0;
    }

    _applyMeter(meter, win) {
        if (!win) {
            meter.setMuted();
            return;
        }
        const util = this._pct(win.utilization);
        const display = this._settings.get_string('usage-display') === 'remaining'
            ? 100 - util
            : util;
        const suffix = this._settings.get_string('usage-display') === 'remaining'
            ? 'left'
            : 'used';
        meter.setValue(util, relativeReset(win.resets_at), display, suffix);
    }

    _selectedWindow() {
        const u = this._lastUsage;
        if (!u)
            return null;
        switch (this._settings.get_string('panel-window')) {
        case 'api':
            return u.api;
        case 'total':
            return u.total;
        case 'max':
            return (u.api?.utilization ?? 0) > (u.auto?.utilization ?? 0) ? u.api : u.auto;
        case 'auto':
        default:
            return u.auto;
        }
    }

    _renderPanel() {
        const win = this._selectedWindow();
        if (!win) {
            this._label.text = '-';
            this._label.style_class = 'cu-panel-pct';
            this._ring.setUnknown();
            return;
        }
        if (this._lastUsage?.isUnlimited) {
            this._label.text = '∞';
            this._label.style_class = 'cu-panel-pct usage-low';
            this._ring.setValue(0);
            return;
        }
        const util = this._pct(win.utilization);
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
        const end = this._lastUsage?.auto?.resets_at;
        if (!end)
            return;
        const seconds = (Date.parse(end) - Date.now()) / 1000;
        if (!(seconds > 0))
            return;
        this._countdownTimer = GLib.timeout_add_seconds(
            GLib.PRIORITY_DEFAULT,
            seconds < 90 ? 1 : 30,
            () => {
                this._countdownTimer = null;
                this._renderFromLastUsage();
                return GLib.SOURCE_REMOVE;
            }
        );
    }

    _pct(value) {
        const n = typeof value === 'number' && Number.isFinite(value) ? value : 0;
        return Math.min(100, Math.max(0, n));
    }

    _stamp(ok) {
        const now = GLib.DateTime.new_now_local();
        this._updated.text = `${ok ? 'Updated' : 'Checked'} ${now.format('%H:%M')}`;
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
        this._lastUsage = null;
        this._autoMeter?.destroy();
        this._autoMeter = null;
        this._apiMeter?.destroy();
        this._apiMeter = null;
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
