#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "magivant.h"
#include "usbdac_manager.h"

typedef struct {
    GtkWidget *window;
    GtkWidget *status_pill;
    GtkWidget *fw_label;
    GtkWidget *connected_box;
    GtkWidget *disconnected_box;
    GtkWidget *permission_box; 
    
    GtkWidget *lbl_vol_val;
    GtkWidget *lbl_bal_val;
    GtkWidget *vol_scale;
    GtkWidget *bal_scale;
    GtkWidget *gain_switch;
    GtkWidget *filter_combo;
    GtkWidget *led_combo;
    GtkWidget *lbl_disc_msg; 
    GtkWidget *spinner; 
    
    MagivantManager *mag_mgr;
    UsbDacManager *usb_mgr;
    
    bool needs_permission;
    bool auth_triggered; 
    char device_path[128];
} AppWidgets;

static void apply_modern_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css =
        /* Menggunakan warna foreground (teks) dari tema sistem dengan opacity (alpha) */
        ".card { background-color: alpha(@theme_fg_color, 0.05); border-radius: 16px; padding: 20px; border: 1px solid alpha(@theme_fg_color, 0.1); margin-bottom: 15px; }"
        ".card-warning { background-color: alpha(#eab308, 0.1); border-radius: 16px; padding: 24px; border: 1px solid alpha(#eab308, 0.3); margin-top: 20px; }"
        
        ".title-large { font-size: 20pt; font-weight: 800; }"
        ".title-medium { font-size: 13pt; font-weight: bold; opacity: 0.9; }"
        ".label-small { font-size: 10pt; opacity: 0.6; }"
        
        ".status-pill { border-radius: 12px; padding: 6px 14px; font-weight: bold; font-size: 9pt; }"
        /* Semantic colors tetap dipertahankan agar indikator status selalu jelas */
        ".pill-connected { background-color: alpha(#22c55e, 0.2); color: #16a34a; }" 
        ".pill-disconnected { background-color: alpha(#ef4444, 0.2); color: #dc2626; }" 
        ".pill-warning { background-color: alpha(#eab308, 0.2); color: #ca8a04; }" 
        
        "scale trough { min-height: 6px; border-radius: 3px; }"
        "scale slider { min-width: 18px; min-height: 18px; border-radius: 9px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_volume_changed(GtkRange *r, gpointer d) { magivant_set_volume(((AppWidgets*)d)->mag_mgr, (int)gtk_range_get_value(r)); }
static void on_balance_changed(GtkRange *r, gpointer d) { magivant_set_balance(((AppWidgets*)d)->mag_mgr, (int)gtk_range_get_value(r)); }
static gboolean on_gain_switched(GtkSwitch *w, gboolean s, gpointer d) { magivant_set_gain(((AppWidgets*)d)->mag_mgr, s); return FALSE; }
static void on_filter_changed(GtkComboBox *w, gpointer d) { magivant_set_digital_filter(((AppWidgets*)d)->mag_mgr, gtk_combo_box_get_active(w)); }
static void on_led_changed(GtkComboBox *w, gpointer d) { magivant_set_led(((AppWidgets*)d)->mag_mgr, gtk_combo_box_get_active(w)); }

