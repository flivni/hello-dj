#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

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
            ACTION_GET_STR_DESC = 0x10,
            ACTION_CLOSE_DEV = 0x20,
            ACTION_EXIT = 0x40,
            ACTION_RECONNECT = 0x80,
        } action_t;

        typedef struct {
            usb_host_client_handle_t client_hdl;
            uint8_t dev_addr;
            usb_device_handle_t dev_hdl;
            uint32_t actions;
        } class_driver_t;

        class_driver_t* driver_obj;

        NoteCallback _onNoteCallback;

        TaskHandle_t _class_driver_task_hdl;

        static void daemonTask(void* arg);
        static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
        static void action_open_dev(class_driver_t *driver_obj);
        static void action_get_info(class_driver_t *driver_obj);
        static void action_get_dev_desc(class_driver_t *driver_obj);
        static void action_get_config_desc(class_driver_t *driver_obj);
        static void action_get_str_desc(class_driver_t *driver_obj);
        static void action_close_dev(class_driver_t *driver_obj);
};