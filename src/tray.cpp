#include <print>
#include <functional>
#include <memory>
#include <cstring>
#include <fstream>
#include <ranges>

#include "icon.hpp"
#include "dbus.hpp"

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

DBusConnection*   g_bus;
std::vector<Item> g_items;
SDL_Renderer*     g_renderer;

// -----------------------------------------------------------------------------

static
auto parse_menu_item(const char* service, const char* object_path, dbus::Iterator item, int depth = 0) -> MenuItem
{
    MenuItem menu_item = {};

    int32_t id = item[0].get<int>().value_or(-1);

    menu_item.on_click = [id, service = std::string(service), object_path = std::string(object_path)] {
        dbus::Message call = dbus_message_new_method_call(service.c_str(), object_path.c_str(), "com.canonical.dbusmenu", "Event");

        dbus::AppendIterator args(call.get());
        args.append(DBUS_TYPE_INT32, id);
        args.append(DBUS_TYPE_STRING, "clicked");
        { auto var = args.open(DBUS_TYPE_VARIANT, "i");
          var.append(DBUS_TYPE_INT32, 0);
          args.close(var); }
        args.append(DBUS_TYPE_UINT32, SDL_GetTicks() / 1000);

        dbus::send(g_bus, call.get());
    };

    for (auto property : item[1]) {
        auto key = property[0].get_string().value();
        if      (key == "label")   menu_item.label     = property[1].get_string().value();
        else if (key == "enabled") menu_item.enabled   = property[1].get<bool>().value();
        else if (key == "visible") menu_item.visible   = property[1].get<bool>().value();
        else if (key == "type")    menu_item.separator = property[1].get_string().value() == "separator";
    }

    for (auto child : item[2]) {
        menu_item.children.emplace_back(parse_menu_item(service, object_path, child, depth + 2));
    }

    return menu_item;
}


static
auto load_menu(const char* service, const char* object_path, dbus::Iterator iter) -> MenuItem
{
    auto revision = iter.get<int64_t>().value_or(-1);

    return parse_menu_item(service, object_path, ++iter, 2);
}

static
auto load_menu(const char* service, const char* menu_path) -> MenuItem
{
    dbus::Message call = dbus_message_new_method_call(service, menu_path, "com.canonical.dbusmenu", "GetLayout");
    dbus::AppendIterator args(call.get());
    args.append(DBUS_TYPE_INT32, 0);
    args.append(DBUS_TYPE_INT32, -1);
    args.close(args.open(DBUS_TYPE_ARRAY, "s"));

    return load_menu(service, menu_path, dbus::Iterator(dbus::send_with_reply(g_bus, call.get()).get()));
}

#define TIME_SCOPE(Message) \
    auto start = std::chrono::steady_clock::now(); \
    defer { \
        auto end = std::chrono::steady_clock::now(); \
        std::println(Message, fmt_time(end - start)); \
    }

bool g_has_menu_path_cache = true;

