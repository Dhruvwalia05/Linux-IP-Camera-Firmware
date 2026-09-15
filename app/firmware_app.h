#ifndef FIRMWARE_APP_H
#define FIRMWARE_APP_H

class FirmwareApp
{
public:
    FirmwareApp();
    ~FirmwareApp();

    bool Initialize();
    void Run();
    void Shutdown();
};

#endif // FIRMWARE_APP_H
