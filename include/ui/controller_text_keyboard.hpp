#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace wowee::ui {
// Shared draft semantics for the in-game pad keyboard. Cancel never edits the
// caller's field; only an explicit Done/Send commits. No platform IME dependency.
struct ControllerTextKeyboard {
    enum class Result { Editing, Accepted, Cancelled };
    std::string draft;
    size_t limit = 255;
    int selected = 0;
    bool upper = false, symbols = false, numeric = false;
    void begin(std::string text, int maxLength, bool number) {
        limit = size_t(std::clamp(maxLength, 1, 255)); numeric = number;
        selected = 0; upper = false; symbols = number;
        // Preserve complete UTF-8 characters in an existing field.
        if (text.size() > limit) {
            size_t end = limit;
            while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
            text.resize(end);
        }
        draft = std::move(text);
    }
    void move(int x, int y) { selected = ((selected/10+y+3)%3)*10+(selected%10+x+10)%10; }
    char character(int key) const {
        if (key < 0 || key >= 26) return 0;
        constexpr std::string_view punctuation = "0123456789 _-'.,:;!?/@+#=()";
        if (numeric) return key < 10 ? char('0'+key) : 0;
        if (symbols) return key < int(punctuation.size()) ? punctuation[size_t(key)] : 0;
        return char((upper ? 'A' : 'a') + key);
    }
    void append(char c) {
        if (draft.size() >= limit || c < 32 || c > 126 || (numeric && (c < '0' || c > '9'))) return;
        draft.push_back(c);
    }
    void backspace() {
        if (draft.empty()) return;
        size_t end = draft.size()-1;
        while (end && (static_cast<unsigned char>(draft[end]) & 0xc0) == 0x80) --end;
        draft.resize(end);
    }
    Result choose(int key) {
        if (key >= 0 && key < 26) { if (const char c = character(key)) append(c); }
        else if (key == 26) backspace();
        else if (key == 27) { if (!numeric) upper = !upper; }
        else if (key == 28) return Result::Accepted;
        else if (key == 29) return Result::Cancelled;
        return Result::Editing;
    }
    void nextPage() { if (!numeric) symbols = !symbols; }
};
}
