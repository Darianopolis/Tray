#include "dbus.hpp"

#include <flat_set>

int main()
{
    DBusError err;
    dbus_error_init(&err);
    defer { dbus_error_free(&err); };

    std::flat_set<std::string> items;

    auto* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    defer { dbus_connection_unref(conn); };

    dbus::VTable watcher(conn, "/StatusNotifierWatcher");
    watcher.interfaces["org.kde.StatusNotifierWatcher"]["RegisterStatusNotifierItem"] =
        [&](DBusConnection*, DBusMessage* msg) {
            auto sender = dbus_message_get_sender(msg);
            std::println("  sender: {}", sender);

            dbus::Iterator args(msg, dbus::iter::read);

            auto object_path = std::string(args.get_string().value_or(""));
            if (object_path.empty() || object_path == sender) {
                std::println("  arg was service, replacing with /StatusNotifierItem");
                object_path = "/StatusNotifierItem";
            }
            std::println("  object_path: {}", object_path);

            items.emplace(std::format("{}{}", sender, object_path));

            dbus::Message reply = dbus_message_new_method_return(msg);
            dbus_connection_send(conn, reply.get(), nullptr);
            return DBUS_HANDLER_RESULT_HANDLED;
        };
    watcher.interfaces["org.kde.StatusNotifierWatcher"]["RegisterStatusNotifierHost"] =
        [](DBusConnection* conn, DBusMessage* msg) {
            dbus::Message reply = dbus_message_new_method_return(msg);
            dbus_connection_send(conn, reply.get(), nullptr);
            return DBUS_HANDLER_RESULT_HANDLED;
        };
    watcher.properties["org.kde.StatusNotifierWatcher"]["RegisteredStatusNotifierItems"] = {
        "as",  [&](DBusConnection*, dbus::Iterator& out) {
            auto arr = out.open(DBUS_TYPE_ARRAY, "s");
            for (auto& item : items) {
                arr.append(DBUS_TYPE_STRING, item.c_str());
            }
            out.close(arr);
        },
    };
    watcher.properties["org.kde.StatusNotifierWatcher"]["IsStatusNotifierHostRegistered"] = {
        "b", [](DBusConnection* conn, dbus::Iterator& out) {
            out.append(DBUS_TYPE_BOOLEAN, true);
        }
    };
    watcher.properties["org.kde.StatusNotifierWatcher"]["ProtocolVersion"] = {
        "i", [](DBusConnection* conn, dbus::Iterator& out) {
            out.append(DBUS_TYPE_INT32, 0);
        }
    };

    auto res = dbus_bus_request_name(conn, "org.kde.StatusNotifierWatcher", DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err) || res == DBUS_REQUEST_NAME_REPLY_EXISTS) {
        std::println("ERROR - Failed to acquire org.kde.StatusNotifierWatcher name, exiting");
        return EXIT_FAILURE;
    }

    for (;;) {
        dbus_connection_read_write_dispatch(conn, -1);
    }
}
