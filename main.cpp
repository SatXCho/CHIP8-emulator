#include <stdio.h>
#include <iostream>

#include "chip8_emulator.h"

int main(int argv, char **args)
{
    if (argv < 2)
    {
        std::cerr << "Usage " << args[0] << " <rom_name>\n";
    }
    // configuration/options
    config_t config = {0};
    if (!setupEmulator(&config, argv, args))
        std::cout << "Window can't be rendered: Configuration un-initialized\n";

    // initialize sdl
    sdl_t sdl = {0};
    if (!initSDl(&sdl, &config))
        std::cout << "SDL not Initialized\n";

    // clear screen to bg color
    clearScreen(sdl, config);

    // chip8 init
    chip8_t chip8 = {};
    const char *rom_name = args[1];
    if (!initChip8(&chip8, rom_name))
        std::cout << "CHIP8 not initialized\n";

    // main emulator loop
    while (chip8.state != QUIT)
    {

        // handle user input
        handleInput(&chip8);
        if (chip8.state == PAUSED)
            continue;

        // emulate CHIP8 instruction
        emulateInstruction(&chip8, config);
        // approx 60Hz/60fps delay 16.67ms
        SDL_Delay(16);

        // updating screen
        updateScreen(sdl, config, &chip8);
    }

    cleanUp(&sdl);
    return 0;
}