static
auto load(const char* service, const char* object_path) -> Item
{
    const char* interface = "org.kde.StatusNotifierItem";

    Item item = {};

    item.unique = std::format("{}{}", service, object_path);

    item.on_click = [service = std::string(service), object_path = std::string(object_path)] {
        dbus::Message call = dbus_message_new_method_call(service.c_str(), object_path.c_str(),
                                                          "org.kde.StatusNotifierItem", "Activate");
        dbus::AppendIterator args(call.get());
        args.append(DBUS_TYPE_INT32, 0);
        args.append(DBUS_TYPE_INT32, 0);
        dbus::send(g_bus, call.get());
    };

    std::string cached_menu_path;
    DBusPendingCall* menu_call = nullptr;
    defer { if (menu_call) dbus_pending_call_unref(menu_call); };
    if (g_has_menu_path_cache) {
        cached_menu_path = [&] {
            dbus::Message call = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                              "com.darianopolis.TrayService", "GetCachedMenuPath");
            dbus::AppendIterator args(call.get());
            args.append(DBUS_TYPE_STRING, service);
            args.append(DBUS_TYPE_STRING, object_path);
            dbus::Message reply = dbus::send_with_reply(g_bus, call.get());
            g_has_menu_path_cache = reply.get();
            return dbus::Iterator(reply.get()).get_string().value_or("");
        }();

        if (!cached_menu_path.empty()) {
            dbus::Message call = dbus_message_new_method_call(service, cached_menu_path.c_str(), "com.canonical.dbusmenu", "GetLayout");
            dbus::AppendIterator args(call.get());
            args.append(DBUS_TYPE_INT32, 0);
            args.append(DBUS_TYPE_INT32, -1);
            args.close(args.open(DBUS_TYPE_ARRAY, "s"));
            menu_call = dbus::send_with_reply_future(g_bus, call.get());
        }
    }

    dbus::Message properties_msg;
    std::unordered_map<std::string, dbus::Iterator> properties;
    {
        TIME_SCOPE("  Properties queried in in {}");

        dbus::Message call = dbus_message_new_method_call(service, object_path,
                                                          "org.freedesktop.DBus.Properties", "GetAll");
        dbus::AppendIterator args(call.get());
        args.append(DBUS_TYPE_STRING, interface);
        properties_msg = dbus::send_with_reply(g_bus, call.get());
        for (auto entry : dbus::Iterator(properties_msg.get())) {
            properties[entry[0].get_string().value()] = entry[1];
        }
    }

    if (auto tooltip = properties["ToolTip"]) {
        item.title = tooltip[2].get_string().value();
    }

    if (item.title.empty()) {
        item.title = properties["Title"].get_string().value_or("");
    }

    if (item.title.empty()) {
        TIME_SCOPE("  Process name queried in in {}");

        // Use process name as title

        dbus::Message call = dbus_message_new_method_call("org.freedesktop.DBus","/org/freedesktop/DBus",
                                                          "org.freedesktop.DBus", "GetConnectionUnixProcessID");
        dbus::AppendIterator args(call.get());
        args.append(DBUS_TYPE_STRING, service);
        dbus::Message reply = dbus::send_with_reply(g_bus, call.get());

        auto pid = dbus::Iterator(reply.get()).get<int>().value_or(-1);

        if (std::ifstream file{std::format("/proc/{}/comm", pid), std::ios::binary}; file.is_open()) {
            std::string process_name;
            std::getline(file, process_name);
            item.title = process_name;
        }
    }

    if (auto icon_name = properties["IconName"].get_string().value_or(""); !icon_name.empty()) {
        TIME_SCOPE("  Icon loaded from IconName in {}");

        item.icon = load_texture_from_icon_name(g_renderer, icon_name.c_str());
    }

    if (auto pixmaps = properties["IconPixmap"]; pixmaps && !item.icon.tex) {
        TIME_SCOPE("  Icon loaded from IconPixmap in {}");

        for (auto pixmap : pixmaps) {
            auto width = pixmap[0].get<int>().value_or(-1);
            auto height = pixmap[1].get<int>().value_or(-1);

            std::vector<uint8_t> bytes;
            bytes.reserve(width * height * 4);
            for (auto byte : pixmap[2]) {
                bytes.emplace_back(byte.get<uint8_t>().value());
            }

            item.icon = load_texture(g_renderer, width, height, bytes.data(), SDL_PIXELFORMAT_BGRA8888);
        }
    }

    if (auto menu_path = properties["Menu"].get_string()) {

        if (*menu_path == cached_menu_path && menu_call) {
            TIME_SCOPE("  Menu loaded (prefetched) in {}");

            dbus_pending_call_block(menu_call);
            dbus::Message reply = dbus_pending_call_steal_reply(menu_call);
            item.menu = load_menu(service, menu_path->c_str(), reply.get());
        } else {
            TIME_SCOPE("  Menu loaded (fallback) in {}");

            item.menu = load_menu(service, menu_path->c_str());

            if (g_has_menu_path_cache) {
                // Update menu path in cache

                dbus::Message call = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                                  "com.darianopolis.TrayService", "SetCachedMenuPath");
                dbus::AppendIterator args(call.get());
                args.append(DBUS_TYPE_STRING, service);
                args.append(DBUS_TYPE_STRING, menu_path->c_str());
                dbus::send(g_bus, call.get());
            }
        }
    }

    return item;
}

static
void load(std::string_view item_path)
{
    std::println("Loading item: {}", item_path);
    TIME_SCOPE("Item loaded in {}");

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
    if (!item.separator && item.label.empty()) {
        for (auto& child : item.children) {
            dump_menu_item(child, depth);
        }
    }
    auto indent = [&] { return std::string(depth * 2, ' '); };
    if (item.separator) std::println("{}separator", indent());
    else std::println("{}item: {}", indent(), item.label);
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

static
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


    if (item.children.empty()) {
        if (ImGui::MenuItem(item.label.c_str(), nullptr, false, item.enabled)) {
            item.on_click();
        }
    } else {
        if (ImGui::BeginMenu(item.label.c_str(), item.enabled)) {
            for (auto& child : item.children) {
                draw_menu_item(child);
            }
            ImGui::EndMenu();
        }
        if (ImGui::IsItemClicked()) {
            item.on_click();
        }
    }
}

static bool g_any_popup_open;

static
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

    if (!dont_close || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !g_any_popup_open)) {
        _exit(0);
    }

    bool space_pressed = ImGui::IsKeyPressed(ImGuiKey_Space);

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
            if (!space_pressed) item.on_click();
        }
        bool item_focused = ImGui::IsItemFocused();

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
            if (item_focused && space_pressed) {
                ImGui::OpenPopup(id.c_str());
            }
            if (ImGui::BeginPopupContextItem(id.c_str())) {
                draw_menu_item(*item.menu);
                ImGui::EndPopup();
            }
        }
    }
}

// -----------------------------------------------------------------------------

int main()
{
    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow("Tray", 800, 400, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    g_renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    g_bus = dbus::connect(DBUS_BUS_SESSION);
    defer { dbus_connection_unref(g_bus); };

    {
        dbus::Message call = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                          "org.freedesktop.DBus.Properties", "Get");
        dbus::AppendIterator args(call.get());
        args.append(DBUS_TYPE_STRING, "org.kde.StatusNotifierWatcher");
        args.append(DBUS_TYPE_STRING, "RegisteredStatusNotifierItems");
        for (auto item : dbus::Iterator(dbus::send_with_reply(g_bus, call.get()).get())) {
            load(item.get_string().value());
        }
    }

    // for (auto& item : g_items) {
    //     dump_item(item);
    // }

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

        g_any_popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);

        SDL_RenderPresent(g_renderer);
        SDL_ShowWindow(window);
    }
CLOSE:
}
