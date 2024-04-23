#pragma once

#include "MidiClient.h"

class MidiHost {
    public:     
        static const char* TAG;

        const UBaseType_t HOST_LIB_TASK_PRIORITY = 2;

        MidiHost();
        virtual ~MidiHost();
        MidiHost& begin();
        MidiHost& waitUntilReady();
        MidiHost& addClient(MidiClient* client);

        private:
            MidiClient* _pClient;
            TaskHandle_t _host_lib_task_hdl;

            static void daemonTask(void* arg);
};