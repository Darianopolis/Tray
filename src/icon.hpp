#pragma once

#include "core.hpp"

#include <vector>
#include <memory>
#include <cstring>
#include <print>
#include <filesystem>
#include <unordered_set>

#include <SDL3/SDL.h>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

struct IconTexture
{
    std::vector<uint8_t> data;
    int w;
    int h;
    std::unique_ptr<SDL_Texture, decltype([](SDL_Texture* tex) { SDL_DestroyTexture(tex); })> tex;
};

static
auto load_texture(SDL_Renderer* renderer, int w, int h, const void* data, SDL_PixelFormat format, int pitch = 0) -> IconTexture
{
    IconTexture tex = {};

    tex.w = w;
    tex.h = h;

    tex.data.resize(w * h * 4);
    std::memcpy(tex.data.data(), data, tex.data.size());

    if (!pitch) pitch = w * 4;

    auto surface = SDL_CreateSurfaceFrom(w, h, format, tex.data.data(), pitch);
    defer { SDL_DestroySurface(surface); };

    tex.tex.reset(SDL_CreateTextureFromSurface(renderer, surface));

    return tex;
}

static
auto ensure_size(GdkPixbuf* pb, int size) -> GdkPixbuf*
{
    if (gdk_pixbuf_get_width(pb) == size &&
        gdk_pixbuf_get_height(pb) == size)
        return static_cast<GdkPixbuf*>(g_object_ref(pb));

    return gdk_pixbuf_scale_simple(pb, size, size, GDK_INTERP_BILINEAR);
}

static
auto try_load_file(const std::filesystem::path& path, int size) -> GdkPixbuf*
{
    if (!std::filesystem::exists(path))
        return nullptr;

    GError*    err = nullptr;
    GdkPixbuf* pb  = nullptr;

    if (path.extension() == ".svg") {
        pb = gdk_pixbuf_new_from_file_at_size(path.c_str(), size, size, &err);
    } else {
        pb = gdk_pixbuf_new_from_file(path.c_str(), &err);
    }

    if (err) g_error_free(err);
    if (!pb)  return nullptr;

    GdkPixbuf* scaled = ensure_size(pb, size);
    g_object_unref(pb);
    return scaled;
}

static constexpr std::array icon_exts = { ".svg", ".png", ".xpm" };

static
auto find_icon_file(const std::filesystem::path& root, const std::string& name, bool recurse) -> std::optional<std::filesystem::path>
{
    if (!std::filesystem::is_directory(root))
        return std::nullopt;

    std::unordered_set<std::filesystem::path> needles;
    for (const char* ext : icon_exts) {
        needles.emplace(name + ext);
    }

    auto check = [&](const std::filesystem::path& dir) -> std::optional<std::filesystem::path> {
        auto ext = dir.filename();
        if (needles.contains(dir.filename())) return dir;
        return std::nullopt;
    };

    if (recurse) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) continue;
            if (needles.contains(entry.path().filename())) return entry.path();
        }
    } else {
        for (auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) continue;
            if (needles.contains(entry.path().filename())) return entry.path();
        }
    }

    return std::nullopt;
}

static
auto load_texture_from_pixbuf(SDL_Renderer* renderer, GdkPixbuf* pixbuf) -> IconTexture
{
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        pixbuf = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
    }

    return load_texture(renderer,
        gdk_pixbuf_get_width(pixbuf),
        gdk_pixbuf_get_height(pixbuf),
        gdk_pixbuf_get_pixels(pixbuf),
        SDL_PIXELFORMAT_ABGR8888,
        gdk_pixbuf_get_rowstride(pixbuf));
}

static
auto load_texture_from_icon_name(SDL_Renderer* renderer, const char* name) -> IconTexture
{
    gtk_init_check(nullptr, nullptr);

    // std::println("    Loading icon: \"{}\"", name);
    // auto start = std::chrono::steady_clock::now();
    // defer {
    //     auto end = std::chrono::steady_clock::now();
    //     std::println("      completed in {}", fmt_time(end - start));
    // };

    int requested_size = 32;

    auto* theme = gtk_icon_theme_get_default();
    auto* info = gtk_icon_theme_lookup_icon(theme, name, requested_size, {});

    GError* err = nullptr;
    GdkPixbuf* pixbuf;
    if (info) {
        pixbuf = gtk_icon_info_load_icon(info, &err);
        return load_texture_from_pixbuf(renderer, pixbuf);
    } else {
        const std::vector<std::pair<std::filesystem::path, bool>> search_dirs = {
            { std::filesystem::path(g_get_home_dir()) / ".local/share/spotify-launcher/install/usr/share/spotify/icons", false },

            { "/usr/share/pixmaps",                                           true },
            { "/usr/share/icons/hicolor",                                     true },
            { std::filesystem::path(g_get_home_dir()) / ".local/share/icons", true },
            { "/usr/share/icons",                                             true },
        };

        for (auto& [dir, recurse] : search_dirs) {
            if (auto hit = find_icon_file(dir, name, recurse)) {
                if (GdkPixbuf* pb = try_load_file(*hit, requested_size)) {
                    return load_texture_from_pixbuf(renderer, pb);
                }
            }
        }
    }

    return {};
}