static gboolean update_ui_on_main_thread(gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    DacUiState *state = &app->mag_mgr->ui_state;

    GtkStyleContext *sc = gtk_widget_get_style_context(app->status_pill);
    gtk_style_context_remove_class(sc, "pill-disconnected");
    gtk_style_context_remove_class(sc, "pill-connected");
    gtk_style_context_remove_class(sc, "pill-warning");

    if (app->needs_permission) {
        gtk_label_set_text(GTK_LABEL(app->status_pill), "AUTHENTICATING");
        gtk_style_context_add_class(sc, "pill-warning");
        gtk_label_set_text(GTK_LABEL(app->fw_label), "Waiting for OS authentication...");
        
        gtk_widget_hide(app->connected_box);
        gtk_widget_hide(app->disconnected_box);
        gtk_widget_show_all(app->permission_box);
        gtk_spinner_start(GTK_SPINNER(app->spinner));

        if (!app->auth_triggered) {
            app->auth_triggered = true;
            uint16_t vid = app->usb_mgr->vid;
            uint16_t pid = app->usb_mgr->pid;
            char cmd[512];
            snprintf(cmd, sizeof(cmd), 
                "pkexec sh -c \"echo 'SUBSYSTEM==\\\"usb\\\", ATTR{idVendor}==\\\"%04x\\\", ATTR{idProduct}==\\\"%04x\\\", MODE=\\\"0666\\\"' > /etc/udev/rules.d/99-magivant.rules && udevadm control --reload-rules && udevadm trigger\"", 
                vid, pid);
            g_spawn_command_line_async(cmd, NULL);
        }

    } else if (state->is_connected) {
        app->auth_triggered = false;
        gtk_spinner_stop(GTK_SPINNER(app->spinner));

        gtk_label_set_text(GTK_LABEL(app->status_pill), "CONNECTED");
        gtk_style_context_add_class(sc, "pill-connected");

        char fw_str[64]; snprintf(fw_str, sizeof(fw_str), "Firmware Version: %s", state->firmware_version);
        gtk_label_set_text(GTK_LABEL(app->fw_label), fw_str);

        char vol_str[32]; snprintf(vol_str, sizeof(vol_str), "Volume Index: %d", state->volume_index);
        gtk_label_set_text(GTK_LABEL(app->lbl_vol_val), vol_str);

        char bal_str[32];
        if (state->balance_base_value < 0) snprintf(bal_str, sizeof(bal_str), "Channel Balance: Left %d", abs(state->balance_base_value));
        else if (state->balance_base_value > 0) snprintf(bal_str, sizeof(bal_str), "Channel Balance: Right %d", state->balance_base_value);
        else snprintf(bal_str, sizeof(bal_str), "Channel Balance: Center");
        gtk_label_set_text(GTK_LABEL(app->lbl_bal_val), bal_str);

        g_signal_handlers_block_by_func(app->vol_scale, on_volume_changed, app);
        g_signal_handlers_block_by_func(app->bal_scale, on_balance_changed, app);
        g_signal_handlers_block_by_func(app->gain_switch, on_gain_switched, app);
        g_signal_handlers_block_by_func(app->filter_combo, on_filter_changed, app);
        g_signal_handlers_block_by_func(app->led_combo, on_led_changed, app);

        gtk_range_set_value(GTK_RANGE(app->vol_scale), state->volume_index);
        gtk_range_set_value(GTK_RANGE(app->bal_scale), state->balance_base_value);
        gtk_switch_set_active(GTK_SWITCH(app->gain_switch), state->is_high_gain);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->filter_combo), state->digital_filter_pos);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->led_combo), state->led_pos);

        g_signal_handlers_unblock_by_func(app->vol_scale, on_volume_changed, app);
        g_signal_handlers_unblock_by_func(app->bal_scale, on_balance_changed, app);
        g_signal_handlers_unblock_by_func(app->gain_switch, on_gain_switched, app);
        g_signal_handlers_unblock_by_func(app->filter_combo, on_filter_changed, app);
        g_signal_handlers_unblock_by_func(app->led_combo, on_led_changed, app);

        gtk_widget_hide(app->disconnected_box);
        gtk_widget_hide(app->permission_box);
        gtk_widget_show(app->connected_box);
    } else {
        app->auth_triggered = false; 
        gtk_spinner_stop(GTK_SPINNER(app->spinner));

        gtk_label_set_text(GTK_LABEL(app->status_pill), "DISCONNECTED");
        gtk_style_context_add_class(sc, "pill-disconnected");
        gtk_label_set_text(GTK_LABEL(app->fw_label), "Waiting for device...");

        gtk_widget_hide(app->connected_box);
        gtk_widget_hide(app->permission_box);
        gtk_widget_show(app->disconnected_box);
    }
    return G_SOURCE_REMOVE;
}

static void on_magivant_state_changed(const DacUiState* state, void* user_data) {
    g_idle_add(update_ui_on_main_thread, user_data);
}

