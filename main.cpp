#include <SDL2/SDL.h>
#include <stdio.h>
#include <cstring>
#include <iostream>

// SDL Container
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// options object
typedef struct
{
    uint32_t window_width;  // Configurable 32-bit SDL window width
    uint32_t window_height; // Configurable 32-bit SDL window height
    uint32_t fg_color;      // Hex RGBA8888 foreground color & alpha
    uint32_t bg_color;      // Hex RGBA8888 background color & alpha
    uint32_t pixelscale;    // Scale pixel by factor
} config_t;

// emulator states
typedef enum
{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// instruction
typedef struct
{
    uint16_t opcode;
    uint16_t NNN; // 12 bit address/constant
    uint8_t NN;   // 8 bit constant
    uint8_t N;    // 4 bit constant
    uint8_t X;    // 4 bit register identifier
    uint8_t Y;    // 4 bit register identifier
} inst_t;

// CHIP8 Machine object (For multiple displays)
typedef struct
{
    emulator_state_t state;
    uint8_t ram[4096];     // ram
    bool display[64 * 32]; // display
    uint8_t stack[12];     // stack
    uint8_t *stack_ptr;    // stack pointer
    uint8_t V[16];         // v register (data register v0 to vf)
    uint16_t I;            // I register (index register)
    uint16_t PC;           // Program counter
    uint8_t delay_timer;   // -->60Hz when > 0
    uint8_t sound_timer;   // --> 60Hz & plays tone when > 0
    bool keypad[16];       // Hex
    const char *rom_name;
    inst_t inst; // currently executing instruction
} chip8_t;

bool initSDl(sdl_t *sdl, config_t *config)
{
    // initisalize SDL video audio and timers
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        std::cout << "SDL subsystem could not initialize! SDL_Error: \n"
                  << SDL_GetError();
        return false;
    }
    sdl->window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config->window_width * config->pixelscale,
                                   config->window_height * config->pixelscale,
                                   0);
    if (!sdl->window)
    {
        std::cout << "Couldn't create window(SDL) " << SDL_GetError();
        return false;
    }
    sdl->renderer = SDL_CreateRenderer(sdl->window, // SDL_Window pointer
                                       -1,
                                       SDL_RENDERER_ACCELERATED); // 2D Hardware Acceleration flag

    if (!sdl->renderer)
    {
        std::cout << "Couldn't create renderer(SDL) " << SDL_GetError();
        return false;
    }
    return true; // Success
}

bool setupEmulator(config_t *config, int argv, char **args)
{
    // default width & height values for CHIP 8, also used as default emulator config
    *config = (config_t){
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFFFFFFFF, // white
        .bg_color = 0x00000000, // black
        .pixelscale = 20,
    };

    // Override defaults from passed in arguments
    for (int i = 1; i < argv; i++)
    {
        (void)args[i]; // Prevent compiler error from unused variables argc/argv
        // e.g. set scale factor
        if (strncmp(args[i], "--scale-factor", strlen("--scale-factor")) == 0)
        {
            // Note: should probably add checks for numeric
            i++;
            config->pixelscale = (uint32_t)strtol(args[i], NULL, 10);
        }
    }

    return true;
}

// initialize CHIP8 machine
bool initChip8(chip8_t *chip8, const char rom_name[])
{
    const uint32_t entry_point = 0x200; // CHIP8 roms loaded into 0x200 in the memory
    // load font
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80, // F
    };

    memset(chip8, 0, sizeof(chip8_t));
    std::memcpy(&chip8->ram[0], font, sizeof(font));

    // load ROM
    FILE *rom = fopen(rom_name, "rb"); // open ROM
    if (!rom)
        std::cout << "ROM" << rom_name << "invalid or does not exist!\n"; // error
    fseek(rom, 0, SEEK_END);                                              // get ROM size through fseek from 0 to end of seek
    const size_t rom_size = ftell(rom);                                   // assign ROM size
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom); // reset ROM pointer after fseek call
    if (rom_size > max_size)
        std::cout << "ROM " << rom_name << "too big(" << rom_size << ") to be loaded, max size: " << max_size << "\n";

    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1)
    {
        std::cout << "Could not read Rom file" << rom_name << "into CHIP8 memory\n";
        return false;
    }
    fclose(rom);

    chip8->state = RUNNING;  // Default machine state
    chip8->PC = entry_point; // program counter
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    return true; // Success
}

