#pragma once
#include <stddef.h>

extern unsigned char *pixels_rgb;

int renderer_init(int width, int height);

int renderer_set_gm_framebuffer(const uint8_t *buffer);

int renderer_set_vt_texture_id(uint32_t texture_id);

int renderer_generate();

int renderer_destroy();