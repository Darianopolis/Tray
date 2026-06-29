#include <print>
#include <stacktrace>
#include <iostream>
#include <flat_set>

#include "dbus.hpp"
#include "dbus_pack.hpp"
#include "enums.hpp"

static sd_bus*                    g_bus = nullptr;
static std::flat_set<std::string> g_items;

static
int StatusNotifierWatcher_RegisterStatusNotifierItem(sd_bus_message* msg, void* userdata, sd_bus_error* err)
{
    std::println("StatusNotifierWatcher.RegisterStatusNotifierItem");

    const char* sender = sd_bus_message_get_sender(msg);
    std::println("  sender: {}", sender);

    auto args = dbus::unpack_message(msg);

    auto object_path = dbus::ValueIterator(args)[0].as_string();
    if (!object_path || *object_path == sender) {
        std::println("  arg was service, replacing with /StatusNotifierItem");
        object_path = "/StatusNotifierItem";
    }
    std::println("  object_path: {}", *object_path);

    g_items.emplace(std::format("{}{}", sender, *object_path));

    return sd_bus_reply_method_return(msg, "");
}

static
int StatusNotifierWatcher_RegisterStatusNotifierHost(sd_bus_message* msg, void* userdata, sd_bus_error* err)
{
    std::println("StatusNotifierWatcher.RegisterStatusNotifierHost");
    return sd_bus_reply_method_return(msg, "");
}

static
int StatusNotifierWatcher_RegisteredStatusNotifierItems(
    sd_bus* bus, const char* path,
    const char* iface, const char* prop,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* err)
{
    std::println("StatusNotifierWatcher.RegisteredStatusNotifierItems");
    dbus::Array values;
    for (auto& item : g_items) {
        std::println("ITEM: {}", item);
        values.elements.emplace_back(dbus::String(item));
    }
    dbus::append(reply, "as", values);
    return 0;
}

static
int StatusNotifierWatcher_IsStatusNotifierHostRegistered(
    sd_bus* bus, const char* path,
    const char* iface, const char* prop,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* err)
{
    std::println("StatusNotifierWatcher.IsStatusNotifierHostRegistered");
    return sd_bus_message_append(reply, "b", true);
}

static
int StatusNotifierWatcher_ProtocolVersion(
    sd_bus* bus, const char* path,
    const char* iface, const char* prop,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* err)
{
    std::println("StatusNotifierWatcher.ProtocolVersion");
    return sd_bus_message_append(reply, "i", (int32_t)0);
}

static
const sd_bus_vtable StatusNotifierWatcher_VTable[] = {
    SD_BUS_VTABLE_START(0),

    // Methods
    SD_BUS_METHOD(
        "RegisterStatusNotifierItem", "s", "",
        StatusNotifierWatcher_RegisterStatusNotifierItem,
        SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD(
        "RegisterStatusNotifierHost", "s", "",
        StatusNotifierWatcher_RegisterStatusNotifierHost,
        SD_BUS_VTABLE_UNPRIVILEGED),

    // Properties
    SD_BUS_PROPERTY(
        "RegisteredStatusNotifierItems", "as",
        StatusNotifierWatcher_RegisteredStatusNotifierItems,
        0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

    SD_BUS_PROPERTY(
        "IsStatusNotifierHostRegistered", "b",
        StatusNotifierWatcher_IsStatusNotifierHostRegistered,
        0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

    SD_BUS_PROPERTY(
        "ProtocolVersion", "i",
        StatusNotifierWatcher_ProtocolVersion,
        0,
        SD_BUS_VTABLE_PROPERTY_CONST),

    SD_BUS_SIGNAL("StatusNotifierItemRegistered",   "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered",   "",  0),

    SD_BUS_VTABLE_END
};

int main()
{
    check(sd_bus_open_user(&g_bus));

    check(sd_bus_add_object_vtable(g_bus,
        nullptr,
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        StatusNotifierWatcher_VTable,
        nullptr));

    check(sd_bus_request_name(g_bus, "org.kde.StatusNotifierWatcher", 0));

    for (;;) {
        auto res = sd_bus_process(g_bus, nullptr);
        if (res > 0) continue;
        check(res);

        check(sd_bus_wait(g_bus, UINT64_MAX));
    }
}
