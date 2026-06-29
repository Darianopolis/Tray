#include <print>
#include <functional>
#include <memory>
#include <cstring>
#include <fstream>

#include "icon.hpp"

#include "dbus.hpp"
#include "dbus_pack.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

struct MenuItem
{
    std::string label;
    bool separator = false;
    bool enabled = true;
    bool visible = true;
    std::vector<MenuItem> children;

    std::function<void()> on_click;
};

struct Item
{
    std::string unique;

    std::string title;
    IconTexture icon;
    std::optional<MenuItem> menu;

    std::function<void()> on_click;
};

sd_bus*           g_bus;
std::vector<Item> g_items;
SDL_Renderer*     g_renderer;

// -----------------------------------------------------------------------------

static
auto parse_menu_item(const char* service, const char* object_path, dbus::ValueIterator item, int depth = 0) -> MenuItem
{
    auto indent = [&](int delta = 0) {
        return std::string(depth + delta, ' ') + std::string(depth + delta, ' ');
    };

    MenuItem menu_item = {};

    int32_t id = item[0].as_signed().value_or(-1);
    std::println("{}{}", indent(), id);

    menu_item.on_click = [id, service = std::string(service), object_path = std::string(object_path)] {
        dbus::call(g_bus, service.c_str(), object_path.c_str(), "com.canonical.dbusmenu", "Event", "isvu",
            id,
            "clicked",
            "i", 0,
            (uint32_t)(SDL_GetTicks() / 1000));
    };

    auto properties = item[1];
    if (auto label = properties["label"].as_string()) {
        std::println("{}label: {}", indent(1), *label);
    }
    if (!properties["enabled"].as_boolean().value_or(true)) {
        std::println("{}disabled", indent(1));
    }
    if (!properties["visible"].as_boolean().value_or(true)) {
        std::println("{}hidden", indent(1));
    }
    if (properties["type"].as_string().value_or("") == "separator") {
        std::println("{}separator", indent(1));
    }
    if (auto children_display = properties["children-display"].as_string()) {
        std::println("{}children-display: {}", indent(1), *children_display);
    }

    menu_item.label = properties["label"].as_string().value_or("");
    menu_item.enabled = properties["enabled"].as_boolean().value_or(true);
    menu_item.visible = properties["visible"].as_boolean().value_or(true);
    menu_item.separator = properties["type"].as_string().value_or("") == "separator";

    auto children = item[2];
    {
        dbus::ValueIterator value;
        for (int i = 0; (value = children[i]); ++i) {
            menu_item.children.emplace_back(parse_menu_item(service, object_path, value, depth + 2));
        }
    }

    return menu_item;
}

static
auto load_menu(const char* service, const char* object_path) -> MenuItem
{
    auto layout = dbus::call(g_bus, service, object_path, "com.canonical.dbusmenu", "GetLayout", "iias", int32_t(0), int32_t(-1), 0);

    auto revision = dbus::ValueIterator(layout)[0].as_unsigned().value_or(12345678);
    std::println("    revision: {}", revision);

    return parse_menu_item(service, object_path, dbus::ValueIterator(layout)[1], 2);
}

