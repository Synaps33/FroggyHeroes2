/***************************************************************************
 *   fheroes2 libretro core wrapper                                        *
 ***************************************************************************/

#include "libretro.h"
#include "libco.h"
#include "SDL.h"

extern "C" {
#include "sdl/src/events/SDL_events_c.h"
#include "sdl/src/audio/SDL_sysaudio.h"
extern SDL_AudioDevice *current_audio;
}

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

/* Callbacks */
static retro_environment_t environ_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

/* Coroutines */
static cothread_t main_thread = NULL;
static cothread_t game_thread = NULL;

/* Game paths */
extern std::string g_libretro_game_dir;
static std::string s_game_path;

/* Time tracking */
static uint32_t s_current_ticks = 0;

/* Mouse cursor emulation */
static int s_mouse_x = 160;
static int s_mouse_y = 120;
static int s_dpad_held_frames = 0;
static uint32_t s_prev_pad = 0;
static bool s_left_click_held = false;
static bool s_right_click_held = false;

/* Render one audio buffer from SDL's mixer into the libretro batch callback.
 *
 * SDL never starts a thread here (SDL_SYS_CreateThread is stubbed out on this
 * platform), so nothing would ever call spec.callback on its own. We drive it
 * manually instead: exactly one buffer per video frame.
 *
 * The sample count is taken from the spec rather than hardcoded, because
 * SDL_OpenAudio() may hand back a different buffer size than requested. */
static void libretro_audio_render(void)
{
    if (!audio_batch_cb) return;
    if (!current_audio || current_audio->paused || !current_audio->spec.callback) {
        return;
    }

    const int spec_size = current_audio->spec.size;
    if (spec_size <= 0) return;

    /* 22050Hz stereo 16-bit is 4 bytes per frame; 2940 bytes = 735 frames. */
    static int16_t samples[4096 * 2];
    const int max_frames = (int)(sizeof(samples) / sizeof(samples[0]) / 2);
    int frames = spec_size / 4;
    if (frames > max_frames) frames = max_frames;
    if (frames <= 0) return;

    memset(samples, 0, frames * 4);
    current_audio->spec.callback(current_audio->spec.userdata, (Uint8*)samples, frames * 4);
    audio_batch_cb(samples, (size_t)frames);
}

/* Forward declarations */
extern int fheroes2_main(int argc, char **argv);

/* Thread entry point */
static void fheroes2_thread_entry(void)
{
    char arg0[] = "fheroes2";
    char *argv[] = { arg0, NULL };
    fheroes2_main(1, argv);

    /* If game exits, yield forever */
    while (1) {
        co_switch(main_thread);
    }
}

extern uint16_t s_libretro_framebuffer[320 * 240];
static bool s_frame_flipped = false;
static uint32_t s_accum_ms = 0;

