#include "settings.h"

#include <algorithm>

namespace stp {
namespace {

constexpr wchar_t kKey[] = L"Software\\stp-viewer";

bool readDword(HKEY key, const wchar_t* name, DWORD* value) {
    DWORD size = sizeof(DWORD), type = 0;
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

void readBool(HKEY key, const wchar_t* name, bool* value) {
    DWORD v = 0;
    if (readDword(key, name, &v)) *value = v != 0;
}

void writeDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

SnapSettings readSnap(HKEY key) {
    SnapSettings snap;
    DWORD v = 0;
    if (readDword(key, L"SnapModes", &v)) snap.modes = v & kSnapAllModes;
    readBool(key, L"Snap", &snap.enabled);
    bool ortho = false, polar = false;
    readBool(key, L"Ortho", &ortho);
    readBool(key, L"Polar", &polar);
    // Orto y Polar se excluyen: si el registro trae los dos, gana Orto.
    snap.constraint.kind = ortho ? ConstraintKind::Ortho : polar ? ConstraintKind::Polar : ConstraintKind::None;
    if (readDword(key, L"PolarStep", &v) && (v == 15 || v == 30 || v == 45 || v == 90)) {
        snap.constraint.polarStepDegrees = v;
    }
    return snap;
}

}  // namespace

SnapSettings loadSnapSettings() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return SnapSettings();
    const SnapSettings snap = readSnap(key);
    RegCloseKey(key);
    return snap;
}

ViewerSettings loadSettings() {
    ViewerSettings s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return s;
    s.snap = readSnap(key);
    DWORD v = 0;
    readBool(key, L"RibbonCollapsed", &s.ribbonCollapsed);
    if (readDword(key, L"RibbonTab", &v) && v <= 4) s.ribbonTab = static_cast<int>(v);
    readBool(key, L"Panel", &s.panelVisible);
    if (readDword(key, L"PanelWidth", &v) && v >= 160 && v <= 800) s.panelWidth = static_cast<int>(v);
    readBool(key, L"StatusBar", &s.statusVisible);
    readBool(key, L"ViewCube", &s.cubeVisible);
    readBool(key, L"Maximized", &s.maximized);

    RECT window = {};
    DWORD size = sizeof(window), type = 0;
    if (RegQueryValueExW(key, L"Window", nullptr, &type, reinterpret_cast<BYTE*>(&window), &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size == sizeof(window) && window.right - window.left >= 200 && window.bottom - window.top >= 150 &&
        MonitorFromRect(&window, MONITOR_DEFAULTTONULL)) {
        s.window = window;  // solo si sigue cayendo en algun monitor
    }

    size = 0;
    if (RegQueryValueExW(key, L"Recent", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ &&
        size > 0 && size < 64 * 1024) {
        std::wstring buffer(size / sizeof(wchar_t) + 2, L'\0');
        if (RegQueryValueExW(key, L"Recent", nullptr, &type, reinterpret_cast<BYTE*>(&buffer[0]), &size) == ERROR_SUCCESS) {
            for (const wchar_t* p = buffer.c_str(); *p; p += wcslen(p) + 1) s.recent.push_back(p);
        }
    }
    RegCloseKey(key);
    return s;
}

void saveSettings(const ViewerSettings& s) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    writeDword(key, L"SnapModes", s.snap.modes & kSnapAllModes);
    writeDword(key, L"Snap", s.snap.enabled);
    writeDword(key, L"Ortho", s.snap.constraint.kind == ConstraintKind::Ortho);
    writeDword(key, L"Polar", s.snap.constraint.kind == ConstraintKind::Polar);
    writeDword(key, L"PolarStep", static_cast<DWORD>(s.snap.constraint.polarStepDegrees));
    writeDword(key, L"RibbonCollapsed", s.ribbonCollapsed);
    writeDword(key, L"RibbonTab", static_cast<DWORD>(s.ribbonTab));
    writeDword(key, L"Panel", s.panelVisible);
    writeDword(key, L"PanelWidth", static_cast<DWORD>(s.panelWidth));
    writeDword(key, L"StatusBar", s.statusVisible);
    writeDword(key, L"ViewCube", s.cubeVisible);
    writeDword(key, L"Maximized", s.maximized);
    if (s.window.right > s.window.left) {
        RegSetValueExW(key, L"Window", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&s.window), sizeof(s.window));
    }
    std::wstring multi;
    for (const std::wstring& path : s.recent) {
        if (path.empty()) continue;
        multi += path;
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    RegSetValueExW(key, L"Recent", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(multi.data()),
                   static_cast<DWORD>(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
}

}  // namespace stp