static
auto load(const char* service, const char* object_path) -> Item
{
    std::println("  service: {}", service);
    std::println("  object_path: {}", object_path);

    const char* interface = "org.kde.StatusNotifierItem";

    Item item = {};

    item.unique = std::format("{}{}", service, object_path);

    item.on_click = [service = std::string(service), object_path = std::string(object_path)] {
        dbus::call(g_bus, service.c_str(), object_path.c_str(), "org.kde.StatusNotifierItem", "Activate", "ii",
            int32_t(0),
            int32_t(0));
    };

    auto title = dbus::ValueIterator(dbus::get_property(g_bus, service, object_path, interface, "Title"))[0].as_string();
    if (title) {
        std::println("  Title: {}", *title);
        item.title = *title;
    }

    {
        // Tooltip

        auto tooltip_res = dbus::get_property(g_bus, service, object_path, interface, "ToolTip");
        if (auto tooltip = dbus::ValueIterator{tooltip_res}[0]) {
            if (item.title.empty()) {
                item.title = tooltip[2].as_string().value();
            }
            std::println("  Tooltip: {}", tooltip[2].as_string().value());
        }
    }

    {
        // Get process information
        auto res = dbus::call(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", "s", service);
        auto pid = dbus::ValueIterator{res}[0].as_signed().value_or(-1);

        std::println("  PID: {}", pid);

        if (std::ifstream file{std::format("/proc/{}/comm", pid), std::ios::binary}; file.is_open()) {
            std::string process_name;
            std::getline(file, process_name);
            std::println("  Process Name: {}", process_name);
            if (item.title.empty()) {
                item.title = process_name;
            }
        }
    }

    auto icon_name = dbus::ValueIterator(dbus::get_property(g_bus, service, object_path, interface, "IconName"))[0].as_string();
    if (icon_name) {
        std::println("  IconName: {}", *icon_name);
        item.icon = load_texture_from_icon_name(g_renderer, icon_name->c_str());
    }

    auto icon_pixmap = dbus::get_property(g_bus, service, object_path, interface, "IconPixmap");
    if (auto array = dbus::ValueIterator(icon_pixmap)[0]) {
        dbus::ValueIterator pixmap;
        for (int i = 0; (pixmap = array[i]); i++) {
            auto width = pixmap[0].as_signed().value_or(-1);
            auto height = pixmap[1].as_signed().value_or(-1);
            std::println("  IconPixmap[{}] = {}x{}", i, width, height);

            std::vector<uint8_t> bytes;
            {
                dbus::ValueIterator array = pixmap[2];
                for (int i = 0; i < width * height * 4; ++i) {
                    uint8_t value = array[i].as_unsigned().value();
                    // std::println("    [{}] = {}", i, value);
                    bytes.emplace_back(value);
                }
            }
            item.icon = load_texture(g_renderer, width, height, bytes.data());
        }
    }

    auto menu_path = dbus::ValueIterator(dbus::get_property(g_bus, service, object_path, interface, "Menu"))[0].as_string();
    if (menu_path) {
        std::println("  Menu: {}", *menu_path);
        item.menu = load_menu(service, menu_path->c_str());
    }

    return item;
}

static
void load(std::string_view item_path)
{
    std::println("Loading item: {}", item_path);

    auto slash = item_path.find('/');
    std::string service;
    std::string object_path;
    if (slash == std::string::npos) {
        service = item_path;
        object_path = "/StatusNotifierItem";
    } else {
        service = item_path.substr(0, slash);
        object_path = item_path.substr(slash);
    }

    auto item = load(service.c_str(), object_path.c_str());

    if (item.title.empty() && !item.icon.tex && !item.menu) {
        std::println("loaded item is empty, skipping");
        return;
    }

    g_items.emplace_back(std::move(item));
}

// -----------------------------------------------------------------------------

static
void dump_menu_item(MenuItem& item, int depth = 0)
{
    auto indent = [&] { return std::string(depth * 2, ' '); };
    std::println("{}label: {}", indent(), item.label);
    if (item.separator) std::println("{}separator", indent());
    for (auto& child : item.children) {
        dump_menu_item(child, depth + 1);
    }
}

static
void dump_item(Item& item)
{
    std::println("{}", item.title);
    if (item.menu) {
        dump_menu_item(*item.menu, 1);
    }
}

// -----------------------------------------------------------------------------

void draw_menu_item(MenuItem& item)
{
    if (item.separator) {
        ImGui::Separator();
        return;
    }

    if (item.label.empty()) {
        for (auto& child : item.children) {
            draw_menu_item(child);
        }
        return;
    }

    if (ImGui::Selectable(item.label.c_str()) && item.on_click) {
        item.on_click();
    }

    if (item.children.empty()) return;

    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        for (auto& child : item.children) {
            draw_menu_item(child);
        }
        ImGui::EndPopup();
    }
}

