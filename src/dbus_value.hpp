#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <print>

#include "enums.hpp"

namespace dbus
{
    struct Value;

    struct Number
    {
        enum class Kind { Signed, Unsigned, Double };

        Kind kind;
        union {
            int64_t  i;
            uint64_t u;
            double   d;
        };

        static Number from_signed  (int64_t  v) noexcept { Number n; n.kind = Kind::Signed;   n.i = v; return n; }
        static Number from_unsigned(uint64_t v) noexcept { Number n; n.kind = Kind::Unsigned; n.u = v; return n; }
        static Number from_double  (double   v) noexcept { Number n; n.kind = Kind::Double;   n.d = v; return n; }

        int64_t  as_signed()   const { return (kind == Kind::Signed)   ? i : (kind == Kind::Unsigned) ? static_cast<int64_t>(u)  : static_cast<int64_t>(d);  }
        uint64_t as_unsigned() const { return (kind == Kind::Unsigned) ? u : (kind == Kind::Signed)   ? static_cast<uint64_t>(i) : static_cast<uint64_t>(d); }
        double   as_real()     const { return (kind == Kind::Double)   ? d : (kind == Kind::Signed)   ? static_cast<double>(i)   : static_cast<double>(u);   }

        bool operator==(const Number& other) const
        {
            if (kind != other.kind) return false;
            switch (kind) {
                break;case Kind::Signed:   return as_signed()   == other.as_signed();
                break;case Kind::Unsigned: return as_unsigned() == other.as_unsigned();
                break;case Kind::Double:   return as_real()     == other.as_real();
            }
        }
    };

    struct Boolean
    {
        bool value;

        bool operator==(const Boolean&) const = default;
    };

    struct String
    {
        std::string value;

        bool operator==(const String&) const = default;
    };

    // ── Container types ───────────────────────────────────────────────────────────

    // Ordered sequence — represents ARRAY (non-dict), STRUCT, and variants whose
    // contents are themselves array-like.
    struct Array
    {
        std::vector<Value> elements;

        bool operator==(const Array&) const = default;
    };

    struct Dict
    {
        std::vector<Value> keys;
        std::vector<Value> values;

        bool operator==(const Dict&) const = default;
    };

    struct Error {
        std::string error;

        bool operator==(const Error&) const = default;
    };

    // ── Tagged union ─────────────────────────────────────────────────────────────

    enum class Type { Array, Dict, String, Number, Boolean, Error };

    struct Value {
        using Storage = std::variant<Array, Dict, String, Number, Boolean, Error>;
        Storage storage;

        // ── Constructors ──────────────────────────────────────────────────────────
        Value() = delete;

        /*implicit*/ Value(Array   v) : storage(std::move(v)) {}
        /*implicit*/ Value(Dict    v) : storage(std::move(v)) {}
        /*implicit*/ Value(String  v) : storage(std::move(v)) {}
        /*implicit*/ Value(Number  v) : storage(std::move(v)) {}
        /*implicit*/ Value(Boolean v) : storage(std::move(v)) {}
        /*implicit*/ Value(Error   v) : storage(std::move(v)) {}

        bool operator==(const Value&) const = default;

        // Convenience factories for common scalar types.
        static Value make_string (std::string  s) { return Value(String{std::move(s)});     }
        static Value make_boolean(bool         b) { return Value(Boolean{b});               }
        static Value make_signed (int64_t      i) { return Value(Number::from_signed(i));   }
        static Value make_unsigned(uint64_t    u) { return Value(Number::from_unsigned(u)); }
        static Value make_double (double       d) { return Value(Number::from_double(d));   }
        static Value make_error  (std::string  e) { return Value(Error{std::move(e)});      }

        // // ── Type query ────────────────────────────────────────────────────────────
        Type type() const noexcept {
            return std::visit([](auto const& v) -> Type {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Array>)   return Type::Array;
                if constexpr (std::is_same_v<T, Dict>)    return Type::Dict;
                if constexpr (std::is_same_v<T, String>)  return Type::String;
                if constexpr (std::is_same_v<T, Number>)  return Type::Number;
                if constexpr (std::is_same_v<T, Boolean>) return Type::Boolean;
                if constexpr (std::is_same_v<T, Error>)   return Type::Error;
            }, storage);
        }

        bool is_array()   const noexcept { return type() == Type::Array;   }
        bool is_dict()    const noexcept { return type() == Type::Dict;    }
        bool is_string()  const noexcept { return type() == Type::String;  }
        bool is_number()  const noexcept { return type() == Type::Number;  }
        bool is_boolean() const noexcept { return type() == Type::Boolean; }
        bool is_error()   const noexcept { return type() == Type::Error;   }

