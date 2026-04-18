#ifndef USBDAC_MANAGER_H
#define USBDAC_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <libusb-1.0/libusb.h>

typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    uint16_t vid;
    uint16_t pid;
} UsbDacManager;

UsbDacManager* usbdac_manager_create(void);
void usbdac_manager_destroy(UsbDacManager* mgr);

bool usbdac_is_device_present(UsbDacManager* mgr);

bool usbdac_open_connection(UsbDacManager* mgr);
void usbdac_close_connection(UsbDacManager* mgr);

bool usbdac_send_command(UsbDacManager* mgr, uint8_t command_id, uint8_t value1, uint8_t value2, uint8_t value3);
bool usbdac_read_data(UsbDacManager* mgr, uint8_t command_id, uint8_t* out_buffer);

bool usbdac_check_presence_and_permission(UsbDacManager* mgr, char* path_out, bool* needs_perm_out);

#endif // USBDAC_MANAGER_H
