#include "icon.hpp"

#include "core.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
//      IconTexture loading
// -----------------------------------------------------------------------------

auto load_texture(SDL_Renderer* renderer, int w, int h, const void* data, SDL_PixelFormat format, int pitch) -> IconTexture
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

// -----------------------------------------------------------------------------

using Pixbuf = std::unique_ptr<GdkPixbuf, decltype([](auto* pixbuf) { g_object_unref(pixbuf); })>;

static
auto load_texture_from_pixbuf(SDL_Renderer* renderer, Pixbuf pixbuf, int w, int h) -> IconTexture
{
    if (!gdk_pixbuf_get_has_alpha(pixbuf.get())) {
        pixbuf.reset(gdk_pixbuf_add_alpha(pixbuf.get(), false, 0, 0, 0));
    }

    if (gdk_pixbuf_get_width(pixbuf.get()) != w || gdk_pixbuf_get_height(pixbuf.get()) != h) {
        pixbuf.reset(gdk_pixbuf_scale_simple(pixbuf.get(), w, h, GDK_INTERP_BILINEAR));
    }

    return load_texture(renderer,
        gdk_pixbuf_get_width(pixbuf.get()),
        gdk_pixbuf_get_height(pixbuf.get()),
        gdk_pixbuf_get_pixels(pixbuf.get()),
        SDL_PIXELFORMAT_ABGR8888,
        gdk_pixbuf_get_rowstride(pixbuf.get()));
}

// -----------------------------------------------------------------------------
//      From GTK icon theme lookup
// -----------------------------------------------------------------------------

static
auto try_load_from_theme(const char* name, int preferred_size) -> GdkPixbuf*
{
    auto* theme = gtk_icon_theme_get_default();
    auto* info = gtk_icon_theme_lookup_icon(theme, name, preferred_size, {});

    if (!info) return nullptr;

    GError* err = nullptr;
    defer { if (err) g_error_free(err); };
    return gtk_icon_info_load_icon(info, &err);
}

// -----------------------------------------------------------------------------
//      From manual filesystem search
// -----------------------------------------------------------------------------


namespace {
    struct DirEntry
    {
        ino64_t        d_ino;    /* 64-bit inode number */
        off64_t        d_off;    /* Not an offset; see getdents() */
        unsigned short d_reclen; /* Size of this dirent */
        unsigned char  d_type;   /* File type */
        char           d_name[]; /* Filename (null-terminated) */
    };

    using SeenInodes = std::unordered_map<dev_t, std::unordered_set<ino_t>>;

    enum class WalkAction { cont, stop };
}

template<typename Callback>
static
auto walk_dirs(const char* path, SeenInodes& seen_inodes, const Callback& cb) -> WalkAction
{
    struct stat st;
    if (stat(path, &st) != 0) return WalkAction::cont;
    if (!S_ISDIR(st.st_mode)) return WalkAction::cont;
    if (!seen_inodes[st.st_dev].emplace(st.st_ino).second) return WalkAction::cont;
    if (cb(path, st) !=  WalkAction::cont) return WalkAction::stop;

    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return WalkAction::cont;
    defer { close(fd); };

    static constexpr size_t buffer_size = 65536;
    alignas(DirEntry) char buf[buffer_size];

    for (;;) {
        long n = syscall(SYS_getdents64, fd, buf, sizeof(buf));
        if (n <= 0) break;

        for (uintptr_t off = 0; off < n;) {
            auto* d = reinterpret_cast<DirEntry*>(buf + off);
            off += d->d_reclen;

            const char* name = d->d_name;

            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
                continue;
            }

            if (d->d_type == DT_DIR || d->d_type == DT_LNK || d->d_type == DT_UNKNOWN) {
                std::string child = std::format("{}/{}", path, name);
                if (walk_dirs(child.c_str(), seen_inodes, cb) == WalkAction::stop) {
                    return WalkAction::stop;
                }
            }
        }
    }

    return WalkAction::cont;
}

static
auto try_load_from_path(const std::filesystem::path& path, int w, int h) -> GdkPixbuf*
{
    if (!std::filesystem::exists(path)) return nullptr;

    GError* err = nullptr;
    defer { if (err) g_error_free(err); };

    if (std::string_view(path.c_str()).ends_with(".svg")) {
        return gdk_pixbuf_new_from_file_at_size(path.c_str(), w, h, &err);
    } else {
        return gdk_pixbuf_new_from_file(path.c_str(), &err);
    }
}

static
auto try_load_from_dirs(const char* name, int w, int h) -> GdkPixbuf*
{
    static const std::filesystem::path home_dir = std::filesystem::path(g_get_home_dir());
    static const std::array search_dirs = std::to_array<std::filesystem::path>({

        // TODO: Find a way to find icons in weird spots like this efficiently
        home_dir / ".local/share/spotify-launcher/install/usr/share/spotify/icons",

        home_dir / ".local/share/pixmaps",
        home_dir / ".local/share/icons/hicolor",
        home_dir / ".local/share/icons",

        "/usr/local/share/pixmaps",
        "/usr/local/share/icons/hicolor",
        "/usr/local/share/icons",

        "/usr/share/pixmaps",
        "/usr/share/icons/hicolor",
        "/usr/share/icons",
    });

    static constexpr std::array icon_exts = { ".svg", ".png", ".xpm" };
    std::vector<std::filesystem::path> file_names;
    for (const char* ext : icon_exts) {
        file_names.emplace_back(std::format("{}{}", name, ext));
    }

    SeenInodes seen_inodes;
    for (auto& dir : search_dirs) {
        if (!std::filesystem::is_directory(dir)) continue;

        GdkPixbuf* pixbuf = nullptr;
        walk_dirs(dir.c_str(), seen_inodes, [&](const char* cpath, struct stat& st) {
            for (auto& filename : file_names) {
                if ((pixbuf = try_load_from_path(dir / filename, w, h))) {
                    return WalkAction::stop;
                }
            }
            return WalkAction::cont;
        });
        if (pixbuf) return pixbuf;
    }

    return nullptr;
}

// -----------------------------------------------------------------------------

void init_icon_loader()
{
    gtk_init_check(nullptr, nullptr);
}

auto load_texture_from_icon_name(SDL_Renderer* renderer, int w, int h, const char* name) -> IconTexture
{
    Pixbuf pixbuf{try_load_from_theme(name, std::max(w, h))};
    if (!pixbuf) pixbuf.reset(try_load_from_dirs(name, w, h));
    if (!pixbuf) return {};
    return load_texture_from_pixbuf(renderer, std::move(pixbuf), w, h);
}
