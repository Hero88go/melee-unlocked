/* The boundary between the game and the machine it runs on.
 *
 * The game is built from the decompiled sources into its own library with its own compiler, so it
 * cannot link against the host directly. Instead the host hands it one table of function pointers
 * (MuHostApi) and takes one back (MuGameApi), both plain C and both versioned. Everything the
 * console's hardware used to do for the game arrives through the first table; everything the host
 * needs to drive the game goes through the second.
 *
 * Shared verbatim by the MSVC host and the GCC game library. No C++, no host types.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_HOST_H
#define MU_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MU_HOST_API_VERSION 2
#define MU_GAME_API_VERSION 1

/* The console's 40.5 MHz timebase, which the host advances deterministically. */
#define MU_TB_HZ 40500000ull

/* Why the game stopped. */
enum { MU_STOP_EXIT = 0, MU_STOP_RESET = 1, MU_STOP_PANIC = 2 };

/* One controller, laid out as the game's PADStatus is. */
typedef struct MuPadStatus {
    uint16_t button;
    int8_t stick_x, stick_y, sub_x, sub_y;
    uint8_t trigger_l, trigger_r, analog_a, analog_b;
    int8_t err;
} MuPadStatus;

typedef void (*MuDiscDone)(int32_t result, void* user);
typedef void (*MuCardDone)(int32_t result, void* user);

/* What a scripted run wants a VS match to be, so the sweep does not have to
 * drive the character and stage screens by cursor position for every
 * combination. The menus still run; only the result is forced. */
typedef struct MuMatchPlayer {
    int32_t kind;        /* CharacterKind, or negative for an empty slot */
    int32_t cpu;         /* 0 human, 1 CPU */
    int32_t cpu_level;   /* 1..9, ignored for a human */
    int32_t costume;
} MuMatchPlayer;

typedef struct MuMatchOverride {
    int32_t stage;       /* the rules' stage kind */
    MuMatchPlayer players[6];
} MuMatchOverride;

