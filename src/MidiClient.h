#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
} class_driver_t;

class MidiClient {
    public: 
        const UBaseType_t DAEMON_TASK_PRIORITY = 3;
        static const UBaseType_t CLIENT_NUM_EVENT_MSG = 5;

        static const char* TAG;

        typedef void (*NoteCallback)(uint8_t, uint8_t, uint8_t);

        MidiClient();
        virtual ~MidiClient();

        void begin();

        // Note: currently we support just a single callback
        void registerOnNoteCallback(NoteCallback callback);


    private:
        typedef enum {
            ACTION_OPEN_DEV = 0x01,
            ACTION_GET_DEV_INFO = 0x02,
            ACTION_GET_DEV_DESC = 0x04,
            ACTION_GET_CONFIG_DESC = 0x08,
            ACTION_ENABLE_MIDI = 0x10,
            ACTION_GET_STR_DESC = 0x20,
            ACTION_CLOSE_DEV = 0x40,
            ACTION_EXIT = 0x80,
            ACTION_RECONNECT = 0x100,
        } action_t;

        // static usb_host_client_handle_t Client_Handle;
        static usb_device_handle_t Device_Handle;

        class_driver_t* driver_obj;

        NoteCallback _onNoteCallback;

        TaskHandle_t _class_driver_task_hdl;
        class_driver_t _driver_obj = {0};

        bool _isMidiInterfaceClaimed;
        bool _isMidiEndpointsPrepared;

        static const size_t MIDI_IN_BUFFERS = 8;
        static const size_t MIDI_OUT_BUFFERS = 8;
        usb_transfer_t *MIDIOut = NULL;
        usb_transfer_t* MIDIIn[MIDI_IN_BUFFERS] = {NULL};

        static void daemonTask(void* arg);
        static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
        static void midi_transfer_cb(usb_transfer_t *transfer);

        void runDaemon();
        void client_event_cb_impl(const usb_host_client_event_msg_t *event_msg);
        void action_open_dev();
        void action_get_info();
        void action_get_dev_desc();
        void action_get_config_desc();
        void action_get_str_desc();
        void action_close_dev();
        void action_enable_midi();
        void claimInterface(const usb_intf_desc_t* intf);
        void prepareEndpoints(const usb_ep_desc_t* endpoint);
};