#pragma once

#include <utility>
#include <print>
#include <chrono>

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

auto ptr_to(auto&& v) { return &v; }

template<typename Dur>
auto get_millis(Dur dur) -> double
{
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(dur).count();
}

template<typename Dur>
auto fmt_time(Dur dur) -> std::string
{
    return std::format("{:.2f}ms", std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(dur).count());
}
