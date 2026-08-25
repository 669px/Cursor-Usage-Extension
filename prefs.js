import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk';
import Gio from 'gi://Gio';

import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

export default class CursorUsagePreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage({
            title: 'AI Usage',
            icon_name: 'preferences-system-symbolic',
        });
        window.add(page);

        const general = new Adw.PreferencesGroup({title: 'General'});
        page.add(general);

        const refresh = new Adw.SpinRow({
            title: 'Refresh interval',
            subtitle: 'Seconds between updates (keep ≥120 for Claude)',
            adjustment: new Gtk.Adjustment({
                lower: 10,
                upper: 600,
                step_increment: 10,
                page_increment: 60,
                value: settings.get_int('refresh-interval'),
            }),
        });
        settings.bind('refresh-interval', refresh, 'value', Gio.SettingsBindFlags.DEFAULT);
        general.add(refresh);

        const providers = new Adw.PreferencesGroup({title: 'Providers'});
        page.add(providers);

        for (const [key, title, subtitle] of [
            ['show-cursor', 'Cursor', 'cursor.com usage summary'],
            ['show-claude', 'Claude', 'Claude Code OAuth usage'],
            ['show-codex', 'Codex', 'ChatGPT / Codex rate limits'],
        ]) {
            const row = new Adw.SwitchRow({title, subtitle});
            settings.bind(key, row, 'active', Gio.SettingsBindFlags.DEFAULT);
            providers.add(row);
        }

        const display = new Adw.PreferencesGroup({title: 'Panel'});
        page.add(display);

        const modeRow = new Adw.ComboRow({
            title: 'Display',
            subtitle: 'Ring gauge or percentage only',
        });
        const modeModel = new Gtk.StringList();
        modeModel.append('Ring + percentage');
        modeModel.append('Percentage only');
        modeRow.set_model(modeModel);
        modeRow.set_selected(settings.get_string('display-mode') === 'text' ? 1 : 0);
        modeRow.connect('notify::selected', () => {
            settings.set_string('display-mode', modeRow.get_selected() === 1 ? 'text' : 'ring');
        });
        display.add(modeRow);

        const providerRow = new Adw.ComboRow({
            title: 'Panel provider',
            subtitle: 'Which service the panel percentage follows',
        });
        const providerModel = new Gtk.StringList();
        providerModel.append('Most used');
        providerModel.append('Cursor');
        providerModel.append('Claude');
        providerModel.append('Codex');
        providerRow.set_model(providerModel);
        const pp = settings.get_string('panel-provider');
        providerRow.set_selected(
            pp === 'cursor' ? 1 : pp === 'claude' ? 2 : pp === 'codex' ? 3 : 0
        );
        providerRow.connect('notify::selected', () => {
            settings.set_string(
                'panel-provider',
                ['max', 'cursor', 'claude', 'codex'][providerRow.get_selected()]
            );
        });
        display.add(providerRow);

        const poolRow = new Adw.ComboRow({
            title: 'Panel pool',
            subtitle: 'Pool within the provider (Cursor Auto/API, Claude 5h/7d, Codex primary/weekly)',
        });
        const poolModel = new Gtk.StringList();
        poolModel.append('Most used');
        poolModel.append('Primary / Auto / 5h');
        poolModel.append('Secondary / API / 7d');
        poolModel.append('Total');
        poolRow.set_model(poolModel);
        const pool = settings.get_string('panel-window');
        poolRow.set_selected(pool === 'auto' ? 1 : pool === 'api' ? 2 : pool === 'total' ? 3 : 0);
        poolRow.connect('notify::selected', () => {
            settings.set_string('panel-window', ['max', 'auto', 'api', 'total'][poolRow.get_selected()]);
        });
        display.add(poolRow);

        const usageRow = new Adw.ComboRow({
            title: 'Values',
            subtitle: 'Used or remaining',
        });
        const usageModel = new Gtk.StringList();
        usageModel.append('Used');
        usageModel.append('Remaining');
        usageRow.set_model(usageModel);
        usageRow.set_selected(settings.get_string('usage-display') === 'remaining' ? 1 : 0);
        usageRow.connect('notify::selected', () => {
            settings.set_string('usage-display', usageRow.get_selected() === 1 ? 'remaining' : 'used');
        });
        display.add(usageRow);

        const showIcon = new Adw.SwitchRow({title: 'Show icon'});
        settings.bind('show-icon', showIcon, 'active', Gio.SettingsBindFlags.DEFAULT);
        display.add(showIcon);

        const showTier = new Adw.SwitchRow({title: 'Show plan tier'});
        settings.bind('show-tier', showTier, 'active', Gio.SettingsBindFlags.DEFAULT);
        display.add(showTier);

        const menu = new Adw.PreferencesGroup({title: 'Menu'});
        page.add(menu);

        const billing = new Adw.SwitchRow({
            title: 'Show billing / credits line',
            subtitle: 'Cursor spend, Claude extra usage, Codex credits',
        });
        settings.bind('show-billing', billing, 'active', Gio.SettingsBindFlags.DEFAULT);
        menu.add(billing);

        const network = new Adw.PreferencesGroup({title: 'Network'});
        page.add(network);

        const proxy = new Adw.EntryRow({
            title: 'Proxy URL',
            show_apply_button: true,
        });
        proxy.set_text(settings.get_string('proxy-url'));
        proxy.connect('apply', () => {
            settings.set_string('proxy-url', proxy.get_text());
        });
        network.add(proxy);
    }
}
