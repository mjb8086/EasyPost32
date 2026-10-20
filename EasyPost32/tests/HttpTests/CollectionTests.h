#pragma once
#include <functional>
#include <string>
void RunCollectionTests(const std::string& base,
    const std::function<void(const char*, std::function<void()>)>& test);
