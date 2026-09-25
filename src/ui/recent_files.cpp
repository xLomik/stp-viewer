#include "recent_files.h"

#include <algorithm>
#include <cwctype>

namespace stp {
namespace ui {
namespace {
wchar_t fold(wchar_t c) { return c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c)); }
}  // namespace

bool samePath(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

void RecentFiles::add(const std::wstring& path) {
    if (path.empty()) return;
    remove(path);
    m_items.insert(m_items.begin(), path);
    if (m_items.size() > kLimit) m_items.resize(kLimit);
}

bool RecentFiles::remove(const std::wstring& path) {
    const auto it = std::find_if(m_items.begin(), m_items.end(),
                                 [&](const std::wstring& item) { return samePath(item, path); });
    if (it == m_items.end()) return false;
    m_items.erase(it);
    return true;
}

void RecentFiles::setItems(const std::vector<std::wstring>& items) {
    m_items.clear();
    for (const std::wstring& item : items) {
        if (item.empty() || m_items.size() >= kLimit) continue;
        const bool repeated = std::any_of(m_items.begin(), m_items.end(),
                                          [&](const std::wstring& seen) { return samePath(seen, item); });
        if (!repeated) m_items.push_back(item);
    }
}

}  // namespace ui
}  // namespace stp
