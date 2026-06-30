#pragma once

#include "core.hpp"

#include <dbus/dbus.h>

#include <print>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <functional>

namespace dbus
{
    static
    const char* type_to_string(int type)
    {
        switch (type) {
            break;case DBUS_TYPE_BYTE:        return "BYTE";
            break;case DBUS_TYPE_BOOLEAN:     return "BOOLEAN";
            break;case DBUS_TYPE_INT16:       return "INT16";
            break;case DBUS_TYPE_UINT16:      return "UINT16";
            break;case DBUS_TYPE_INT32:       return "INT32";
            break;case DBUS_TYPE_UINT32:      return "UINT32";
            break;case DBUS_TYPE_INT64:       return "INT64";
            break;case DBUS_TYPE_UINT64:      return "UINT64";
            break;case DBUS_TYPE_DOUBLE:      return "DOUBLE";
            break;case DBUS_TYPE_STRING:      return "STRING";
            break;case DBUS_TYPE_OBJECT_PATH: return "OBJECT_PATH";
            break;case DBUS_TYPE_SIGNATURE:   return "SIGNATURE";
            break;case DBUS_TYPE_UNIX_FD:     return "UNIX_FD";
            break;case DBUS_TYPE_ARRAY:       return "ARRAY";
            break;case DBUS_TYPE_VARIANT:     return "VARIANT";
            break;case DBUS_TYPE_STRUCT:      return "STRUCT";
            break;case DBUS_TYPE_DICT_ENTRY:  return "DICT_ENTRY";
        }
        return "N/A";
    }

    struct Message
    {
        DBusMessage* message;

        void reset(DBusMessage* _message = nullptr)
        {
            if (message) {
                dbus_message_unref(message);
            }
            message = _message;
            if (_message) {
                dbus_message_ref(_message);
            }
        }

        Message(DBusMessage* _message)
            : message(_message)
        {
            if (message) {
                dbus_message_ref(message);
            }
        }

        Message(const Message& other)
            : message(other.message)
        {
            if (message) {
                dbus_message_ref(message);
            }
        }

        Message& operator=(const Message& other)
        {
            if (message != other.message) {
                reset(other.message);
            }
            return *this;
        }

        auto get() const -> DBusMessage*
        {
            return message;
        }

        ~Message()
        {
            if (message) {
                dbus_message_unref(message);
            }
        }
    };

    namespace iter
    {
        struct Read {};
        constexpr Read read;

        struct Append {};
        constexpr Append append;
    }

    struct Iterator
    {
        mutable DBusMessageIter iter;
        bool closed = false;

        // Init

private:
        Iterator(const DBusMessageIter& _iter, bool _at_end = false)
            : iter(_iter)
            , closed(_at_end)
        {}

public:
        Iterator()
            : closed(true)
        {
            dbus_message_iter_init_closed(&iter);
        }

        Iterator(DBusMessage* message, iter::Read)
        {
            if (message) {
                dbus_message_iter_init(message, &iter);
            } else {
                dbus_message_iter_init_closed(&iter);
                closed = true;
            }
        }

        Iterator(DBusMessage* message, iter::Append)
        {
            if (message) {
                dbus_message_iter_init_append(message, &iter);
            } else {
                dbus_message_iter_init_closed(&iter);
                closed = true;
            }
        }


        // Append

