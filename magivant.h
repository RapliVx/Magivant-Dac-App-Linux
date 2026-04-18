#ifndef MAGIVANT_H
#define MAGIVANT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct {
    bool is_connected;
    char firmware_version[16];
    int volume_index;
    int balance_base_value;
    bool is_high_gain;
    int digital_filter_pos;
    int led_pos;
} DacUiState;

typedef struct {
    DacUiState ui_state;
    pthread_mutex_t state_mutex;
    pthread_t polling_thread;
    bool is_polling;
    void (*on_state_changed)(const DacUiState* state, void* user_data);
    void* user_data;
    void* usb_handle; 
} MagivantManager;

MagivantManager* magivant_manager_create(void);
void magivant_manager_destroy(MagivantManager* mgr);
void magivant_set_state_callback(MagivantManager* mgr, void (*cb)(const DacUiState*, void*), void* user_data);

void magivant_on_device_connected(MagivantManager* mgr);
void magivant_on_device_disconnected(MagivantManager* mgr);

void magivant_set_volume(MagivantManager* mgr, int index);
void magivant_set_balance(MagivantManager* mgr, int balance);
void magivant_set_gain(MagivantManager* mgr, bool is_high);
void magivant_set_digital_filter(MagivantManager* mgr, int pos);
void magivant_set_led(MagivantManager* mgr, int pos);

#endif // MAGIVANT_H