// update screen for each frame
void updateScreen(const sdl_t sdl, const config_t config, const chip8_t *chip8)
{
    SDL_Rect pixel = {.x = 0, .y = 0, .w = config.pixelscale, .h = config.pixelscale};

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

    // Draw one SDL rectangle per pixel
    for (uint32_t i = 0; i < sizeof chip8->display; i++)
    {
        pixel.x = (i % config.window_width) * config.pixelscale;
        pixel.y = (i / config.window_width) * config.pixelscale;

        if (chip8->display[i])
        {
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &pixel);
        }
        else
        {
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &pixel);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}
void handleInput(chip8_t *chip8)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        // Exit, close window, end program
        case QUIT:
            chip8->state = QUIT; // Used for exiting main emulator loop
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                // escape key quits
                chip8->state = QUIT;
                return;
            case SDLK_SPACE:
                // Spacebar pauses the emualation
                if (chip8->state == RUNNING)
                {
                    chip8->state = PAUSED;
                    std::cout << "Emulation paused \n";
                }
                else
                {
                    chip8->state = RUNNING;
                    std::cout << "Emulation resumed \n";
                }
                return;
            default:
                break;
            }
            break;
        case SDL_KEYUP:
            break;
        default:
            break;
        }
    }
}


// clear screen, independent from CHIP8 clear screen instruction
void clearScreen(sdl_t sdl, const config_t config)
{
    SDL_SetRenderDrawColor(sdl.renderer,
                           ((config.bg_color >> 24) & 0xFF),
                           ((config.bg_color >> 16) & 0xFF),
                           ((config.bg_color >> 8) & 0xFF),
                           ((config.bg_color >> 0) & 0xFF));
    SDL_RenderClear(sdl.renderer);
}

// Freeing resources and closing SDL
void cleanUp(sdl_t *sdl)
{
    SDL_DestroyRenderer(sdl->renderer); // destroying renderer
    SDL_DestroyWindow(sdl->window);     // destroying window
    SDL_Quit();                         // Quit SDL subsystems
}

