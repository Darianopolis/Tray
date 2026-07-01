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

// -----------------------------------------------------------------------------
//      Debug
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
//      Containers
// -----------------------------------------------------------------------------

    struct Error
    {
        DBusError data;

        Error()
        {
            dbus_error_init(&data);
        }

        ~Error()
        {
            dbus_error_free(&data);
        }

        Error(const Error&) = delete;

        explicit operator bool() const noexcept
        {
            return dbus_error_is_set(&data);
        }

        auto name()
        {
            return data.name;
        }

        auto message()
        {
            return data.message;
        }
    };

    namespace detail
    {
        struct Adopt {};
        struct Reference {};
    }

    constexpr detail::Adopt     adopt;
    constexpr detail::Reference reference;

    template<typename T, auto ref, auto unref>
    struct Ref
    {
        T* value;

        void reset(T* _message = nullptr)
        {
            if (value) {
                unref(value);
            }
            value = _message;
            if (_message) {
                ref(_message);
            }
        }

        Ref()
            : value(nullptr)
        {}

        Ref(T* _message, detail::Reference)
            : value(_message)
        {
            if (value) {
                ref(value);
            }
        }

        Ref(T* _message, detail::Adopt)
            : value(_message)
        {
        }

        Ref(const Ref& other)
            : value(other.value)
        {
            if (value) {
                ref(value);
            }
        }

        Ref& operator=(const Ref& other)
        {
            if (value != other.value) {
                reset(other.value);
            }
            return *this;
        }

        auto get() const -> T*
        {
            return value;
        }

        ~Ref()
        {
            if (value) {
                unref(value);
            }
        }

        explicit operator bool() const noexcept
        {
            return value;
        }
    };

    using Message     = Ref<DBusMessage,     dbus_message_ref,      dbus_message_unref>;
    using Connection  = Ref<DBusConnection,  dbus_connection_ref,   dbus_connection_unref>;
    using PendingCall = Ref<DBusPendingCall, dbus_pending_call_ref, dbus_pending_call_unref>;

// -----------------------------------------------------------------------------
//      Append Iterator
// -----------------------------------------------------------------------------

    struct AppendIterator
    {
        mutable DBusMessageIter iter;

        // Init

private:
        AppendIterator(const DBusMessageIter& _iter)
            : iter(_iter)
        {}

public:
        AppendIterator()
        {
            dbus_message_iter_init_closed(&iter);
        }

        AppendIterator(DBusMessage* message)
        {
            dbus_message_iter_init_append(message, &iter);
        }

        // Append

        template<typename T>
        void append(int type, const T& value)
        {

#define APPEND_BASIC_TYPE(Type) \
    { \
        Type storage = value; \
        if (!dbus_message_iter_append_basic(&iter, type, &storage)) { \
            assert(false && "DBUS ASSERT : Failed to append type");\
        } \
    }

            if constexpr (std::is_arithmetic_v<T>) {
                switch (type) {
                    break;case DBUS_TYPE_BYTE:    APPEND_BASIC_TYPE(uint8_t)
                    break;case DBUS_TYPE_BOOLEAN: APPEND_BASIC_TYPE(int)
                    break;case DBUS_TYPE_INT16:   APPEND_BASIC_TYPE(int16_t)
                    break;case DBUS_TYPE_UINT16:  APPEND_BASIC_TYPE(uint16_t)
                    break;case DBUS_TYPE_INT32:   APPEND_BASIC_TYPE(int32_t)
                    break;case DBUS_TYPE_UINT32:  APPEND_BASIC_TYPE(uint32_t)
                    break;case DBUS_TYPE_INT64:   APPEND_BASIC_TYPE(int64_t)
                    break;case DBUS_TYPE_UINT64:  APPEND_BASIC_TYPE(uint64_t)
                    break;case DBUS_TYPE_DOUBLE:  APPEND_BASIC_TYPE(double)
                    break;default:
                        assert(false && "Expected arithmetic D-Bus type");
                }
            } else if constexpr (std::is_convertible_v<T, const char*>) {
                const char* string = value;
                switch (type) {
                    break;case DBUS_TYPE_STRING:      APPEND_BASIC_TYPE(const char*)
                    break;case DBUS_TYPE_OBJECT_PATH: APPEND_BASIC_TYPE(const char*)
                    break;case DBUS_TYPE_SIGNATURE:   APPEND_BASIC_TYPE(const char*)
                    break;default:
                        assert(false && "Expected string D-Bus type");
                }
            } else {
                assert(false && "Unsupported T for append");
            }
        }

        auto open(int type, const char* signature) -> AppendIterator
        {
            DBusMessageIter sub;
            if (!dbus_message_iter_open_container(&iter, type, signature, &sub)) {
                assert(false && "Failed to open container");
            }
            return {sub};
        }

        void close(const AppendIterator& sub)
        {
            if (!dbus_message_iter_close_container(&iter, &sub.iter)) {
                assert(false && "Failed to close container");
            }
        }
    };

