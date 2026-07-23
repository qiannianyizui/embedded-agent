// Theme — default theme singleton
#include "Theme.h"

namespace ea::tui {

const Theme& default_theme() {
    static const Theme theme{};
    return theme;
}

}  // namespace ea::tui
