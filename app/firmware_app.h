#ifndef FIRMWARE_APP_H
#define FIRMWARE_APP_H

#include "framework/signal/signal_handler.h"

class FirmwareApp
{
    SignalHandler signalHandler;
public:
    FirmwareApp();
    ~FirmwareApp();

    bool Initialize();
    void Run();
    void Shutdown();
};

#endif // FIRMWARE_APP_H
