#include "MidiClient.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

const char* MidiClient::TAG = "HelloDJ";
// usb_host_client_handle_t MidiClient::Client_Handle = {0};
// usb_device_handle_t MidiClient::Device_Handle = {0};

MidiClient::MidiClient() {
    _isMidiInterfaceClaimed = false;
    _isMidiEndpointsPrepared = false;
    MIDIIn[MIDI_IN_BUFFERS] = {NULL};
}

MidiClient::~MidiClient() {
    // Destructor
}

void MidiClient::begin() {
    BaseType_t task_created;
    task_created = xTaskCreatePinnedToCore(daemonTask,
        "midi-client",
        4096,
        this,
        DAEMON_TASK_PRIORITY,
        &_class_driver_task_hdl,
        0);
    assert(task_created == pdTRUE);
}

void MidiClient::daemonTask(void *arg)
{
    MidiClient* pThis = (MidiClient*)arg;
    class_driver_t driver_obj = {0};

    ESP_LOGE(TAG, "Registering Client"); // Was LOGI
    usb_host_client_config_t client_config = {
        .is_synchronous = false,    //Synchronous clients currently not supported. Set this to false
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));
    pThis->driver_obj = &driver_obj;

    // FDL: This does not look thread safe to me.
    while (1) {
        if (driver_obj.actions == 0) {
            usb_host_client_handle_events(driver_obj.client_hdl, portMAX_DELAY);
        } else {
            if (driver_obj.actions & ACTION_OPEN_DEV) {
                pThis->action_open_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_INFO) {
                pThis->action_get_info(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_DESC) {
                pThis->action_get_dev_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_CONFIG_DESC) {
                pThis->action_get_config_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_ENABLE_MIDI) {
                pThis->action_enable_midi(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_STR_DESC) {
                pThis->action_get_str_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_CLOSE_DEV) {
                pThis->action_close_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_EXIT) {
                break;
            }
            if (driver_obj.actions & ACTION_RECONNECT) {
                driver_obj.actions = 0;
            }
        }
    }

    ESP_LOGE(TAG, "Deregistering Client"); // Was LOGI
    ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));
    vTaskSuspend(NULL);
}

void MidiClient::client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        // A new device has been enumerated and added to the USB Host Library
        if (driver_obj->dev_addr == 0) {
            driver_obj->dev_addr = event_msg->new_dev.address;
            //Open the device next
            driver_obj->actions |= ACTION_OPEN_DEV;
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (driver_obj->dev_hdl != NULL) {
            //Cancel any other actions and close the device next
            driver_obj->actions = ACTION_CLOSE_DEV;
        }
        break;
    default:
        //Should never occur
        abort();
    }
}

void MidiClient::midi_transfer_cb(usb_transfer_t *transfer)
{
    class_driver_t* driver_obj = (class_driver_t*)transfer->context;
  ESP_LOGE("", "midi_transfer_cb."); // Was ESP_LOGI
  if (driver_obj->dev_hdl == transfer->device_handle) {
    int in_xfer = transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK;
    if ((transfer->status == 0) && in_xfer) {
      uint8_t *const p = transfer->data_buffer;
      for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
        if ((p[i] + p[i+1] + p[i+2] + p[i+3]) == 0) break;
        ESP_LOGE("", "midi: %02x %02x %02x %02x", // Was ESP_LOGI
            p[i], p[i+1], p[i+2], p[i+3]);
      }
      esp_err_t err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK) {
        ESP_LOGE("", "usb_host_transfer_submit In fail: %x", err); // Was ESP_LOGI
      }
    }
    else {
      ESP_LOGE("", "transfer->status %d", transfer->status); // Was ESP_LOGI
    }
  }
}

void MidiClient::action_open_dev(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_addr != 0);
    ESP_LOGE(TAG, "Opening device at address %d", driver_obj->dev_addr); // Was LOGI
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    //Get the device's information next
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

