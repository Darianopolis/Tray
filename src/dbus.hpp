#pragma once

#include "core.hpp"

#include "dbus_unpack.hpp"

#include <systemd/sd-bus.h>

namespace dbus
{
    inline
    auto call(
        sd_bus* bus,
        const char* service,
        const char* path,
        const char* iface,
        const char* method,
        const char* types,    // sd_bus_message_append format string, may be ""
        ...) -> Value
    {
        sd_bus_message* req = nullptr;
        defer { sd_bus_message_unrefp(&req); };
        int r = sd_bus_message_new_method_call(bus, &req, service, path, iface, method);
        if (r < 0) return Value::make_error(strerror(-r));

        if (types && types[0] != '\0') {
            va_list ap;
            va_start(ap, types);
            r = sd_bus_message_appendv(req, types, ap);
            va_end(ap);
            if (r < 0) return Value::make_error(strerror(-r));
        }

        sd_bus_message* reply = nullptr;
        sd_bus_error err = SD_BUS_ERROR_NULL;
        defer {
            sd_bus_message_unrefp(&reply);
            sd_bus_error_free(&err);
        };
        r = sd_bus_call(bus, req, 0, &err, &reply);

        if (r < 0) {
            std::string error = err.message ? err.message : strerror(-r);
            // std::println(stderr, "bus_call {}.{} failed: {}", iface, method, error);
            return Value::make_error(std::move(error));
        }

        return unpack_message(reply);
    }

    inline
    auto get_property(sd_bus* bus, const char* service, const char* path, const char* iface,  const char* prop)
    {
        return call(bus, service, path, "org.freedesktop.DBus.Properties", "Get", "ss", iface, prop);
    }
}
