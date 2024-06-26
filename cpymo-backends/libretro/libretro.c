#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <alloca.h>
#include <stdio.h>
#include <libgen.h>
#include "libretro.h"

#include "../../cpymo/cpymo_engine.h"
#include "../software/cpymo_backend_software.h"
#include "../include/cpymo_backend_audio.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../stb/stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../stb/stb_image_resize.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../stb/stb_image_write.h"
#define STB_DS_IMPLEMENTATION
#include "../../stb/stb_ds.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb/stb_truetype.h"


static void                            fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t              log_cb     = fallback_log;
static retro_video_refresh_t           video_cb;
static retro_input_poll_t              input_poll_cb;
static retro_input_state_t             input_state_cb;
static retro_audio_sample_batch_t      audio_batch_cb;
static retro_environment_t             environ_cb;
static cpymo_engine                    engine;
static cpymo_backend_software_image    soft_image;
static cpymo_backend_software_context  soft_context;
static cpymo_input                     input      = { 0 };
static float                           delta      = 0;
static stbtt_fontinfo                  font;

cpymo_input cpymo_input_snapshot(void)
{
    return input;
}

void cpymo_backend_audio_free()
{
}

void cpymo_backend_audio_lock()
{
}

void cpymo_backend_audio_unlock()
{
}

const cpymo_backend_audio_info *cpymo_backend_audio_get_info(void)
{
    static const cpymo_backend_audio_info audio_info = {
        44100, cpymo_backend_audio_s16, 2
    };
    return &audio_info;
}

FILE *cpymo_backend_read_save(const char *gamedir, const char * name)
{
    char *path = (char *)alloca(strlen(gamedir) + strlen(name) + 8);
    sprintf(path, "%s/save/%s", gamedir, name);
    return fopen(path, "rb");
}

FILE *cpymo_backend_write_save(const char *gamedir, const char * name)
{
    char *path = (char *)alloca(strlen(gamedir) + strlen(name) + 8);
    sprintf(path, "%s/save/%s", gamedir, name);
    return fopen(path, "wb");
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t cb)
{
    struct retro_log_callback log;
    environ_cb = cb;
    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
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

void retro_get_system_info(struct retro_system_info *info)
{
    info->need_fullpath = true;
    info->valid_extensions = "txt";
    info->library_version = "1.1.9";
    info->library_name = "CPyMO";
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = soft_image.w;
    info->geometry.base_height = soft_image.h;
    info->geometry.max_width = soft_image.w;
    info->geometry.max_height = soft_image.h;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

static void frame_cb(retro_usec_t usec)
{
    delta = (float)usec / 1000000.0;
}

void retro_init(void)
{
    enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
    struct retro_frame_time_callback frametime = {
        .callback = frame_cb,
        .reference = 1000000 / 60.0,
    };
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
    environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frametime);
}


static error_t cpymo_backend_font_try_load_font(const char *path)
{
    unsigned char *data = NULL;
    size_t len = 0;
    error_t err = cpymo_utils_loadfile(path, (char **)&data, &len);
    if (err != CPYMO_ERR_SUCC)
        return err;
    if (stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
        free(data);
        return CPYMO_ERR_BAD_FILE_FORMAT;
    }
    return CPYMO_ERR_SUCC;
}

static error_t cpymo_backend_font_init(const char *gamedir)
{
    error_t err;
    char *fontpath = (char *)alloca(strlen(gamedir) + 24);

    sprintf(fontpath, "%s/system/default.ttf", gamedir);
    err = cpymo_backend_font_try_load_font(fontpath);
    if (err == CPYMO_ERR_SUCC)
        return CPYMO_ERR_SUCC;
    sprintf(fontpath, "%s/system/default.otf", gamedir);
    err = cpymo_backend_font_try_load_font(fontpath);
    if (err == CPYMO_ERR_SUCC)
        return CPYMO_ERR_SUCC;

    const char *systemdir;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemdir)) {
        fontpath = (char *)alloca(strlen(systemdir) + 24);
        sprintf(fontpath, "%s/cpymo/default.ttf", systemdir);
        err = cpymo_backend_font_try_load_font(fontpath);
        if (err == CPYMO_ERR_SUCC)
            return CPYMO_ERR_SUCC;
        sprintf(fontpath, "%s/cpymo/default.otf", systemdir);
        err = cpymo_backend_font_try_load_font(fontpath);
        if (err == CPYMO_ERR_SUCC)
            return CPYMO_ERR_SUCC;
    }
    return err;
}

