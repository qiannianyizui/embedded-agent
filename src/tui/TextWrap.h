// TextWrap — display-width-aware line wrapping (no FTXUI flexbox)
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ea::tui {

// Greedy wrap of |text| to at most |max_width| terminal cells per line.
// - Explicit '\n' breaks are preserved.
// - CJK glyphs count as two cells (via ftxui::string_width).
// - Words are kept intact when they fit; overlong words are hard-broken.
// - Trailing spaces are trimmed from wrapped lines.
// A |max_width| <= 0 returns the text split on '\n' only (no wrapping).
std::vector<std::string> wrap_by_width(std::string_view text, int max_width);

}  // namespace ea::tui
