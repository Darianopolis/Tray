#pragma once

#include <type_traits>
#include <meta>
#include <format>
#include <string_view>
#include <optional>
#include <span>
#include <ranges>

#if __cpp_lib_reflection
template<typename E>
    requires (std::meta::is_enumerable_type(^^E))
constexpr
auto enum_values() -> std::span<const E>
{
    return std::define_static_array(std::meta::enumerators_of(^^E)
        | std::views::transform([](std::meta::info e) {
            return std::meta::extract<E>(e);
        }));
}

template<typename E>
    requires (std::meta::is_enumerable_type(^^E))
constexpr
auto enum_index(E value) -> std::optional<size_t>
{
    size_t i = 0;
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
        if (value == [:e:]) return i;
        i++;
    }
    return std::nullopt;
}

template<typename E, bool Enumerable = std::meta::is_enumerable_type(^^E)>
    requires std::is_enum_v<E>
constexpr
auto enum_name(E value) -> std::string_view
{
    if constexpr (Enumerable) {
        template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (value == [:e:]) return std::meta::identifier_of(e);
        }
    }
  return "";
}
#else
template<typename E>
constexpr
auto enum_values() -> std::span<const E>
{
    return {};
}

template<typename E>
constexpr
auto enum_index(E e) -> std::optional<size_t>
{
    return std::nullopt;
}

template<typename E>
constexpr
auto enum_name(E e) -> std::string_view
{
    return "";
}
#endif

template<typename E>
    requires std::is_enum_v<E>
struct std::formatter<E> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(E v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{}", enum_name(v));
    }
};