bool retro_load_game(const struct retro_game_info *game)
{
    error_t err;
    if (game == NULL || game->path == NULL)
        return false;

    char *gamedir = dirname(strdup(game->path));
    err = cpymo_engine_init(&engine, gamedir);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_engine_init: %s\n", cpymo_error_message(err));
        return false;
    }
    err = cpymo_backend_font_init(gamedir);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_backend_font_init: %s\n", cpymo_error_message(err));
        return false;
    }

    soft_image.w = engine.gameconfig.imagesize_w;
    soft_image.h = engine.gameconfig.imagesize_h;
    soft_image.line_stride = soft_image.w * 4;
    soft_image.pixel_stride = 4;
    soft_image.r_offset = 2;
    soft_image.g_offset = 1;
    soft_image.b_offset = 0;
    soft_image.a_offset = 3;
    soft_image.has_alpha_channel = false;
    soft_image.pixels = (uint8_t *)malloc(soft_image.line_stride * soft_image.h);

    soft_context.font = &font;
    soft_context.render_target = &soft_image;
    soft_context.logical_screen_w = soft_image.w;
    soft_context.logical_screen_h = soft_image.h;
    soft_context.scale_on_load_image = false;
    cpymo_backend_software_set_context(&soft_context);

    return true;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_deinit(void)
{
}

void retro_reset(void)
{
}

static void input_update(void)
{
#define JOY(x) input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_##x)
    input.up          = JOY(UP);
    input.down        = JOY(DOWN);
    input.left        = JOY(LEFT);
    input.right       = JOY(RIGHT);
    input.ok          = JOY(A) || JOY(X);
    input.cancel      = JOY(B) || JOY(Y);
    input.skip        = JOY(L) || JOY(START);
    input.hide_window = JOY(R) || JOY(SELECT);
#undef JOY

#define POINTER(x) input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_##x)
    input.mouse_position_useable = true;
    input.mouse_x                = 1.0 * soft_image.w * (POINTER(X) + 0x7fff) / 65534;
    input.mouse_y                = 1.0 * soft_image.h * (POINTER(Y) + 0x7fff) / 65534;
    input.mouse_button           = POINTER(PRESSED);
    input.mouse_wheel_delta      = 0;
#undef POINTER
}

void retro_run(void)
{
    static const int samples = 44100 / 60;
    static int16_t audio_buffer[44100 / 60 * 2];
    bool redraw = false;

    input_poll_cb();
    input_update();

    cpymo_engine_update(&engine, delta, &redraw);
    if (redraw)
        cpymo_engine_draw(&engine);
    video_cb(soft_image.pixels, soft_image.w, soft_image.h, soft_image.line_stride);

    cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
    audio_batch_cb(audio_buffer, samples);
}

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void *data, size_t size)
{
    return false;
}

bool retro_unserialize(const void *data, size_t size)
{
    return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    return false;
}

void retro_unload_game(void)
{
    if (font.data) {
        free(font.data);
        font.data = NULL;
    }
    if (soft_image.pixels) {
        free(soft_image.pixels);
        soft_image.pixels = NULL;
    }
    cpymo_engine_free(&engine);
    cpymo_backend_software_set_context(NULL);
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    return 0;
}

size_t retro_get_memory_size(unsigned id)
{
    return 0;
}
