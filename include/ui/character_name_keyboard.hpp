#pragma once
#include <string>
#include <cctype>
namespace wowee::ui {
// Name keyboard owns its draft. A letter selection never completes the edit.
struct CharacterNameKeyboard {
    enum class Result { Editing, Accepted, Cancelled };
    bool open = false;
    bool justOpened = false;
    int selected = 0;
    bool upper = false;
    std::string draft;
    void begin(const std::string& name) { draft=name; selected=0; upper=false; open=true; justOpened=true; }
    void move(int x, int y) { selected=((selected/10+y+3)%3)*10+(selected%10+x+10)%10; }
    void letter(char c) {
        if (draft.size() >= 12 || !((c>='A'&&c<='Z')||(c>='a'&&c<='z'))) return;
        draft.push_back((upper || draft.empty()) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    Result choose(int key) {
        if (!open) return Result::Editing;
        if (key>=0 && key<26) letter(static_cast<char>('a'+key));
        else if (key==26 && !draft.empty()) draft.pop_back();
        else if (key==27) upper=!upper;
        else if (key==28 && draft.size()>=2) { open=false; return Result::Accepted; }
        else if (key==29) { open=false; return Result::Cancelled; }
        return Result::Editing;
    }
};
}