void MidiClient::action_get_info(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGE(TAG, "Getting device information"); // Was LOGI
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    ESP_LOGE(TAG, "\t%s speed", (char *[]) { // Was LOGI
        "Low", "Full", "High"
    }[dev_info.speed]);
    ESP_LOGE(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue); // Was LOGI

    //Get the device descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

void MidiClient::action_get_dev_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGE(TAG, "Getting device descriptor"); // Was LOGI
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    //Get the device's config descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

void MidiClient::action_enable_midi(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGE(TAG, "Detecting MIDI"); // Was LOGI
    const usb_config_desc_t* config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));

    const uint8_t *p = &config_desc->val[0];
    uint8_t bLength;
    for (int i = 0; i < config_desc->wTotalLength; i+=bLength, p+=bLength) {
        bLength = *p;
        if ((i + bLength) <= config_desc->wTotalLength) {
            const uint8_t bDescriptorType = *(p + 1);
            switch (bDescriptorType) {
                case USB_B_DESCRIPTOR_TYPE_DEVICE:
                    ESP_LOGE("", "USB Device Descriptor should not appear in config"); // Was LOGI
                    break;
                case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
                    // show_config_desc(p);
                    break;
                case USB_B_DESCRIPTOR_TYPE_STRING:
                    ESP_LOGE("", "USB string desc TBD"); // Was LOGI
                    break;
                case USB_B_DESCRIPTOR_TYPE_INTERFACE:
                    // show_interface_desc(p);
                    claimInterface(driver_obj, (usb_intf_desc_t *)p);
                    break;
                case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
                    // show_endpoint_desc(p);
                    prepareEndpoints(driver_obj, (usb_ep_desc_t *)p);
                    break;
                case USB_B_DESCRIPTOR_TYPE_DEVICE_QUALIFIER:
                    // Should not be in config?
                    ESP_LOGE("", "USB device qual desc TBD"); // Was LOGI
                    break;
                case USB_B_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION:
                    // Should not be in config?
                    ESP_LOGE("", "USB Other Speed TBD"); // Was LOGI
                    break;
                case USB_B_DESCRIPTOR_TYPE_INTERFACE_POWER:
                    // Should not be in config?
                    ESP_LOGE("", "USB Interface Power TBD"); // Was LOGI
                    break;
                default:
                    ESP_LOGE("", "Unknown USB Descriptor Type: 0x%x", *p); // Was LOGI
                    break;
            }
        } else {
            ESP_LOGE("", "USB Descriptor invalid"); // Was ESP_LOGI
            break;
        }
    }

    //Get the device's string descriptors next
    driver_obj->actions &= ~ACTION_ENABLE_MIDI;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

void MidiClient::claimInterface(class_driver_t* driver_obj, const usb_intf_desc_t *intf)
{
    if (_isMidiInterfaceClaimed) return;

    if ((intf->bInterfaceClass == USB_CLASS_AUDIO) &&
        (intf->bInterfaceSubClass == 3) &&
        (intf->bInterfaceProtocol == 0))
    {
        _isMidiInterfaceClaimed = true;
        ESP_LOGE("", "Claiming a MIDI device!"); // Was ESP_LOGI
        esp_err_t err = usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl,
            intf->bInterfaceNumber, intf->bAlternateSetting);
        if (err != ESP_OK) ESP_LOGE("", "usb_host_interface_claim failed: %x", err); // Was ESP_LOGI
    }
}

void MidiClient::prepareEndpoints(class_driver_t* driver_obj, const usb_ep_desc_t* endpoint) {
    if (!_isMidiInterfaceClaimed) {
        ESP_LOGE("", "Unexpected: Ready to prepare endpoints, but MIDI interface not yet claimed.");
        return;
    }
    if (_isMidiEndpointsPrepared) {
        ESP_LOGE("", "Unexpected: Ready to prepare endpoints, but enpoints already prepared.");
        return;
    }

    esp_err_t err;

    // must be bulk for MIDI
    if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
        ESP_LOGE("", "Not bulk endpoint: 0x%02x", endpoint->bmAttributes); // Was ESP_LOGI
        return;
    }
    if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
        for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
            err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIIn[i]);
            if (err != ESP_OK) {
                MIDIIn[i] = NULL;
                ESP_LOGE("", "usb_host_transfer_alloc In fail: %x", err); // Was ESP_LOGI
            } else {
                MIDIIn[i]->device_handle = driver_obj->dev_hdl;
                MIDIIn[i]->bEndpointAddress = endpoint->bEndpointAddress;
                MIDIIn[i]->callback = midi_transfer_cb;
                MIDIIn[i]->context = driver_obj;
                MIDIIn[i]->num_bytes = endpoint->wMaxPacketSize;
                esp_err_t err = usb_host_transfer_submit(MIDIIn[i]);
                if (err != ESP_OK) {
                    ESP_LOGE("", "usb_host_transfer_submit In fail: %x", err); // Was ESP_LOGI
                }
            }
        }
    } else {
        err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIOut);
        if (err != ESP_OK) {
            MIDIOut = NULL;
            ESP_LOGE("", "usb_host_transfer_alloc Out fail: %x", err); // Was ESP_LOGI
            return;
        }
        ESP_LOGE("", "Out data_buffer_size: %d", MIDIOut->data_buffer_size); // Was ESP_LOGI
        MIDIOut->device_handle = driver_obj->dev_hdl;
        MIDIOut->bEndpointAddress = endpoint->bEndpointAddress;
        MIDIOut->callback = midi_transfer_cb;
        MIDIOut->context = driver_obj;
        //    MIDIOut->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
    }
    _isMidiEndpointsPrepared = ((MIDIOut != NULL) && (MIDIIn[0] != NULL));
}

void MidiClient::action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGE(TAG, "Getting config descriptor"); // Was LOGI
    const usb_config_desc_t* config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);

    //Get the device's string descriptors next
    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_ENABLE_MIDI;
}

void MidiClient::action_get_str_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGE(TAG, "Getting Manufacturer string descriptor"); // Was LOGI
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGE(TAG, "Getting Product string descriptor"); // Was LOGI
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGE(TAG, "Getting Serial Number string descriptor"); // Was LOGI
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    //Nothing to do until the device disconnects
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
}

void MidiClient::action_close_dev(class_driver_t *driver_obj)
{
    ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;
    //We need to connect a new device
    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions |= ACTION_RECONNECT;
}

void MidiClient::registerOnNoteCallback(NoteCallback cb) {
    _onNoteCallback = cb;
}