/* Helpers for video flip and delay */
extern "C" {

void libretro_video_flip(const void *pixels, int width, int height, int pitch)
{
    if (video_cb && pixels) {
        video_cb(pixels, width, height, pitch);
    }
    s_current_ticks += 33;
    s_frame_flipped = true;
    s_accum_ms = 0;

    /* Audio is emitted from retro_run(), once per frame. Doing it here as
     * well made the mixer's callback run twice per frame whenever the frame
     * contained enough SDL_Delay() calls to push s_accum_ms past 33ms, which
     * consumed the sound data at double rate. */
    if (main_thread) {
        co_switch(main_thread);
    }
}

Uint32 libretro_get_ticks(void)
{
    return s_current_ticks;
}

void libretro_delay(Uint32 ms)
{
    s_current_ticks += ms;
    s_accum_ms += ms;
    while (s_accum_ms >= 33) {
        s_accum_ms -= 33;
        if (!s_frame_flipped && video_cb) {
            video_cb(s_libretro_framebuffer, 320, 240, 320 * sizeof(uint16_t));
        }
        s_frame_flipped = false;
        if (main_thread) {
            co_switch(main_thread);
        }
    }
}

static void send_key_event(SDLKey key, Uint8 state)
{
    SDL_keysym sym;
    sym.scancode = 0;
    sym.sym = key;
    sym.mod = KMOD_NONE;
    sym.unicode = 0;
    SDL_PrivateKeyboard(state, &sym);
}

void libretro_poll_events(void)
{
    if (!input_state_cb) return;

    /* Read standard buttons */
    bool up     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    bool down   = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    bool left   = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    bool right  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    bool btn_a  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    bool btn_b  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    bool btn_x  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    bool btn_y  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
    bool start  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    bool select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
    bool btn_l  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    bool btn_r  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

    /* Mouse movement via D-PAD with smooth ramp-up */
    bool moving = up || down || left || right;
    if (moving) {
        s_dpad_held_frames++;
    } else {
        s_dpad_held_frames = 0;
    }

    int speed = 2;
    if (s_dpad_held_frames > 8)  speed = 4;
    if (s_dpad_held_frames > 20) speed = 7;

    int old_x = s_mouse_x;
    int old_y = s_mouse_y;

    if (up)    s_mouse_y -= speed;
    if (down)  s_mouse_y += speed;
    if (left)  s_mouse_x -= speed;
    if (right) s_mouse_x += speed;

    if (s_mouse_x < 0)   s_mouse_x = 0;
    if (s_mouse_x > 319) s_mouse_x = 319;
    if (s_mouse_y < 0)   s_mouse_y = 0;
    if (s_mouse_y > 239) s_mouse_y = 239;

    if (s_mouse_x != old_x || s_mouse_y != old_y) {
        SDL_PrivateMouseMotion(0, 0, (Sint16)s_mouse_x, (Sint16)s_mouse_y);
    }

    /* Left Click: L button (requested) or A button */
    bool left_click = btn_l || btn_a;
    if (left_click && !s_left_click_held) {
        s_left_click_held = true;
        SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_LEFT, (Sint16)s_mouse_x, (Sint16)s_mouse_y);
    } else if (!left_click && s_left_click_held) {
        s_left_click_held = false;
        SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_LEFT, (Sint16)s_mouse_x, (Sint16)s_mouse_y);
    }

    /* Right Click (Unit inspect / info): R button (requested) or B button */
    bool right_click = btn_r || btn_b;
    if (right_click && !s_right_click_held) {
        s_right_click_held = true;
        SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_RIGHT, (Sint16)s_mouse_x, (Sint16)s_mouse_y);
    } else if (!right_click && s_right_click_held) {
        s_right_click_held = false;
        SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_RIGHT, (Sint16)s_mouse_x, (Sint16)s_mouse_y);
    }

    /* Button edge transitions for keyboard keys */
    uint32_t curr_pad = (up     ? (1 << 0) : 0) |
                        (down   ? (1 << 1) : 0) |
                        (left   ? (1 << 2) : 0) |
                        (right  ? (1 << 3) : 0) |
                        (btn_a  ? (1 << 4) : 0) |
                        (btn_b  ? (1 << 5) : 0) |
                        (btn_x  ? (1 << 6) : 0) |
                        (btn_y  ? (1 << 7) : 0) |
                        (start  ? (1 << 8) : 0) |
                        (select ? (1 << 9) : 0) |
                        (btn_l  ? (1 << 10) : 0) |
                        (btn_r  ? (1 << 11) : 0);

    uint32_t pressed  = curr_pad & ~s_prev_pad;
    uint32_t released = ~curr_pad & s_prev_pad;
    s_prev_pad = curr_pad;

    /* START -> Return */
    if (pressed & (1 << 8))  send_key_event(SDLK_RETURN, SDL_PRESSED);
    if (released & (1 << 8)) send_key_event(SDLK_RETURN, SDL_RELEASED);

    /* SELECT -> Escape */
    if (pressed & (1 << 9))  send_key_event(SDLK_ESCAPE, SDL_PRESSED);
    if (released & (1 << 9)) send_key_event(SDLK_ESCAPE, SDL_RELEASED);

    /* X -> Space */
    if (pressed & (1 << 6))  send_key_event(SDLK_SPACE, SDL_PRESSED);
    if (released & (1 << 6)) send_key_event(SDLK_SPACE, SDL_RELEASED);

    /* Y -> TAB: show/hide minimap.
     *
     * The engine already binds the minimap to KEY_TAB (game_startgame.cpp,
     * "case KEY_TAB: SwitchShowRadar()"), but nothing ever generated that key,
     * so the radar could only be reached by clicking the control panel at the
     * top of the screen. KEY_h was the previous binding here, and it is a no-op
     * on the adventure map - it only does anything in the main menu (high
     * scores), on the new-game screen (hot seat) and in battle (hero window).
     * Those screens are fully mouse-navigable, so the trade is worth it.
     *
     * Note this only works while the sidebar is hidden (pocket pc = 1 in
     * fheroes2.cfg): SwitchShowRadar() early-returns unless HideInterface() is
     * set. On a 320x240 screen enabling the radar hides the hero icon bar, the
     * status bar and the button bar, since there is no room for all of them. */
    if (pressed & (1 << 7))  send_key_event(SDLK_TAB, SDL_PRESSED);
    if (released & (1 << 7)) send_key_event(SDLK_TAB, SDL_RELEASED);
}

} /* extern "C" */

