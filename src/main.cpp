#include "app.hpp"

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand
) {
    HydroApplication application;

    if (!application.initialize(
        instance,
        showCommand
    )) {
        MessageBoxA(
            NULL,
            "A E Temple IDE nem indithato el.",
            "E Temple IDE",
            MB_OK | MB_ICONERROR
        );

        return EXIT_FAILURE;
    }

    return application.run();
}
