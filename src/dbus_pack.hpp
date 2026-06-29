#pragma once

#include "dbus_value.hpp"

#include <systemd/sd-bus.h>

#include <print>

#include <cerrno>

static
void check(int value)
{
    if (value < 0) {
        std::println("Error: {}", strerror(-value));
    }
}

namespace dbus
{
    inline
    void append(sd_bus_message* message, std::string_view signature, const Value& value)
    {
        switch (signature[0]) {
            break;case SD_BUS_TYPE_ARRAY:
                sd_bus_message_open_container(message, 'a', std::string(signature.substr(1)).c_str());
                for (auto& item : value.as_array().elements) {
                    append(message, signature.substr(1), item);
                }
                sd_bus_message_close_container(message);
            break;case SD_BUS_TYPE_STRING:
            sd_bus_message_append(message, "s", value.as_string().value.c_str());
        }
    }
}
