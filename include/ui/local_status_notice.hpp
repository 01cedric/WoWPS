#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace wowee::ui {
struct LocalStatusNotice {
    uint64_t revision = 0;
    std::string text;
    double expires = 0;
    void observe(uint64_t event, const std::string& message, double now) {
        if (event == revision && message == text) return;
        revision = event; text = message;
        expires = message.empty() ? 0 : now + 3.0;
    }
    float alpha(double now) const {
        return text.empty() ? 0.f : static_cast<float>(std::clamp((expires-now)/.6,0.0,1.0));
    }
};
}
