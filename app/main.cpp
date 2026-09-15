#include <cstdlib>
#include "firmware_app.h"

int main(){
    FirmwareApp app;

    if(!app.Initialize()){
        return EXIT_FAILURE;
    }

    app.Run();

    app.Shutdown();

    return EXIT_SUCCESS;
}