        template<typename T>
        void append(int type, const T& value)
        {
            if constexpr (std::is_arithmetic_v<T>) {

    #define TRAY_DBUS_APPEND_TYPE(Type) \
                { \
                    Type storage = value; \
                    if (!dbus_message_iter_append_basic(&iter, type, &storage)) { \
                        assert(false && "Failed to append basic arithmetic type");\
                    } \
                }

                switch (type) {

                    break;case DBUS_TYPE_BYTE:    TRAY_DBUS_APPEND_TYPE(uint8_t)
                    break;case DBUS_TYPE_BOOLEAN: TRAY_DBUS_APPEND_TYPE(int)
                    break;case DBUS_TYPE_INT16:   TRAY_DBUS_APPEND_TYPE(int16_t)
                    break;case DBUS_TYPE_UINT16:  TRAY_DBUS_APPEND_TYPE(uint16_t)
                    break;case DBUS_TYPE_INT32:   TRAY_DBUS_APPEND_TYPE(int32_t)
                    break;case DBUS_TYPE_UINT32:  TRAY_DBUS_APPEND_TYPE(uint32_t)
                    break;case DBUS_TYPE_INT64:   TRAY_DBUS_APPEND_TYPE(int64_t)
                    break;case DBUS_TYPE_UINT64:  TRAY_DBUS_APPEND_TYPE(uint64_t)
                    break;case DBUS_TYPE_DOUBLE:  TRAY_DBUS_APPEND_TYPE(double)
                    break;default:
                        assert(false && "Type is not arithmetic");
                }

            } else if constexpr (std::is_convertible_v<T, const char*>) {
                const char* string = value;
                if (!dbus_message_iter_append_basic(&iter, type, &string)) {
                    assert(false && "Failed to append basic string type");
                }
            } else {
                assert(false && "Unsupported T for append");
            }
        }

        auto open(int type, const char* signature) -> Iterator
        {
            DBusMessageIter sub;
            if (!dbus_message_iter_open_container(&iter, type, signature, &sub)) {
                assert(false && "Failed to open container");
            }
            return {sub, true};
        }

        auto close(const Iterator& sub)
        {
            if (!dbus_message_iter_close_container(&iter, &sub.iter)) {
                assert(false && "Failed to close container");
            }
        }

        // Query

        DBusMessageIter unwrap_variants() const
        {
            DBusMessageIter concrete = iter;
            while (dbus_message_iter_get_arg_type(&concrete) == DBUS_TYPE_VARIANT) {
                DBusMessageIter sub;
                dbus_message_iter_recurse(&concrete, &sub);
                concrete = sub;
            }
            return concrete;
        }

        auto type() const
        {
            if (closed) return 0;

            DBusMessageIter concrete = unwrap_variants();
            return dbus_message_iter_get_arg_type(&concrete);
        }

        auto recurse() const -> Iterator
        {
            if (closed) return {};

            DBusMessageIter concrete = unwrap_variants();
            switch (dbus_message_iter_get_arg_type(&concrete)) {
                break;case DBUS_TYPE_ARRAY:
                      case DBUS_TYPE_STRUCT:
                      case DBUS_TYPE_DICT_ENTRY:
                    DBusMessageIter sub;
                    dbus_message_iter_recurse(&concrete, &sub);
                    return {sub};
                break;default:
                    return {concrete, true};
            }

            DBusMessageIter sub;
            dbus_message_iter_recurse(&concrete, &sub);
            return {sub};
        }

        explicit operator bool() const noexcept
        {
            return !closed && dbus_message_iter_get_arg_type(&iter);
        }

        // Type access

        auto get_string() const noexcept -> std::optional<std::string>
        {
            if (closed) return std::nullopt;

            DBusMessageIter concrete = unwrap_variants();
            switch (dbus_message_iter_get_arg_type(&concrete)) {
                break;case DBUS_TYPE_STRING:
                    case DBUS_TYPE_OBJECT_PATH:
                    case DBUS_TYPE_SIGNATURE:
                    const char* value;
                    dbus_message_iter_get_basic(&concrete, &value);
                    return value;
            }
            return std::nullopt;
        }

        template<typename T>
            requires std::is_arithmetic_v<T>
        auto get() const noexcept -> std::optional<T>
        {
            if (closed) return std::nullopt;

    #define TRAY_DBUS_GET_TYPE(Type) \
        { \
            Type value; \
            dbus_message_iter_get_basic(&concrete, &value); \
            return value; \
        }

            DBusMessageIter concrete = unwrap_variants();
            switch (dbus_message_iter_get_arg_type(&concrete)) {

                break;case DBUS_TYPE_BYTE:    TRAY_DBUS_GET_TYPE(uint8_t)
                break;case DBUS_TYPE_BOOLEAN: TRAY_DBUS_GET_TYPE(int)
                break;case DBUS_TYPE_INT16:   TRAY_DBUS_GET_TYPE(int16_t)
                break;case DBUS_TYPE_UINT16:  TRAY_DBUS_GET_TYPE(uint16_t)
                break;case DBUS_TYPE_INT32:   TRAY_DBUS_GET_TYPE(int32_t)
                break;case DBUS_TYPE_UINT32:  TRAY_DBUS_GET_TYPE(uint32_t)
                break;case DBUS_TYPE_INT64:   TRAY_DBUS_GET_TYPE(int64_t)
                break;case DBUS_TYPE_UINT64:  TRAY_DBUS_GET_TYPE(uint64_t)
                break;case DBUS_TYPE_DOUBLE:  TRAY_DBUS_GET_TYPE(double)
            }
            return std::nullopt;
        }

