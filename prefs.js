import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk';
import Gio from 'gi://Gio';

import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

export default class CursorUsagePreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage({
            title: 'Cursor Usage',
            icon_name: 'preferences-system-symbolic',
        });
        window.add(page);

        const general = new Adw.PreferencesGroup({title: 'General'});
        page.add(general);

        const refresh = new Adw.SpinRow({
            title: 'Refresh interval',
            subtitle: 'Seconds between updates',
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

        const poolRow = new Adw.ComboRow({
            title: 'Panel pool',
            subtitle: 'Which usage value the panel shows',
        });
        const poolModel = new Gtk.StringList();
        poolModel.append('Most used');
        poolModel.append('Auto');
        poolModel.append('API');
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

        const showIcon = new Adw.SwitchRow({
            title: 'Show icon',
        });
        settings.bind('show-icon', showIcon, 'active', Gio.SettingsBindFlags.DEFAULT);
        display.add(showIcon);

        const showTier = new Adw.SwitchRow({
            title: 'Show plan tier',
        });
        settings.bind('show-tier', showTier, 'active', Gio.SettingsBindFlags.DEFAULT);
        display.add(showTier);

        const menu = new Adw.PreferencesGroup({title: 'Menu'});
        page.add(menu);

        const billing = new Adw.SwitchRow({
            title: 'Show billing line',
            subtitle: 'Included spend and on-demand usage',
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