/* Libretro API implementations */
void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_init(void)
{
    s_current_ticks = 0;
    s_mouse_x = 160;
    s_mouse_y = 120;
    s_dpad_held_frames = 0;
    s_prev_pad = 0;
    s_left_click_held = false;
    s_right_click_held = false;
}

void retro_deinit(void)
{
    if (game_thread) {
        co_delete(game_thread);
        game_thread = NULL;
    }
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name     = "fheroes2";
    info->library_version  = "SVN-r1898";
    info->valid_extensions = "agg|mp2|bin|zip";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width   = 320;
    info->geometry.base_height  = 240;
    info->geometry.max_width    = 320;
    info->geometry.max_height   = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 30.0;
    info->timing.sample_rate    = 22050.0;
}

void retro_reset(void)
{
}

void retro_run(void)
{
    if (input_poll_cb) {
        input_poll_cb();
    }

    s_frame_flipped = false;

    if (game_thread) {
        co_switch(game_thread);
    }

    /* Exactly one audio buffer per frame, emitted here rather than from the
     * flip/delay paths. The frontend drives retro_run() once per video frame,
     * so this is the only place with a reliable one-to-one mapping between
     * rendered frames and mixer callbacks - rendering it from the flip and
     * from the delay accumulator both double-emitted whenever a frame's delays
     * totalled more than 33ms. */
    libretro_audio_render();
}

static std::string extract_directory(const std::string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    std::string dir = path.substr(0, pos);
    /* If game file was inside DATA/ or data/, step up to game root dir */
    if (dir.length() >= 5) {
        std::string tail = dir.substr(dir.length() - 5);
        if (tail == "/DATA" || tail == "/data" || tail == "\\DATA" || tail == "\\data") {
            dir = dir.substr(0, dir.length() - 5);
        }
    }
    return dir;
}

bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (environ_cb && !environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        return false;
    }

    if (game && game->path) {
        s_game_path = game->path;
        g_libretro_game_dir = extract_directory(s_game_path);
    } else {
        s_game_path = "";
        g_libretro_game_dir = ".";
    }

    main_thread = co_active();
    /* 512KB stack for game cothread */
    game_thread = co_create(0x80000, fheroes2_thread_entry);
    if (!game_thread) {
        return false;
    }

    /* Run until first frame is ready */
    co_switch(game_thread);

    return true;
}

void retro_unload_game(void)
{
    if (game_thread) {
        co_delete(game_thread);
        game_thread = NULL;
    }
}

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void *data, size_t size)
{
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void *data, size_t size)
{
    (void)data;
    (void)size;
    return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}
