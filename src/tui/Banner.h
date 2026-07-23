// Banner — startup brand display shown as the first system message
#pragma once
#include <ftxui/component/component.hpp>
#include <string>

namespace ea::tui {

struct BannerInfo {
    std::string model;
    std::string cwd;
    std::string session_id;
    int tool_count = 0;
};

// Create a Banner component that renders the brand identity.
// Wide terminal (≥95 cols): multi-line ASCII art with gradient
// Medium: compact rule ─── Embedded Agent ───
// Narrow: just the name
ftxui::Component make_banner(const BannerInfo& info);

// Render the banner as an Element (for embedding in ChatArea)
ftxui::Element render_banner(const BannerInfo& info, int width);

}  // namespace ea::tui