        // ── Checked accessors ─────────────────────────────────────────────────────
        const Array&   as_array()   const { return std::get<Array>(storage);   }
        const Dict&    as_dict()    const { return std::get<Dict>(storage);    }
        const String&  as_string()  const { return std::get<String>(storage);  }
        const Number&  as_number()  const { return std::get<Number>(storage);  }
        const Boolean& as_boolean() const { return std::get<Boolean>(storage); }

        Array&   as_array()   { return std::get<Array>(storage);   }
        Dict&    as_dict()    { return std::get<Dict>(storage);    }
        String&  as_string()  { return std::get<String>(storage);  }
        Number&  as_number()  { return std::get<Number>(storage);  }
        Boolean& as_boolean() { return std::get<Boolean>(storage); }
    };

    inline
    void dump_string(const Value& value, int depth = 0)
    {
        auto indent = [&](int delta = 0) {
            return std::string(depth + delta, ' ') + std::string(depth + delta, ' ');
        };

        switch (value.type()) {
            break;case Type::Array:
                std::println("{}Array", indent());
                for (auto& element : value.as_array().elements) {
                    dump_string(element, depth + 1);
                }
                // std::println("{}", indent());
            break;case Type::Dict:
                std::println("{}Dict", indent());
                for (int i = 0; i < value.as_dict().values.size(); ++i) {
                    dump_string(value.as_dict().keys[i], depth + 1);
                    dump_string(value.as_dict().values[i], depth + 1);
                    // std::println("{},", indent(1));
                }
                // std::println("{}}}", indent());
            break;case Type::Number:
                std::println("{}{}", indent(), value.as_number().as_real());
            break;case Type::String:
                std::println("{}\"{}\"", indent(), value.as_string().value);
            break;case Type::Boolean:
                std::println("{}{}", indent(), value.as_boolean().value);
            break;case Type::Error:
                std::println("{}Error({})", indent(), std::get<Error>(value.storage).error);
        }
    }

    struct ValueIterator
    {
        const Value* value = nullptr;

        ValueIterator() = default;
        ValueIterator(const Value& value): value(&value) {}

        explicit operator bool() const
        {
            return value && !value->is_error();
        }

        ValueIterator key(size_t i) const
        {
            if (value && value->is_dict()) {
                auto& dict = value->as_dict();
                if (i < dict.keys.size()) {
                    return dict.keys[i];
                }
            }

            return {};
        }

        ValueIterator operator[](std::string_view key) const
        {
            if (value && value->is_dict()) {
                auto& dict = value->as_dict();
                for (int i = 0; i < dict.keys.size(); ++i) {
                    if (dict.keys[i].is_string() && dict.keys[i].as_string().value == key) {
                        return dict.values[i];
                    }
                }
            }

            return {};
        }

        ValueIterator operator[](const Value& key) const
        {
            if (value && value->is_dict()) {
                auto& dict = value->as_dict();
                for (int i = 0; i < dict.keys.size(); ++i) {
                    // if (dict.keys[i].is_string() && dict.keys[i].as_string().value == key) {
                    if (dict.keys[i] == key) {
                        return dict.values[i];
                    }
                }
            }

            return {};
        }

        ValueIterator operator[](size_t i) const
        {
            if (!value) return {};

            switch (value->type()) {
                break;case Type::Array: {
                    auto& array = value->as_array();
                    if (i < array.elements.size()) return array.elements[i];
                }
                break;case Type::Dict: {
                    auto& dict = value->as_dict();
                    if (i < dict.values.size()) return dict.values[i];
                }
                break;default:
                    ;
            }

            return {};
        }

        std::optional<std::string> as_string() const
        {
            if (value && value->is_string()) return value->as_string().value;
            return std::nullopt;
        }

        std::optional<uint64_t> as_unsigned() const
        {
            if (value && value->is_number()) return value->as_number().as_unsigned();
            return std::nullopt;
        }

        std::optional<int64_t> as_signed() const
        {
            if (value && value->is_number()) return value->as_number().as_signed();
            return std::nullopt;
        }

        std::optional<double> as_real() const
        {
            if (value && value->is_number()) return value->as_number().as_real();
            return std::nullopt;
        }

        std::optional<bool> as_boolean() const
        {
            if (value && value->is_boolean()) return value->as_boolean().value;
            return std::nullopt;
        }
    };
}
