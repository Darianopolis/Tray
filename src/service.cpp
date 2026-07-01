#include "dbus.hpp"

#include <flat_map>

static
auto check_service(DBusConnection* conn, const char* service) -> bool
{
    dbus::Message call = dbus_message_new_method_call("org.freedesktop.DBus","/org/freedesktop/DBus",
                                                      "org.freedesktop.DBus", "GetConnectionUnixProcessID");
    dbus::AppendIterator args(call.get());
    args.append(DBUS_TYPE_STRING, service);

    DBusError err;
    dbus_error_init(&err);
    defer { dbus_error_free(&err); };
    dbus::Message reply = dbus_connection_send_with_reply_and_block(conn, call.get(), -1, &err);

    bool active = dbus::Iterator(reply.get()).get<int>().has_value();
    if (!active) std::println("Service {} is no longer alive!", service);
    return active;
}

int main()
{
    std::flat_map<std::string, std::string> items;

    auto* conn = dbus::connect(DBUS_BUS_SESSION);
    defer { dbus_connection_unref(conn); };

    dbus::VTable watcher(conn, "/StatusNotifierWatcher");
    watcher.interfaces["org.kde.StatusNotifierWatcher"]["RegisterStatusNotifierItem"] =
        [&](DBusConnection*, DBusMessage* msg) {
            auto sender = dbus_message_get_sender(msg);
            std::println("  sender: {}", sender);

            dbus::Iterator args(msg);

            auto object_path = std::string(args.get_string().value_or(""));
            if (object_path.empty() || object_path == sender) {
                std::println("  arg was service, replacing with /StatusNotifierItem");
                object_path = "/StatusNotifierItem";
            }
            std::println("  object_path: {}", object_path);

            items[sender] = object_path;

            dbus::Message reply = dbus_message_new_method_return(msg);
            dbus::send(conn, reply.get());
            return DBUS_HANDLER_RESULT_HANDLED;
        };
    watcher.interfaces["org.kde.StatusNotifierWatcher"]["RegisterStatusNotifierHost"] =
        [](DBusConnection* conn, DBusMessage* msg) {
            dbus::Message reply = dbus_message_new_method_return(msg);
            dbus::send(conn, reply.get());
            return DBUS_HANDLER_RESULT_HANDLED;
        };
    watcher.properties["org.kde.StatusNotifierWatcher"]["RegisteredStatusNotifierItems"] = {
        "as",  [&](DBusConnection*, dbus::AppendIterator& out) {
            auto arr = out.open(DBUS_TYPE_ARRAY, "s");
            std::erase_if(items, [&](const std::pair<std::string, std::string>& item) {
                return !check_service(conn, item.first.c_str());
            });
            for (auto[service, path] : items) {
                arr.append(DBUS_TYPE_STRING, std::format("{}{}", service, path).c_str());
            }
            out.close(arr);
        },
    };
    watcher.properties["org.kde.StatusNotifierWatcher"]["IsStatusNotifierHostRegistered"] = {
        "b", [](DBusConnection* conn, dbus::AppendIterator& out) {
            out.append(DBUS_TYPE_BOOLEAN, true);
        }
    };
    watcher.properties["org.kde.StatusNotifierWatcher"]["ProtocolVersion"] = {
        "i", [](DBusConnection* conn, dbus::AppendIterator& out) {
            out.append(DBUS_TYPE_INT32, 0);
        }
    };

    if (dbus::request_name(conn, "org.kde.StatusNotifierWatcher", DBUS_NAME_FLAG_DO_NOT_QUEUE)
            != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        std::println("ERROR - Failed to acquire org.kde.StatusNotifierWatcher name, exiting");
        return EXIT_FAILURE;
    }

    for (;;) {
        dbus_connection_read_write_dispatch(conn, -1);
    }
}
