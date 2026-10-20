#pragma once
#include <functional>
#include <string>
void RunInteropTests(const std::string& base,
    const std::function<void(const char*, std::function<void()>)>& test);
