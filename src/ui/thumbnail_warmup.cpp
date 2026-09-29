#include "thumbnail_warmup.h"

#include <algorithm>
#include <cwctype>

namespace stp {
namespace ui {

const std::vector<std::wstring>& cadExtensions() {
    static const std::vector<std::wstring> list = {L".stp", L".step", L".stpz", L".igs", L".iges", L".dxf",
                                                   L".stl", L".obj", L".ply", L".dwg", L".prt", L".sldprt",
                                                   L".sldasm", L".ipt", L".iam", L".catpart", L".catproduct"};
    return list;
}

bool isCadFile(const std::wstring& name) {
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return false;
    std::wstring ext = name.substr(dot);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    const auto& list = cadExtensions();
    return std::find(list.begin(), list.end(), ext) != list.end();
}

std::vector<std::wstring> warmupOrder(std::vector<WarmupFile> files, std::size_t limit) {
    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const WarmupFile& f) { return f.folder || f.size == 0 || !isCadFile(f.name); }),
                files.end());
    std::stable_sort(files.begin(), files.end(),
                     [](const WarmupFile& a, const WarmupFile& b) { return a.modified > b.modified; });
    std::vector<std::wstring> names;
    for (const WarmupFile& f : files) {
        if (names.size() >= limit) break;
        names.push_back(f.name);
    }
    return names;
}

}  // namespace ui
}  // namespace stp
