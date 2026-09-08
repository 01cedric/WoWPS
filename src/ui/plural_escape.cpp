#include "ui/plural_escape.hpp"

#include <cctype>

namespace wowee {
namespace ui {

std::string resolvePluralEscapes(const std::string& in) {
    if (in.find("|4") == std::string::npos) return in;
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in.compare(i, 2, "|4") != 0) { out.push_back(in[i++]); continue; }
        const size_t colon = in.find(':', i + 2);
        const size_t semi = in.find(';', i + 2);
        // Not the escape after all - a literal "|4" with no body. Left alone
        // rather than eaten, so a malformed string still shows what it says.
        if (colon == std::string::npos || semi == std::string::npos || colon > semi) {
            out.push_back(in[i++]);
            continue;
        }
        const std::string one = in.substr(i + 2, colon - (i + 2));
        const std::string many = in.substr(colon + 1, semi - (colon + 1));
        // The last run of digits anywhere before the escape, not merely the
        // one touching it. The strings put whole words in between -
        // "%d more daily |4quest:quests;", "<%d Combo |4Point:Points;",
        // "have %s friend |4request:requests;" - so a scan that skipped only
        // spaces found no number in any of those and always answered plural,
        // including for one.
        size_t end = std::string::npos;
        for (size_t k = out.size(); k > 0; --k) {
            if (std::isdigit(static_cast<unsigned char>(out[k - 1]))) { end = k; break; }
        }
        bool singular = false;
        if (end != std::string::npos) {
            size_t start = end;
            while (start > 0 && std::isdigit(static_cast<unsigned char>(out[start - 1]))) --start;
            // Exactly "1". Not 21, and not 01 - the latter is not a form these
            // strings produce, and reading it as one would be a guess.
            singular = (end - start == 1 && out[start] == '1');
        }
        out += singular ? one : many;
        i = semi + 1;
    }
    return out;
}

}  // namespace ui
}  // namespace wowee
