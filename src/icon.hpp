#pragma once

#include <vector>
#include <memory>

#include <SDL3/SDL.h>

struct IconTexture
{
    std::vector<uint8_t> data;
    int w;
    int h;
    std::unique_ptr<SDL_Texture, decltype([](SDL_Texture* tex) { SDL_DestroyTexture(tex); })> tex;
};

auto load_texture(SDL_Renderer* renderer, int w, int h, const void* data, SDL_PixelFormat format, int pitch = 0) -> IconTexture;

void init_icon_loader();

auto load_texture_from_icon_name(SDL_Renderer*, int w, int h, const char* name) -> IconTexture;
