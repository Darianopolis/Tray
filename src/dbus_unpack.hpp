#pragma once

#include "dbus_value.hpp"

#include <systemd/sd-bus.h>

#include <stdexcept>
#include <string>

namespace dbus {

// ── Error type ────────────────────────────────────────────────────────────────

struct UnpackError : std::runtime_error {
    int  r;  // errno-style code returned by sd-bus
    explicit UnpackError(const char* ctx, int r)
        : std::runtime_error(std::string(ctx) + ": " + std::to_string(r)), r(r) {}
};

// ── Public entry point ────────────────────────────────────────────────────────

// Unpack the entire contents of an sd_bus_message into a Value tree.
//
// Per the D-Bus spec a message body may contain multiple top-level items, but
// sd-bus presents them as though they are inside a single implicit container.
// unpack_message therefore reads all top-level items and wraps them in an Array
//
// The message read cursor is advanced to the end; rewind with
// sd_bus_message_rewind() if you need to read it again.
Value unpack_message(sd_bus_message* msg);

// ── Implementation ────────────────────────────────────────────────────────────
// Everything below is in the header so the library stays header-only.
// Move to a .cpp if you prefer a compiled unit.

namespace detail {

// Forward declaration — mutually recursive with unpack_array_like.
Value unpack_value(sd_bus_message* msg);

// ── Basic / scalar types ──────────────────────────────────────────────────────

inline Value unpack_basic(sd_bus_message* msg, char type_char) {
    // sd_bus_message_read_basic accepts a void* target whose required type
    // depends on the type character.  We use the widest possible target for
    // each category and let the caller narrow if needed.
    switch (type_char) {
        // Boolean
        case SD_BUS_TYPE_BOOLEAN: {
            int v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read boolean", r);
            return Value::make_boolean(v != 0);
        }

        // Unsigned integers
        case SD_BUS_TYPE_BYTE: {
            uint8_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read byte", r);
            return Value::make_unsigned(v);
        }
        case SD_BUS_TYPE_UINT16: {
            uint16_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read uint16", r);
            return Value::make_unsigned(v);
        }
        case SD_BUS_TYPE_UINT32: {
            uint32_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read uint32", r);
            return Value::make_unsigned(v);
        }
        case SD_BUS_TYPE_UINT64: {
            uint64_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read uint64", r);
            return Value::make_unsigned(v);
        }

        // Signed integers
        case SD_BUS_TYPE_INT16: {
            int16_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read int16", r);
            return Value::make_signed(v);
        }
        case SD_BUS_TYPE_INT32: {
            int32_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read int32", r);
            return Value::make_signed(v);
        }
        case SD_BUS_TYPE_INT64: {
            int64_t v = 0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read int64", r);
            return Value::make_signed(v);
        }

        // Double
        case SD_BUS_TYPE_DOUBLE: {
            double v = 0.0;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read double", r);
            return Value::make_double(v);
        }

        // String-like types (STRING, OBJECT_PATH, SIGNATURE) all become String.
        case SD_BUS_TYPE_STRING:
        case SD_BUS_TYPE_OBJECT_PATH:
        case SD_BUS_TYPE_SIGNATURE: {
            const char* v = nullptr;
            int r = sd_bus_message_read_basic(msg, type_char, &v);
            if (r < 0) throw UnpackError("read string", r);
            // sd-bus owns the memory; copy into std::string immediately.
            return Value::make_string(v ? v : "");
        }

        default:
            throw UnpackError("unsupported basic type", -EINVAL);
    }
}

// ── Container entry/exit helpers ──────────────────────────────────────────────

// Returns the element type character of the container we just entered,
// or '\0' if the container is empty (peek returns 0).
inline char enter_container(sd_bus_message* msg, char container_char, const char* contents) {
    int r = sd_bus_message_enter_container(msg, container_char, contents);
    if (r < 0) throw UnpackError("enter container", r);
    return static_cast<char>(sd_bus_message_peek_type(msg, nullptr, nullptr));
}

inline void exit_container(sd_bus_message* msg) {
    int r = sd_bus_message_exit_container(msg);
    if (r < 0) throw UnpackError("exit container", r);
}

// ── Dict entry unpacking ──────────────────────────────────────────────────────

// Reads a single DICT_ENTRY (already entered into the array by the caller)
// and returns a {key, value} pair.
inline std::pair<Value, Value> unpack_dict_entry(sd_bus_message* msg) {
    // Enter the dict entry container.
    int r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, nullptr);
    if (r < 0) throw UnpackError("enter dict_entry", r);

