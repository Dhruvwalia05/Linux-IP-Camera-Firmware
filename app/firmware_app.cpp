#include "firmware_app.h"
#include "framework/logger/logger.h"

#include <thread>
#include <chrono>

FirmwareApp::FirmwareApp()
{
}

FirmwareApp::~FirmwareApp()
{
}

bool FirmwareApp::Initialize()
{
    Logger& logger = Logger::GetInstance();

    LoggerError result = logger.Initialize();

    if(result != LoggerError::SUCCESS){
        return false;
    }

    logger.Info("Firmware Application Initialized");

    if(!signalHandler.Initialize()){
        logger.Error("SignalHandler Initialization Failed !!!");
        logger.Shutdown();
        return false;
    }

    return true;
}

void FirmwareApp::Run()
{

    Logger& logger = Logger::GetInstance();

    logger.Info("Firmware Application Running");

    while (!signalHandler.IsShutdownRequested())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void FirmwareApp::Shutdown()
{
    Logger& logger = Logger::GetInstance();

    logger.Info("Firmware application shutting down");

    signalHandler.Shutdown();

    logger.Shutdown();
}
