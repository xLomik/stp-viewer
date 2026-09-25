#include "ribbon_layout.h"

namespace stp {
namespace ui {

int ribbonWidth(const std::vector<RibbonGroupSpec>& groups, const std::vector<GroupSize>& sizes, int gap) {
    int total = 0;
    for (std::size_t i = 0; i < groups.size() && i < sizes.size(); ++i) {
        if (i > 0) total += gap;
        switch (sizes[i]) {
            case GroupSize::Large: total += groups[i].large; break;
            case GroupSize::Small: total += groups[i].small; break;
            case GroupSize::Collapsed: total += groups[i].collapsed; break;
        }
    }
    return total;
}

std::vector<GroupSize> layoutRibbon(const std::vector<RibbonGroupSpec>& groups, int available, int gap,
                                    bool narrow) {
    std::vector<GroupSize> sizes(groups.size(), narrow ? GroupSize::Small : GroupSize::Large);
    for (const GroupSize target : {GroupSize::Small, GroupSize::Collapsed}) {
        for (std::size_t k = groups.size(); k-- > 0;) {
            if (ribbonWidth(groups, sizes, gap) <= available) return sizes;
            if (static_cast<int>(sizes[k]) < static_cast<int>(target)) sizes[k] = target;
        }
    }
    return sizes;
}

}  // namespace ui
}  // namespace stp
