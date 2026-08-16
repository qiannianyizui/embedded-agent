// TextWrap — display-width-aware line wrapping implementation
#include "TextWrap.h"
#include <ftxui/screen/string.hpp>
#include <cctype>

namespace ea::tui {

namespace {

void push_line(std::vector<std::string>& lines, std::string& cur, int& cur_w) {
    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
    lines.push_back(std::move(cur));
    cur.clear();
    cur_w = 0;
}

// Width of a single UTF-8 glyph in terminal cells (0 for combining marks).
int glyph_width(const std::string& glyph) {
    return ftxui::string_width(glyph);
}

bool is_word_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/';
}

}  // anonymous namespace

std::vector<std::string> wrap_by_width(std::string_view text, int max_width) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    if (max_width <= 0) {
        size_t start = 0;
        while (start < text.size()) {
            size_t nl = text.find('\n', start);
            if (nl == std::string::npos) {
                lines.push_back(std::string(text.substr(start)));
                break;
            }
            lines.push_back(std::string(text.substr(start, nl - start)));
            start = nl + 1;
        }
        return lines;
    }

    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        size_t seg_end = (nl == std::string::npos) ? text.size() : nl;

        std::string cur;
        int cur_w = 0;
        std::string word;
        int word_w = 0;

        auto flush_word = [&] {
            if (word.empty()) return;
            if (!cur.empty() && cur_w + word_w > max_width) {
                push_line(lines, cur, cur_w);
            }
            if (word_w > max_width) {
                // Hard-break overlong words (URLs, hashes, paths, ...).
                for (char c : word) {
                    if (cur_w >= max_width) push_line(lines, cur, cur_w);
                    cur.push_back(c);
                    ++cur_w;
                }
            } else {
                cur += word;
                cur_w += word_w;
            }
            word.clear();
            word_w = 0;
        };

        size_t i = start;
        while (i < seg_end) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == ' ') {
                flush_word();
                if (cur_w < max_width) {
                    cur.push_back(' ');
                    ++cur_w;
                }
                ++i;
                continue;
            }
            if (c < 0x80) {
                if (is_word_char(c)) {
                    word.push_back(static_cast<char>(c));
                    ++word_w;
                } else {
                    // ASCII punctuation: breakable at any glyph.
                    flush_word();
                    if (cur_w >= max_width) push_line(lines, cur, cur_w);
                    cur.push_back(static_cast<char>(c));
                    ++cur_w;
                }
                ++i;
                continue;
            }
            // Multi-byte glyph (CJK, emoji, ...): breakable at any glyph.
            flush_word();
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            std::string glyph(text.substr(i, len));
            int w = glyph_width(glyph);
            if (cur_w > 0 && cur_w + w > max_width) {
                push_line(lines, cur, cur_w);
            }
            cur += glyph;
            cur_w += w;
            i += len;
        }
        flush_word();
        push_line(lines, cur, cur_w);

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

}  // namespace ea::tui