void frame()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    defer {
        ImGui::End();
        ImGui::PopStyleVar();
    };
    bool dont_close = true;
    if (!ImGui::Begin("Tray", &dont_close, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize)) return;

    for (auto& item : g_items) {
        const float ROW_H     = 32.0f;
        const float ICON_SIZE = 32.0f;
        const float H_PAD     = 3.0f;

        auto id = std::format("##{}", item.unique);

        auto title = item.title;
        if (title.empty()) title = item.unique;

        auto avail = ImGui::GetContentRegionAvail();

        auto cursor_pos = ImGui::GetCursorPos();
        ImVec2 row_origin = ImGui::GetCursorScreenPos();

        if (ImGui::Selectable(id.c_str(), false, {}, {avail.x, 32}) && item.on_click) {
            item.on_click();
        }

        if (item.icon.tex) {
            ImVec2 icon_pos(row_origin.x, row_origin.y);
            ImVec2 icon_end(icon_pos.x + ICON_SIZE, icon_pos.y + ICON_SIZE);
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)item.icon.tex.get(),
                icon_pos, icon_end);

        }

        if (!title.empty()) {
            float text_h    = ImGui::GetTextLineHeight();
            float text_y    = row_origin.y + (ROW_H - text_h) * 0.5f;
            float text_x    = row_origin.x + ICON_SIZE + H_PAD;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(text_x, text_y),
                ImGui::GetColorU32(ImGuiCol_Text),
                title.c_str());

        }

        if (item.menu) {
            if (ImGui::BeginPopupContextItem(id.c_str())) {
                draw_menu_item(*item.menu);
                ImGui::EndPopup();
            }
        }
    }
}

// -----------------------------------------------------------------------------

static
void find_well_known_names()
{
    auto res = dbus::call(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", nullptr);
    auto list = dbus::ValueIterator{res}[0];

    std::unordered_map<std::string, std::vector<std::string>> names;

    dbus::ValueIterator iname;
    for (int i = 0; (iname = list[i]); ++i) {
        auto name = iname.as_string().value();

        auto owned_res = dbus::call(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", "s", name.c_str());
        auto owner = dbus::ValueIterator{owned_res}[0].as_string().value();

        if (owner == name) continue;

        names[owner].emplace_back(name);
    }

    for (auto[owner, names] : names) {
        std::println("{}", owner);
        for (auto& name : names) {
            std::println("  {}", name);
        }
    }
}

// -----------------------------------------------------------------------------

int main()
{

    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow("Tray", 600, 800, SDL_WINDOW_RESIZABLE);
    g_renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);

    // ----

    check(sd_bus_open_user(&g_bus));

    auto msg = dbus::get_property(g_bus,
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems");

    if (auto items = dbus::ValueIterator(msg)[0]) {
        dbus::ValueIterator value;
        for (int i = 0; (value = items[i]); i++) {
            if (auto item = value.as_string()) {
                load(*item);
            }
        }
    }

    std::println("-- FIND NAMES");
    find_well_known_names();

    std::println("-- DUMP");

    for (auto& item : g_items) {
        dump_item(item);
    }

    std::println("-- GUI");

    int queued = 0;
    SDL_Event event;
    for (;;) {
        while (queued ? SDL_PollEvent(&event) : SDL_WaitEvent(&event)) {
            queued = 2;
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                break;case SDL_EVENT_QUIT:
                      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                      case SDL_EVENT_WINDOW_FOCUS_LOST:
                    goto CLOSE;
            }
        }

        if (!queued) continue;
        queued--;

        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();

        frame();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);

        SDL_RenderPresent(g_renderer);
    }
CLOSE:
}
