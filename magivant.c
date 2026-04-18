#include "magivant.h"
#include "usbdac_manager.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const unsigned char APP_VOLUME_INT_LIST[] = {
    0xFF, 0xC8, 0xB4, 0xAA, 0xA0, 0x96, 0x8C, 0x82, 0x7A, 0x74,
    0x6E, 0x6A, 0x66, 0x62, 0x5E, 0x5A, 0x58, 0x56, 0x54, 0x52,
    0x50, 0x4E, 0x4C, 0x4A, 0x48, 0x46, 0x44, 0x42, 0x40, 0x3E,
    0x3C, 0x3A, 0x38, 0x36, 0x34, 0x32, 0x30, 0x2E, 0x2C, 0x2A,
    0x28, 0x26, 0x24, 0x22, 0x20, 0x1E, 0x1C, 0x1A, 0x18, 0x16,
    0x14, 0x12, 0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00
};

static void update_state_and_notify(MagivantManager* mgr) {
    if (mgr->on_state_changed) {
        mgr->on_state_changed(&mgr->ui_state, mgr->user_data);
    }
}

MagivantManager* magivant_manager_create(void) {
    MagivantManager* mgr = (MagivantManager*)malloc(sizeof(MagivantManager));
    memset(mgr, 0, sizeof(MagivantManager));
    pthread_mutex_init(&mgr->state_mutex, NULL);
    return mgr;
}

void magivant_manager_destroy(MagivantManager* mgr) {
    magivant_on_device_disconnected(mgr);
    pthread_mutex_destroy(&mgr->state_mutex);
    free(mgr);
}

void magivant_set_state_callback(MagivantManager* mgr, void (*cb)(const DacUiState*, void*), void* user_data) {
    mgr->on_state_changed = cb;
    mgr->user_data = user_data;
}

static void* polling_loop_thread(void* arg) {
    MagivantManager* mgr = (MagivantManager*)arg;
    uint8_t buf[16];

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;

    while (mgr->is_polling) {
        usleep(500000);

        pthread_mutex_lock(&mgr->state_mutex);
        if (usbdac_read_data(&usb_temp, -94, buf)) {
            int vol_index = 0;
            for (int i = 0; i < sizeof(APP_VOLUME_INT_LIST); i++) {
                if (APP_VOLUME_INT_LIST[i] == buf[4]) {
                    vol_index = i; break;
                }
            }
            mgr->ui_state.volume_index = vol_index;
            int left = buf[5];
            int right = buf[6];
            mgr->ui_state.balance_base_value = (left > 0) ? left : ((right > 0) ? -right : 0);
        }

        if (usbdac_read_data(&usb_temp, -93, buf)) {
            mgr->ui_state.digital_filter_pos = buf[3];
            mgr->ui_state.is_high_gain = (buf[4] == 1);
            mgr->ui_state.led_pos = buf[5];
        }
        update_state_and_notify(mgr);
        pthread_mutex_unlock(&mgr->state_mutex);
    }
    return NULL;
}

void magivant_on_device_connected(MagivantManager* mgr) {
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.is_connected = true;
    
    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    uint8_t buf[16];

    if (usbdac_read_data(&usb_temp, -96, buf)) {
        snprintf(mgr->ui_state.firmware_version, sizeof(mgr->ui_state.firmware_version), "%d.%d.%d", buf[3], buf[4], buf[5]);
    }
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    if (!mgr->is_polling) {
        mgr->is_polling = true;
        pthread_create(&mgr->polling_thread, NULL, polling_loop_thread, mgr);
    }
}

void magivant_on_device_disconnected(MagivantManager* mgr) {
    if (mgr->is_polling) {
        mgr->is_polling = false;
        pthread_join(mgr->polling_thread, NULL);
    }
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.is_connected = false;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);
}

void magivant_set_volume(MagivantManager* mgr, int index) {
    if (index < 0 || index >= sizeof(APP_VOLUME_INT_LIST)) return;
    
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.volume_index = index;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    usbdac_send_command(&usb_temp, 4, APP_VOLUME_INT_LIST[index], 0, 0);
}

void magivant_set_balance(MagivantManager* mgr, int balance) {
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.balance_base_value = balance;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    
    uint8_t abs_val = (uint8_t)abs(balance);
    if (balance < 0) {
        usbdac_send_command(&usb_temp, 5, 0, abs_val, 0);
    } else if (balance > 0) {
        usbdac_send_command(&usb_temp, 5, abs_val, 0, 0);
    } else {
        usbdac_send_command(&usb_temp, 5, 0, 0, 0);
    }
}

void magivant_set_gain(MagivantManager* mgr, bool is_high) {
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.is_high_gain = is_high;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    usbdac_send_command(&usb_temp, 2, is_high ? 1 : 0, 0, 0);
}

void magivant_set_digital_filter(MagivantManager* mgr, int pos) {
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.digital_filter_pos = pos;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    usbdac_send_command(&usb_temp, 1, (uint8_t)pos, 0, 0);
}

void magivant_set_led(MagivantManager* mgr, int pos) {
    pthread_mutex_lock(&mgr->state_mutex);
    mgr->ui_state.led_pos = pos;
    update_state_and_notify(mgr);
    pthread_mutex_unlock(&mgr->state_mutex);

    UsbDacManager usb_temp;
    usb_temp.handle = (libusb_device_handle*)mgr->usb_handle;
    usbdac_send_command(&usb_temp, 6, (uint8_t)pos, 0, 0);
}