        // Random access

        auto operator[](size_t index) const noexcept -> Iterator
        {
            if (closed) return {};

            Iterator sub = recurse();
            for (size_t i = 0; i < index; ++i) {
                ++sub;
            }
            return sub;
        }

        // C++ iterator interface

        auto begin() const -> Iterator
        {
            if (closed) return {};

            return recurse();
        }

        auto end() const
        {
            return std::default_sentinel;
        }

        auto operator*() const noexcept
        {
            return *this;
        }

        bool operator==(std::default_sentinel_t) const noexcept
        {
            return !*this;
        }

        Iterator& operator++() noexcept
        {
            if (!closed) {
                closed = !dbus_message_iter_next(&iter);
            }
            return *this;
        }

        Iterator operator++(int) noexcept
        {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }
    };

    struct Property
    {
        std::string signature;
        std::function<void(DBusConnection*, Iterator&)> handler;
    };

    struct VTable
    {
        DBusConnection* connection;
        const char* path;

        std::unordered_map<std::string_view,
            std::unordered_map<std::string_view,
                std::function<DBusHandlerResult(DBusConnection*, DBusMessage*)>>> interfaces;

        std::unordered_map<std::string_view,
            std::unordered_map<std::string_view,
                Property>> properties;

        VTable(DBusConnection* conn, const char* path)
            : connection(conn)
            , path(path)
        {
            dbus_connection_register_object_path(conn, path, ptr_to(DBusObjectPathVTable {
                .message_function = [](DBusConnection* conn, DBusMessage* msg, void* data) -> DBusHandlerResult {
                    auto* vtable = static_cast<VTable*>(data);

                    auto interface = vtable->interfaces.find(dbus_message_get_interface(msg));
                    if (interface != vtable->interfaces.end()) {
                        auto member = interface->second.find(dbus_message_get_member(msg));
                        if (member != interface->second.end()) {
                            return member->second(conn, msg);
                        }
                    }

                    std::println("ERROR : Message unhandled");
                    std::println("  interface: {} ", dbus_message_get_interface(msg));
                    std::println("     member: {} ", dbus_message_get_member(msg));
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                }
            }), this);

            interfaces["org.freedesktop.DBus.Properties"]["Get"] =
                [this](DBusConnection* conn, DBusMessage* msg) -> DBusHandlerResult {
                    Iterator args(msg, iter::read);

                    auto interface_name = (args++).get_string().value_or("");
                    auto member_name    = (args++).get_string().value_or("");

                    auto interface = properties.find(interface_name);
                    if (interface != properties.end()) {
                        auto member = interface->second.find(member_name);
                        if (member != interface->second.end()) {
                            auto& property = member->second;
                            Message reply = dbus_message_new_method_return(msg);
                            Iterator out(reply.get(), iter::append);
                            auto var = out.open(DBUS_TYPE_VARIANT, property.signature.c_str());
                            property.handler(conn, var);
                            out.close(var);
                            dbus_connection_send(conn, reply.get(), nullptr);
                            return DBUS_HANDLER_RESULT_HANDLED;
                        }
                    }

                    std::println("ERROR : Property unhandled");
                    std::println("  interface: {} ", interface_name);
                    std::println("     member: {} ", member_name);
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                };
        }

        VTable(const VTable&) = delete;
        VTable(VTable&&) = delete;

        ~VTable()
        {
            dbus_connection_unregister_object_path(connection, path);
        }
    };
}
