#include <SDL2/SDL.h>
#include <stdio.h>
#include <cstdint>

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
    inst_t inst;           // currently executing instruction
    bool draw;             // Update the screen yes/no
} chip8_t;