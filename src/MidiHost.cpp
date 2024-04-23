#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "driver/gpio.h"

#include "MidiHost.h"

const char* MidiHost::TAG = "HelloDJ";

MidiHost::MidiHost() {
    // Constructor
}

MidiHost::~MidiHost() {
    // Destructor
}

MidiHost& MidiHost::begin() {
    ESP_LOGI(TAG, "begin1()");
    ESP_LOGW(TAG, "begin2()");
    ESP_LOGE(TAG, "begin3()");
    BaseType_t task_created;
    task_created = xTaskCreatePinnedToCore(daemonTask,
        "usb_host",
        4096,
        xTaskGetCurrentTaskHandle(),
        HOST_LIB_TASK_PRIORITY,
        &_host_lib_task_hdl,
        0);
    assert(task_created == pdTRUE);
    return *this;
}

MidiHost& MidiHost::waitUntilReady() {
    ulTaskNotifyTake(false, 10000);
    return *this;
}

// currently we only support one client
MidiHost& MidiHost::addClient(MidiClient* pClient) {
    _pClient = pClient;
    return *this;
}

// private

void MidiHost::daemonTask(void *arg)
{
    ESP_LOGE(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
# ifdef ENABLE_ENUM_FILTER_CALLBACK
        .enum_filter_cb = set_config_cb,
# endif // ENABLE_ENUM_FILTER_CALLBACK
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    //Signalize the app_main, the USB host library has been installed
    // FDL: Need to handle the case where this is signalled before the wait.
    xTaskNotifyGive(arg);

    bool has_clients = true;
    bool has_devices = true;
    while (has_clients || has_devices) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGE(TAG, "No more clients");
            has_clients = false;
            if (ESP_OK == usb_host_device_free_all()) {
                ESP_LOGE(TAG, "All devices marked as free");
            } else {
                ESP_LOGE(TAG, "Wait for the ALL FREE EVENT");
            }
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGE(TAG, "No more devices");
            has_devices = false;
        }

    }
    ESP_LOGE(TAG, "No more clients and devices");

    //Uninstall the USB Host Library
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskSuspend(NULL);
}