static gboolean auto_detect_timer(gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    
    char dev_path[128] = {0};
    bool needs_perm = false;
    bool is_physically_present = usbdac_check_presence_and_permission(app->usb_mgr, dev_path, &needs_perm);
    bool is_logically_connected = app->mag_mgr->ui_state.is_connected;

    if (is_physically_present) {
        if (needs_perm) {
            if (!app->needs_permission) {
                app->needs_permission = true;
                g_idle_add(update_ui_on_main_thread, app);
            }
        } else {
            if (app->needs_permission || !is_logically_connected) {
                app->needs_permission = false;
                if (usbdac_open_connection(app->usb_mgr)) {
                    app->mag_mgr->usb_handle = app->usb_mgr->handle;
                    magivant_on_device_connected(app->mag_mgr);
                }
            }
        }
    } else {
        app->auth_triggered = false;
        
        if (is_logically_connected || app->needs_permission) {
            app->needs_permission = false;
            usbdac_close_connection(app->usb_mgr);
            magivant_on_device_disconnected(app->mag_mgr);
            g_idle_add(update_ui_on_main_thread, app);
        }
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    apply_modern_css();
    
    AppWidgets *app = g_slice_new0(AppWidgets);
    app->usb_mgr = usbdac_manager_create(); 
    app->mag_mgr = magivant_manager_create();
    magivant_set_state_callback(app->mag_mgr, on_magivant_state_changed, app);

    g_timeout_add(1000, auto_detect_timer, app);

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_default_icon_name("magivant");
    
    gtk_window_set_default_size(GTK_WINDOW(app->window), 450, 720);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 450, 720);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(app->window), scroll);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), main_vbox);

    GtkWidget *header_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_hbox, FALSE, FALSE, 5);

    GtkWidget *header_texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *lbl_title = gtk_label_new("Leteciel Magivant");
    gtk_widget_set_halign(lbl_title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "title-large");
    gtk_box_pack_start(GTK_BOX(header_texts), lbl_title, FALSE, FALSE, 0);

    app->fw_label = gtk_label_new("Firmware: --");
    gtk_widget_set_halign(app->fw_label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->fw_label), "label-small");
    gtk_box_pack_start(GTK_BOX(header_texts), app->fw_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_hbox), header_texts, TRUE, TRUE, 0);

    app->status_pill = gtk_label_new("DISCONNECTED");
    gtk_style_context_add_class(gtk_widget_get_style_context(app->status_pill), "status-pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(app->status_pill), "pill-disconnected");
    gtk_widget_set_halign(app->status_pill, GTK_ALIGN_END);
    gtk_widget_set_valign(app->status_pill, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(header_hbox), app->status_pill, FALSE, FALSE, 0);
    
    app->disconnected_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    GtkWidget *icon_disc = gtk_image_new_from_icon_name("media-removable-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_opacity(icon_disc, 0.3);
    gtk_box_pack_start(GTK_BOX(app->disconnected_box), icon_disc, TRUE, FALSE, 50);
    
    app->lbl_disc_msg = gtk_label_new("<span size='large' weight='bold'>Device Not Found</span>\n\nPlease connect your DAC via USB.");
    gtk_label_set_use_markup(GTK_LABEL(app->lbl_disc_msg), TRUE);
    gtk_label_set_justify(GTK_LABEL(app->lbl_disc_msg), GTK_JUSTIFY_CENTER);
    gtk_widget_set_opacity(app->lbl_disc_msg, 0.6);
    gtk_box_pack_start(GTK_BOX(app->disconnected_box), app->lbl_disc_msg, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), app->disconnected_box, TRUE, TRUE, 0);

    app->permission_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->permission_box), "card-warning");
    
    app->spinner = gtk_spinner_new();
    gtk_widget_set_size_request(app->spinner, 32, 32);
    gtk_box_pack_start(GTK_BOX(app->permission_box), app->spinner, FALSE, FALSE, 10);
    
    GtkWidget *lbl_perm_title = gtk_label_new("<span size='large' weight='bold'>Configuring Device</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_perm_title), TRUE);
    gtk_widget_set_halign(lbl_perm_title, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(app->permission_box), lbl_perm_title, FALSE, FALSE, 0);
    
    GtkWidget *lbl_perm_desc = gtk_label_new("The system is configuring permanent USB access.\nPlease enter your root password in the authentication prompt.");
    gtk_label_set_justify(GTK_LABEL(lbl_perm_desc), GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_perm_desc), "label-small");
    gtk_box_pack_start(GTK_BOX(app->permission_box), lbl_perm_desc, FALSE, FALSE, 10);

    GtkWidget *lbl_perm_foot = gtk_label_new("*After authentication, please disconnect and reconnect your DAC.");
    gtk_widget_set_halign(lbl_perm_foot, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_perm_foot), "label-small");
    gtk_widget_set_opacity(lbl_perm_foot, 0.5);
    gtk_box_pack_start(GTK_BOX(app->permission_box), lbl_perm_foot, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_vbox), app->permission_box, FALSE, FALSE, 20);

    app->connected_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), app->connected_box, TRUE, TRUE, 0);

    GtkWidget *card1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_style_context_add_class(gtk_widget_get_style_context(card1), "card");
    gtk_box_pack_start(GTK_BOX(app->connected_box), card1, FALSE, FALSE, 0);

    app->lbl_vol_val = gtk_label_new("Volume Index: 0");
    gtk_widget_set_halign(app->lbl_vol_val, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_vol_val), "title-medium");
    gtk_box_pack_start(GTK_BOX(card1), app->lbl_vol_val, FALSE, FALSE, 0);
    app->vol_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 60, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->vol_scale), FALSE);
    g_signal_connect(app->vol_scale, "value-changed", G_CALLBACK(on_volume_changed), app);
    gtk_box_pack_start(GTK_BOX(card1), app->vol_scale, TRUE, TRUE, 5);

    app->lbl_bal_val = gtk_label_new("Channel Balance: Center");
    gtk_widget_set_halign(app->lbl_bal_val, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_bal_val), "title-medium");
    gtk_box_pack_start(GTK_BOX(card1), app->lbl_bal_val, FALSE, FALSE, 10);
    app->bal_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -50, 50, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->bal_scale), FALSE);
    gtk_scale_add_mark(GTK_SCALE(app->bal_scale), 0, GTK_POS_BOTTOM, NULL);
    g_signal_connect(app->bal_scale, "value-changed", G_CALLBACK(on_balance_changed), app);
    gtk_box_pack_start(GTK_BOX(card1), app->bal_scale, TRUE, TRUE, 5);

    GtkWidget *card2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_style_context_add_class(gtk_widget_get_style_context(card2), "card");
    gtk_box_pack_start(GTK_BOX(app->connected_box), card2, FALSE, FALSE, 0);

    GtkWidget *vbox_gain = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *lbl_gain_val = gtk_label_new("High Gain Mode");
    gtk_widget_set_halign(lbl_gain_val, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_gain_val), "title-medium");
    gtk_box_pack_start(GTK_BOX(vbox_gain), lbl_gain_val, FALSE, FALSE, 0);
    GtkWidget *lbl_gain_sub = gtk_label_new("Extra power for demanding headphones");
    gtk_widget_set_halign(lbl_gain_sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_gain_sub), "label-small");
    gtk_box_pack_start(GTK_BOX(vbox_gain), lbl_gain_sub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card2), vbox_gain, TRUE, TRUE, 0);

    app->gain_switch = gtk_switch_new();
    gtk_widget_set_valign(app->gain_switch, GTK_ALIGN_CENTER);
    g_signal_connect(app->gain_switch, "state-set", G_CALLBACK(on_gain_switched), app);
    gtk_box_pack_end(GTK_BOX(card2), app->gain_switch, FALSE, FALSE, 0);

    GtkWidget *card3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_style_context_add_class(gtk_widget_get_style_context(card3), "card");
    gtk_box_pack_start(GTK_BOX(app->connected_box), card3, FALSE, FALSE, 0);

    GtkWidget *hbox_filter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget *lbl_filter = gtk_label_new("Digital Filter");
    gtk_widget_set_halign(lbl_filter, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_filter), "title-medium");
    gtk_box_pack_start(GTK_BOX(hbox_filter), lbl_filter, TRUE, TRUE, 0);

    app->filter_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Fast LL");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Fast PC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Slow LL");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Slow PC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "NOS");
    g_signal_connect(app->filter_combo, "changed", G_CALLBACK(on_filter_changed), app);
    gtk_box_pack_end(GTK_BOX(hbox_filter), app->filter_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card3), hbox_filter, FALSE, FALSE, 0);

    GtkWidget *hbox_led = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget *lbl_led = gtk_label_new("LED Settings");
    gtk_widget_set_halign(lbl_led, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_led), "title-medium");
    gtk_box_pack_start(GTK_BOX(hbox_led), lbl_led, TRUE, TRUE, 0);

    app->led_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->led_combo), "On");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->led_combo), "Off (No Save)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->led_combo), "Off (Save)");
    g_signal_connect(app->led_combo, "changed", G_CALLBACK(on_led_changed), app);
    gtk_box_pack_end(GTK_BOX(hbox_led), app->led_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card3), hbox_led, FALSE, FALSE, 10);

    gtk_widget_show_all(app->window);
    gtk_widget_hide(app->connected_box);
    gtk_widget_hide(app->permission_box);
    g_idle_add(update_ui_on_main_thread, app);

    gtk_main();

    magivant_manager_destroy(app->mag_mgr);
    usbdac_manager_destroy(app->usb_mgr);
    g_slice_free(AppWidgets, app);

    return 0;
}