typedef struct MuHostApi {
    uint32_t version;   /* MU_HOST_API_VERSION */

    /* ---- diagnostics ---- */
    void (*log)(const char* text);
    void (*panic)(const char* file, int32_t line, const char* message);

    /* ---- time ----
     * ticks() is the console timebase: monotonic, 40.5 MHz, and under --time-base it is a pure
     * function of the frame count so two runs agree. boot_time() is the console epoch value the
     * game's calendar functions work from. */
    uint64_t (*ticks)(void);
    uint64_t (*boot_time)(void);

    /* ---- the point where the host gets to run ----
     * The game is single-threaded and spins waiting for things the hardware used to finish on its
     * own, so every one of those spins calls poll(): the host delivers disc completions, audio,
     * alarms and the retrace here, in the order the console's interrupts would have. */
    void (*poll)(void);

    /* ---- graphics ----
     * The game's own GX library builds the command stream, exactly as it did on the console; these
     * bytes are that stream. The host decodes them into draws (the same decoder the recompiled
     * build uses). */
    void (*gx_fifo)(const uint8_t* data, uint32_t size);
    /* The video interface: which framebuffer to show, and where in the field we are. */
    void (*vi_configure)(uint32_t width, uint32_t height, uint32_t interlaced);
    void (*vi_set_next_framebuffer)(void* xfb);
    void (*vi_flush)(void);
    uint32_t (*vi_retrace_count)(void);
    uint32_t (*vi_next_field)(void);
    void (*vi_set_black)(int32_t black);
    /* Blocks, running poll(), until the retrace count moves. */
    void (*vi_wait_retrace)(void);

    /* ---- input ---- */
    void (*pad_read)(MuPadStatus out[4]);
    void (*pad_rumble)(int32_t port, int32_t on);

    /* ---- disc ----
     * The host owns the image. Paths are resolved through the disc's own filesystem table, so the
     * game's entry numbers are the console's. */
    int32_t (*disc_entrynum)(const char* path);
    int32_t (*disc_file)(int32_t entrynum, uint32_t* start, uint32_t* length);   /* nonzero: found */
    /* Completion is delivered from poll(), after the same virtual delay the recompiled build uses,
     * so the game sees the timing it saw on the console. */
    void (*disc_read)(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user);
    int32_t (*disc_status)(void);
    uint32_t (*disc_id)(void* out, uint32_t size);

    /* ---- memory card ----
     * Slot A as a folder of .gci files, the way the shipped build already stores it. */
    int32_t (*card_probe)(int32_t chan, int32_t* memSize, int32_t* sectorSize);
    int32_t (*card_mount)(int32_t chan);
    int32_t (*card_unmount)(int32_t chan);
    int32_t (*card_open)(int32_t chan, const char* filename, int32_t* file_no, uint32_t* length);
    int32_t (*card_close)(int32_t chan, int32_t file_no);
    int32_t (*card_create)(int32_t chan, const char* filename, uint32_t size, int32_t* file_no);
    int32_t (*card_delete)(int32_t chan, const char* filename);
    int32_t (*card_rename)(int32_t chan, const char* old_name, const char* new_name);
    int32_t (*card_read)(int32_t chan, int32_t file_no, void* dst, uint32_t length, uint32_t offset);
    int32_t (*card_write)(int32_t chan, int32_t file_no, const void* src, uint32_t length, uint32_t offset);
    int32_t (*card_stat)(int32_t chan, int32_t file_no, void* stat, uint32_t stat_size);
    int32_t (*card_set_stat)(int32_t chan, int32_t file_no, const void* stat, uint32_t stat_size);
    int32_t (*card_free_blocks)(int32_t chan, int32_t* byte_not_used, int32_t* files_not_used);
    int32_t (*card_format)(int32_t chan);

    /* ---- audio ----
     * The game's AX library runs natively and builds its command list in memory; the host's mixer
     * reads it when the mail arrives, exactly as the DSP did. */
    void (*ai_init_dma)(void* buffer, uint32_t length);
    void (*ai_start_dma)(int32_t on);
    void (*ai_set_sample_rate)(uint32_t rate48khz);
    void (*ai_set_stream_volume)(int32_t left, int32_t right);
    void (*dsp_mail)(uint32_t mail);
    uint32_t (*dsp_mail_pending)(void);
    /* Audio RAM: the console's separate 16 MB, which only DMA could reach. */
    void* (*aram_base)(void);
    uint32_t (*aram_size)(void);
    void (*aram_dma)(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length);

    /* ---- machine ---- */
    uint32_t (*mem1_size)(void);
    int32_t (*sound_mode)(void);              /* 0 mono, 1 stereo */
    void (*set_sound_mode)(int32_t mode);
    int32_t (*progressive_mode)(void);
    void (*set_progressive_mode)(int32_t mode);
    int32_t (*reset_code)(void);
    int32_t (*reset_switch)(void);
    void (*stop)(int32_t reason, int32_t code);   /* MU_STOP_* */

    /* ---- scripted runs (version 2) ---- */
    /* The match a --match run asked for, or NULL for an ordinary run. */
    const MuMatchOverride* (*match_override)(void);
} MuHostApi;

typedef struct MuGameApi {
    uint32_t version;   /* MU_GAME_API_VERSION */

    /* Runs the game. Returns when it stops. */
    int32_t (*run)(void);
    /* One 60 Hz tick: the host calls this from its own frame loop, inside the game's poll(). */
    void (*retrace)(void);
    /* Alarms the game registered; the host calls this with the current timebase. */
    void (*fire_alarms)(uint64_t now);
    /* The audio buffer the game handed to AI has been played. */
    void (*ai_dma_done)(void);
} MuGameApi;

/* The library's one export. The host fills `host`, the game fills `game`. */
typedef int32_t (*MuGameEntry)(const MuHostApi* host, MuGameApi* game);

#ifdef __cplusplus
}
#endif
#endif