//print debug output
#ifdef DEBUG
void print_debug_info(chip8_t *chip8)
{
    printf("Address: 0x%04X, Opcode: 0x%04X Desc: ",
           chip8->PC - 2, chip8->inst.opcode);

    switch ((chip8->inst.opcode >> 12) & 0x0F)
    {
    case 0x00:
        if (chip8->inst.NN == 0xE0)
        {
            // 0x00E0: Clear the screen
            printf("Clear screen\n");
        }
        else if (chip8->inst.NN == 0xEE)
        {
            // 0x00EE: Return from subroutine
            // Set program counter to last address on subroutine stack ("pop" it off the stack)
            //   so that next opcode will be gotten from that address.
            printf("Return from subroutine to address 0x%04X\n",
                   *(chip8->stack_ptr - 1));
        }
        else
        {
            printf("Unimplemented Opcode.\n");
        }
        break;

    case 0x01:
        // 0x1NNN: Jump to address NNN
        printf("Jump to address NNN (0x%04X)\n",
               chip8->inst.NNN);
        break;

    case 0x02:
        // 0x2NNN: Call subroutine at NNN
        // Store current address to return to on subroutine stack ("push" it on the stack)
        //   and set program counter to subroutine address so that the next opcode
        //   is gotten from there.
        printf("Call subroutine at NNN (0x%04X)\n",
               chip8->inst.NNN);
        break;

    case 0x03:
        // 0x3XNN: Check if VX == NN, if so, skip the next instruction
        printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
               chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
        break;

    case 0x04:
        // 0x4XNN: Check if VX != NN, if so, skip the next instruction
        printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
               chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
        break;

    case 0x05:
        // 0x5XY0: Check if VX == VY, if so, skip the next instruction
        printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
               chip8->inst.X, chip8->V[chip8->inst.X],
               chip8->inst.Y, chip8->V[chip8->inst.Y]);
        break;

    case 0x06:
        // 0x6XNN: Set register VX to NN
        printf("Set register V%X = NN (0x%02X)\n",
               chip8->inst.X, chip8->inst.NN);
        break;

    case 0x07:
        // 0x7XNN: Set register VX += NN
        printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n",
               chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN,
               chip8->V[chip8->inst.X] + chip8->inst.NN);
        break;

    case 0x08:
        switch (chip8->inst.N)
        {
        case 0:
            // 0x8XY0: Set register VX = VY
            printf("Set register V%X = V%X (0x%02X)\n",
                   chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 1:
            // 0x8XY1: Set register VX |= VY
            printf("Set register V%X (0x%02X) |= V%X (0x%02X); Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
            break;

        case 2:
            // 0x8XY2: Set register VX &= VY
            printf("Set register V%X (0x%02X) &= V%X (0x%02X); Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
            break;

        case 3:
            // 0x8XY3: Set register VX ^= VY
            printf("Set register V%X (0x%02X) ^= V%X (0x%02X); Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
            break;

        case 4:
            // 0x8XY4: Set register VX += VY, set VF to 1 if carry
            printf("Set register V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry; Result: 0x%02X, VF = %X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                   ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
            break;

        case 5:
            // 0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive/0)
            printf("Set register V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
                   (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
            break;

        case 6:
            // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
            printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->V[chip8->inst.X] & 1,
                   chip8->V[chip8->inst.X] >> 1);
            break;

        case 7:
            // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
            printf("Set register V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                   chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y],
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                   (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
            break;

        case 0xE:
            // 0x8XYE: Set register VX <<= 1, store shifted off bit in VF
            printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X],
                   (chip8->V[chip8->inst.X] & 0x80) >> 7,
                   chip8->V[chip8->inst.X] << 1);
            break;

        default:
            // Wrong/unimplemented opcode
            break;
        }
        break;

    case 0x09:
        // 0x9XY0: Check if VX != VY; Skip next instruction if so
        printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
               chip8->inst.X, chip8->V[chip8->inst.X],
               chip8->inst.Y, chip8->V[chip8->inst.Y]);
        break;

    case 0x0A:
        // 0xANNN: Set index register I to NNN
        printf("Set I to NNN (0x%04X)\n",
               chip8->inst.NNN);
        break;

    case 0x0B:
        // 0xBNNN: Jump to V0 + NNN
        printf("Set PC to V0 (0x%02X) + NNN (0x%04X); Result PC = 0x%04X\n",
               chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);
        break;

    case 0x0C:
        // 0xCXNN: Sets register VX = rand() % 256 & NN (bitwise AND)
        printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",
               chip8->inst.X, chip8->inst.NN);
        break;

    case 0x0D:
        // 0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
        //   Screen pixels are XOR'd with sprite bits,
        //   VF (Carry flag) is set if any screen pixels are set off; This is useful
        //   for collision detection or other reasons.
        printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) "
               "from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
               chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
               chip8->V[chip8->inst.Y], chip8->I);
        break;

    case 0x0E:
        if (chip8->inst.NN == 0x9E)
        {
            // 0xEX9E: Skip next instruction if key in VX is pressed
            printf("Skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
        }
        else if (chip8->inst.NN == 0xA1)
        {
            // 0xEX9E: Skip next instruction if key in VX is not pressed
            printf("Skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
        }
        break;

    case 0x0F:
        switch (chip8->inst.NN)
        {
        case 0x0A:
            // 0xFX0A: VX = get_key(); Await until a keypress, and store in VX
            printf("Await until a key is pressed; Store key in V%X\n",
                   chip8->inst.X);
            break;

        case 0x1E:
            // 0xFX1E: I += VX; Add VX to register I. For non-Amiga CHIP8, does not affect VF
            printf("I (0x%04X) += V%X (0x%02X); Result (I): 0x%04X\n",
                   chip8->I, chip8->inst.X, chip8->V[chip8->inst.X],
                   chip8->I + chip8->V[chip8->inst.X]);
            break;

        case 0x07:
            // 0xFX07: VX = delay timer
            printf("Set V%X = delay timer value (0x%02X)\n",
                   chip8->inst.X, chip8->delay_timer);
            break;

        case 0x15:
            // 0xFX15: delay timer = VX
            printf("Set delay timer value = V%X (0x%02X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X]);
            break;

        case 0x18:
            // 0xFX18: sound timer = VX
            printf("Set sound timer value = V%X (0x%02X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X]);
            break;

        case 0x29:
            // 0xFX29: Set register I to sprite location in memory for character in VX (0x0-0xF)
            printf("Set I to sprite location in memory for character in V%X (0x%02X). Result(VX*5) = (0x%02X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] * 5);
            break;

        case 0x33:
            // 0xFX33: Store BCD representation of VX at memory offset from I;
            //   I = hundred's place, I+1 = ten's place, I+2 = one's place
            printf("Store BCD representation of V%X (0x%02X) at memory from I (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
            break;

        case 0x55:
            // 0xFX55: Register dump V0-VX inclusive to memory offset from I;
            //   SCHIP does not inrement I, CHIP8 does increment I
            printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
            break;

        case 0x65:
            // 0xFX65: Register load V0-VX inclusive from memory offset from I;
            //   SCHIP does not inrement I, CHIP8 does increment I
            printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
            break;

        default:
            break;
        }
        break;

    default:
        printf("Unimplemented Opcode.\n");
        break; // Unimplemented or invalid opcode
    }
}
#endif

// Emulate a single instruction
void emulateInstruction(chip8_t *chip8, const config_t config)
{
    // get next opcode from RAM
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    // pre-increment Program counter
    chip8->PC += 2;

    // Fill current instruction opcode
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

    #ifdef DEBUG
        print_debug_info(chip8);
    #endif

    // emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F)
    {
    case 0x00:
        if (chip8->inst.NN == 0xE0)
        {
            // 0x00E0: Clear the screen
            memset(&chip8->display[0], false, sizeof chip8->display);
            std::cout << "cleared screen\n";
        }
        else if (chip8->inst.NN == 0xEE)
        {
            // 0x00EE: Return from subroutine
            chip8->PC = *--chip8->stack_ptr;
            std::cout << "return subr\n";
        }
        else
        {
            // Unimplemented/invalid opcode, may be 0xNNN for calling machine code routine for RCA1802
        }
        break;
    
    case 0x01:
        // 0x1NNN: Jump to address NNN
        chip8->PC = chip8->inst.NNN; // Set program counter so that next opcode is from NNN
        break;
    
    case 0x02:
        // 0x2NNN: Call subroutine at NNN
        *chip8->stack_ptr++ = chip8->PC;
        chip8->PC = chip8->inst.NNN;
        break;

    case 0x06:
        // 0x6XNN: Set register VX to NN
        chip8->V[chip8->inst.X] = chip8->inst.NN;
        break;

    case 0x07:
        // 0x7XNN: Set register VX += NN
        chip8->V[chip8->inst.X] += chip8->inst.NN;
        break;

    case 0x0A:
        // 0xANNN: Set index register I to NNN
        chip8->I = chip8->inst.NNN;
        break;

    case 0x0D:
    {
        // 0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
        //   Screen pixels are XOR'd with sprite bits,
        //   VF (Carry flag) is set if any screen pixels are set off; This is useful
        //   for collision detection or other reasons.
        uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
        uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
        const uint8_t orig_X = X_coord; // Original X value

        chip8->V[0xF] = 0; // Initialize carry flag to 0

        // Loop over all N rows of the sprite
        for (uint8_t i = 0; i < chip8->inst.N; i++)
        {
            // Get next byte/row of sprite data
            const uint8_t sprite_data = chip8->ram[chip8->I + i];
            X_coord = orig_X; // Reset X for next row to draw

            for (int8_t j = 7; j >= 0; j--)
            {
                // If sprite pixel/bit is on and display pixel is on, set carry flag
                bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                const bool sprite_bit = (sprite_data & (1 << j));

                if (sprite_bit && *pixel)
                {
                    chip8->V[0xF] = 1;
                }

                // XOR display pixel with sprite pixel/bit to set it on or off
                *pixel ^= sprite_bit;

                // Stop drawing this row if hit right edge of screen
                if (++X_coord >= config.window_width)
                    break;
            }

            // Stop drawing entire sprite if hit bottom edge of screen
            if (++Y_coord >= config.window_height)
                break;
        }
        // chip8->draw = true; // Will update screen on next 60hz tick
        break;
    }

    default:
        break;
    }
}

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