#pragma once

#include <utility>
#include <print>

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

auto ptr_to(auto&& v) { return &v; }
