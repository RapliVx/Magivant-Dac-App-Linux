#include "usbdac_manager.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DAC_MAGIC_1 0xC1
#define DAC_MAGIC_2 0xA7

#define REQ_TYPE_OUT 67
#define REQ_TYPE_IN  195
#define REQ_OUT      160
#define REQ_IN       161
#define INDEX_VAL    2464
#define TIMEOUT_MS   3000

static bool find_magivant_via_lsusb(uint16_t *vid, uint16_t *pid) {
    FILE *fp = popen("lsusb", "r");
    if (!fp) return false;

    char line[512];
    bool found = false;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "MAGIVANT") != NULL || strstr(line, "LETECIEL") != NULL) {
            char *id_ptr = strstr(line, "ID ");
            if (id_ptr != NULL) {
                if (sscanf(id_ptr, "ID %hx:%hx", vid, pid) == 2) {
                    found = true;
                    break;
                }
            }
        }
    }
    pclose(fp);
    return found;
}

UsbDacManager* usbdac_manager_create(void) {
    UsbDacManager* mgr = (UsbDacManager*)malloc(sizeof(UsbDacManager));
    mgr->vid = 0;
    mgr->pid = 0;
    mgr->handle = NULL;

    if (libusb_init(&mgr->ctx) < 0) {
        free(mgr);
        return NULL;
    }
    return mgr;
}

void usbdac_manager_destroy(UsbDacManager* mgr) {
    if (!mgr) return;
    usbdac_close_connection(mgr);
    libusb_exit(mgr->ctx);
    free(mgr);
}

bool usbdac_is_device_present(UsbDacManager* mgr) {
    if (!mgr) return false;
    uint16_t dynamic_vid = 0, dynamic_pid = 0;

    if (find_magivant_via_lsusb(&dynamic_vid, &dynamic_pid)) {
        mgr->vid = dynamic_vid;
        mgr->pid = dynamic_pid;
        return true;
    }

    mgr->vid = 0;
    mgr->pid = 0;
    return false;
}

bool usbdac_check_presence_and_permission(UsbDacManager* mgr, char* path_out, bool* needs_perm_out) {
    *needs_perm_out = false;
    path_out[0] = '\0';

    if (!usbdac_is_device_present(mgr)) {
        return false;
    }

    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(mgr->ctx, &devs);
    bool libusb_found = false;

    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devs[i], &desc);

        if (desc.idVendor == mgr->vid && desc.idProduct == mgr->pid) {
            uint8_t bus = libusb_get_bus_number(devs[i]);
            uint8_t addr = libusb_get_device_address(devs[i]);

            sprintf(path_out, "/dev/bus/usb/%03d/%03d", bus, addr);

            if (access(path_out, R_OK | W_OK) != 0) {
                *needs_perm_out = true;
            }

            libusb_found = true;
            break;
        }
    }
    libusb_free_device_list(devs, 1);
    return libusb_found;
}

bool usbdac_open_connection(UsbDacManager* mgr) {
    if (mgr->handle != NULL) return true;
    if (mgr->vid == 0 || mgr->pid == 0) return false;

    mgr->handle = libusb_open_device_with_vid_pid(mgr->ctx, mgr->vid, mgr->pid);
    return (mgr->handle != NULL);
}

void usbdac_close_connection(UsbDacManager* mgr) {
    if (mgr->handle != NULL) {
        libusb_close(mgr->handle);
        mgr->handle = NULL;
    }
}

bool usbdac_send_command(UsbDacManager* mgr, uint8_t command_id, uint8_t value1, uint8_t value2, uint8_t value3) {
    if (mgr->handle == NULL) return false;

    uint8_t payload[7] = {DAC_MAGIC_1, DAC_MAGIC_2, command_id, value1, value2, value3, 0};

    int sum = 0;
    for (int i = 0; i < 6; i++) sum += payload[i];
    payload[6] = (uint8_t)(sum & 0xFF);

    int result = libusb_control_transfer(mgr->handle, REQ_TYPE_OUT, REQ_OUT, 0, INDEX_VAL, payload, 7, TIMEOUT_MS);
    return (result == 7);
}

bool usbdac_read_data(UsbDacManager* mgr, uint8_t command_id, uint8_t* out_buffer) {
    if (mgr->handle == NULL || out_buffer == NULL) return false;

    uint8_t payload[7] = {DAC_MAGIC_1, DAC_MAGIC_2, command_id, 0, 0, 0, 0};

    int sum = 0;
    for (int i = 0; i < 6; i++) sum += payload[i];
    payload[6] = (uint8_t)(sum & 0xFF);

    int write_result = libusb_control_transfer(mgr->handle, REQ_TYPE_OUT, REQ_OUT, 0, INDEX_VAL, payload, 7, TIMEOUT_MS);

    if (write_result >= 0) {
        usleep(10000);
        int bytes_read = libusb_control_transfer(mgr->handle, REQ_TYPE_IN, REQ_IN, 0, INDEX_VAL, out_buffer, 7, TIMEOUT_MS);
        if (bytes_read == 7) {
            return true;
        }
    }
    return false;
}