    Value key = unpack_value(msg);
    Value val = unpack_value(msg);

    exit_container(msg);
    return {std::move(key), std::move(val)};
}

// ── Array-like containers ─────────────────────────────────────────────────────

// Reads the current array/struct container (already opened by the caller) into
// either a Dict or an Array depending on whether its element type is DICT_ENTRY.
// Returns the resulting Value and leaves the cursor at the closing bracket.
inline Value unpack_open_container(sd_bus_message* msg, bool is_dict_array) {
    if (is_dict_array) {
        Dict dict;
        while (true) {
            char peek = static_cast<char>(sd_bus_message_peek_type(msg, nullptr, nullptr));
            if (peek == 0) break;  // end of container
            auto[key, value] = unpack_dict_entry(msg);
            dict.keys.emplace_back(std::move(key));
            dict.values.emplace_back(std::move(value));
        }
        return Value(std::move(dict));
    } else {
        Array arr;
        while (true) {
            char peek = static_cast<char>(sd_bus_message_peek_type(msg, nullptr, nullptr));
            if (peek == 0) break;  // end of container
            arr.elements.push_back(unpack_value(msg));
        }
        return Value(std::move(arr));
    }
}

// ── Top-level per-value dispatcher ───────────────────────────────────────────

inline Value unpack_value(sd_bus_message* msg) {
    char        type_char = 0;
    const char* contents  = nullptr;
    int peek = sd_bus_message_peek_type(msg, &type_char, &contents);
    if (peek < 0) throw UnpackError("peek type", peek);
    if (peek == 0) throw UnpackError("unexpected end of message", -EBADMSG);

    switch (type_char) {

        // ── Variant: recurse transparently ───────────────────────────────────
        case SD_BUS_TYPE_VARIANT: {
            int r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_VARIANT, contents);
            if (r < 0) throw UnpackError("enter variant", r);
            Value inner = unpack_value(msg);
            exit_container(msg);
            return inner;  // the variant "collapses" — no Variant node in the tree
        }

        // ── Array: may become Dict if its element type is DICT_ENTRY ─────────
        case SD_BUS_TYPE_ARRAY: {
            // contents is the element type signature, e.g. "{sv}" or "s" etc.
            bool is_dict = (contents && contents[0] == SD_BUS_TYPE_DICT_ENTRY_BEGIN);

            int r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, contents);
            if (r < 0) throw UnpackError("enter array", r);

            Value result = unpack_open_container(msg, is_dict);
            exit_container(msg);
            return result;
        }

        // ── Struct: always becomes an Array ──────────────────────────────────
        case SD_BUS_TYPE_STRUCT: {
            int r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, contents);
            if (r < 0) throw UnpackError("enter struct", r);

            Value result = unpack_open_container(msg, false);
            exit_container(msg);
            return result;
        }

        // ── Basic scalars ─────────────────────────────────────────────────────
        default:
            return unpack_basic(msg, type_char);
    }
}

} // namespace detail

// ── Public definition ─────────────────────────────────────────────────────────

inline Value unpack_message(sd_bus_message* msg) {
    if (!msg) throw UnpackError("null message", -EINVAL);

    Array root;
    while (true) {
        char peek = static_cast<char>(sd_bus_message_peek_type(msg, nullptr, nullptr));
        if (peek == 0) break;
        root.elements.push_back(detail::unpack_value(msg));
    }

    return Value(std::move(root));
}

} // namespace dbus