// -----------------------------------------------------------------------------
//      Read Iterator
// -----------------------------------------------------------------------------

    struct Iterator
    {
        mutable DBusMessageIter iter;
        bool closed = false;

        // Init

private:
        Iterator(const DBusMessageIter& _iter, bool _closed = false)
            : iter(_iter)
            , closed(_closed)
        {}

public:
        Iterator()
            : closed(true)
        {
            dbus_message_iter_init_closed(&iter);
        }

        Iterator(DBusMessage* message)
        {
            if (message) {
                dbus_message_iter_init(message, &iter);
            } else {
                dbus_message_iter_init_closed(&iter);
                closed = true;
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

#define GET_BASIC_TYPE(Type) \
    { \
        Type value; \
        dbus_message_iter_get_basic(&concrete, &value); \
        return value; \
    }

            DBusMessageIter concrete = unwrap_variants();
            switch (dbus_message_iter_get_arg_type(&concrete)) {
                break;case DBUS_TYPE_BYTE:    GET_BASIC_TYPE(uint8_t)
                break;case DBUS_TYPE_BOOLEAN: GET_BASIC_TYPE(int)
                break;case DBUS_TYPE_INT16:   GET_BASIC_TYPE(int16_t)
                break;case DBUS_TYPE_UINT16:  GET_BASIC_TYPE(uint16_t)
                break;case DBUS_TYPE_INT32:   GET_BASIC_TYPE(int32_t)
                break;case DBUS_TYPE_UINT32:  GET_BASIC_TYPE(uint32_t)
                break;case DBUS_TYPE_INT64:   GET_BASIC_TYPE(int64_t)
                break;case DBUS_TYPE_UINT64:  GET_BASIC_TYPE(uint64_t)
                break;case DBUS_TYPE_DOUBLE:  GET_BASIC_TYPE(double)
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

// -----------------------------------------------------------------------------
//      Helpers
// -----------------------------------------------------------------------------

    inline
    auto connect(DBusBusType type) -> Connection
    {
        Error err;
        auto conn = dbus_bus_get(type, &err.data);
        if (err) std::println("DBUS ERROR : {} - {}", err.name(), err.message());
        return {conn, adopt};
    }

    inline
    auto request_name(DBusConnection* conn, const char* name, unsigned int flags) -> int
    {
        Error err;
        return dbus_bus_request_name(conn, name, flags, &err.data);
    }

    inline
    auto send_with_reply_future(DBusConnection* conn, DBusMessage* msg) -> PendingCall
    {
        DBusPendingCall* pending = nullptr;
        dbus_connection_send_with_reply(conn, msg, &pending, -1);
        return {pending, adopt};
    }

    inline
    auto new_method_call(const char* bus_name, const char* path, const char* interface, const char* method) -> Message
    {
        return {dbus_message_new_method_call(bus_name, path, interface, method), adopt};
    }

    inline
    auto new_method_return(DBusMessage* msg) -> Message
    {
        return {dbus_message_new_method_return(msg), adopt};
    }

    inline
    auto send_with_reply(DBusConnection* conn, DBusMessage* msg) -> Message
    {
        Error err;
        Message result{dbus_connection_send_with_reply_and_block(conn, msg, -1, &err.data), adopt};
        if (err) std::println("DBUS ERROR : {} - {}", err.name(), err.message());
        return result;
    }

    inline
    auto send(DBusConnection* conn, DBusMessage* msg) -> bool
    {
        return dbus_connection_send(conn, msg, nullptr);
    }

// -----------------------------------------------------------------------------
//      VTable
// -----------------------------------------------------------------------------

    struct Property
    {
        std::string signature;
        std::function<void(DBusConnection*, AppendIterator&)> handler;
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

        VTable(DBusConnection* _connection, const char* _path)
            : connection(_connection)
            , path(_path)
        {
            dbus_connection_register_object_path(_connection, _path, ptr_to(DBusObjectPathVTable {
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
                    Iterator args(msg);

                    auto interface_name = (args++).get_string().value_or("");
                    auto member_name    = (args++).get_string().value_or("");

                    auto interface = properties.find(interface_name);
                    if (interface != properties.end()) {
                        auto member = interface->second.find(member_name);
                        if (member != interface->second.end()) {
                            auto& property = member->second;
                            Message reply = dbus::new_method_return(msg);
                            AppendIterator out(reply.get());
                            auto var = out.open(DBUS_TYPE_VARIANT, property.signature.c_str());
                            property.handler(conn, var);
                            out.close(var);
                            send(conn, reply.get());
                            return DBUS_HANDLER_RESULT_HANDLED;
                        }
                    }

                    std::println("ERROR : Properties.Get({}, {}) unhandled", interface_name, member_name);
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                };

            interfaces["org.freedesktop.DBus.Properties"]["GetAll"] =
                [this](DBusConnection* conn, DBusMessage* msg) -> DBusHandlerResult {

                    auto interface_name = Iterator(msg).get_string().value_or("");

                    auto interface = properties.find(interface_name);
                    if (interface != properties.end()) {
                        Message reply = dbus::new_method_return(msg);
                        AppendIterator out(reply.get());
                        auto arr = out.open(DBUS_TYPE_ARRAY, "{sv}");
                        for (auto[name, property] : interface->second) {
                            auto entry = arr.open(DBUS_TYPE_DICT_ENTRY, nullptr);
                            entry.append(DBUS_TYPE_STRING, std::string(name).c_str());
                            {
                                auto var = entry.open(DBUS_TYPE_VARIANT, property.signature.c_str());
                                property.handler(conn, var);
                                entry.close(var);
                            }
                            arr.close(entry);
                        }
                        out.close(arr);
                        send(conn, reply.get());
                        return DBUS_HANDLER_RESULT_HANDLED;
                    }

                    std::println("ERROR : Properties.GetAll({}) unhandled", interface_name);
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
