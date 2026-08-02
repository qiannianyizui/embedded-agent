// Banner — startup welcome card shown as the first system message
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

// Render the welcome card as an Element (for embedding in ChatArea).
ftxui::Element render_banner(const BannerInfo& info, int width);

// Create a Banner component that renders the welcome card.
ftxui::Component make_banner(const BannerInfo& info);

}  // namespace ea::tui
