// MyFolderMenu - C++ Win32 port (no third-party dependencies)
// Native Win32 tray application, equivalent to the original C# WinForms app.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <winver.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")
#endif

namespace {

constexpr wchar_t kAppName[] = L"MyFolderMenu";
constexpr wchar_t kMutexName[] = L"Local\\MyFolderMenu";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTrayIconId = 1;
constexpr UINT kMenuIdBase = 100;
constexpr int kIdStartup = 2001;
constexpr int kIdHideExt = 2002;
constexpr int kIdThemeLight = 2011;
constexpr int kIdThemeDark = 2012;
constexpr int kIdThemeSystem = 2013;
constexpr int kIdExport = 2021;
constexpr int kIdImport = 2022;

enum class AppTheme { System, Light, Dark };

struct AppSettings {
    bool hideFileExtensions = true;
    AppTheme theme = AppTheme::System;
};

std::wstring ThemeToIni(AppTheme theme);
AppTheme ThemeFromIni(const std::wstring& value);

enum class ActionType { None, OpenPath, AddFolder, RemoveFolder, Settings, About, ExitApp };

struct MenuAction {
    ActionType type = ActionType::None;
    std::wstring path;
};

struct MenuItemData {
    std::wstring text;
    HICON icon = nullptr;
    bool hasSubmenu = false;
    bool separator = false;
    bool disabled = false;
};

struct DirEntry {
    std::wstring path;
    std::wstring name;
    bool isDir = false;
};

struct CiLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        int r = CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE);
        return r == CSTR_LESS_THAN;
    }
};

HINSTANCE g_hInst = nullptr;
HWND g_hwndMain = nullptr;
HICON g_appIcon = nullptr;
HICON g_folderIcon = nullptr;
HFONT g_menuFont = nullptr;
HFONT g_uiFont = nullptr;
UINT g_taskbarCreatedMessage = 0;
int g_dpi = 96;
bool g_menuDark = false;
HBRUSH g_menuBrush = nullptr;
HBRUSH g_menuBgBrush = nullptr;
HBRUSH g_menuSelBrush = nullptr;
std::vector<std::unique_ptr<MenuItemData>> g_menuItems;
std::map<UINT, MenuAction> g_menuActions;
std::map<HMENU, std::wstring> g_lazyMenus;
UINT g_nextMenuId = kMenuIdBase;
bool g_hideExtensions = true;
std::map<std::wstring, HICON, CiLess> g_fileIcons;

using SetPreferredAppModeFn = int (WINAPI*)(int);
using FlushMenuThemesFn = void (WINAPI*)();
SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;

int Scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

std::wstring Trim(const std::wstring& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && iswspace(text[start])) {
        ++start;
    }
    while (end > start && iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(start, end - start);
}

std::wstring ToLower(std::wstring text) {
    if (!text.empty()) {
        CharLowerBuffW(&text[0], (DWORD)text.size());
    }
    return text;
}

bool EqualsCI(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

std::wstring EscapeAmp(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 4);
    for (wchar_t c : text) {
        if (c == L'&') {
            out += L'&';
        }
        out += c;
    }
    return out;
}

std::wstring ExePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
        if (length == 0) {
            return L"";
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetSpecialFolder(int csidl) {
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buffer))) {
        return buffer;
    }
    return L"";
}

std::wstring AppDataDir() {
    std::wstring appData = GetSpecialFolder(CSIDL_APPDATA);
    if (appData.empty()) {
        wchar_t buffer[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            appData = std::wstring(buffer, length);
        }
    }
    if (appData.empty()) {
        return L".";
    }
    return appData + L"\\MyFolderMenu";
}

std::wstring IniPath() {
    return AppDataDir() + L"\\folders.ini";
}

std::wstring LegacyJsonPath() {
    return AppDataDir() + L"\\folders.json";
}

std::wstring ExpandEnv(const std::wstring& text) {
    DWORD required = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (required == 0) {
        return text;
    }
    std::vector<wchar_t> buffer(required);
    DWORD length = ExpandEnvironmentStringsW(text.c_str(), buffer.data(), required);
    if (length == 0 || length > required) {
        return text;
    }
    return std::wstring(buffer.data(), length - 1);
}

std::wstring FullPath(const std::wstring& text) {
    DWORD size = 512;
    std::vector<wchar_t> buffer;
    for (;;) {
        buffer.resize(size);
        DWORD length = GetFullPathNameW(text.c_str(), size, buffer.data(), nullptr);
        if (length == 0) {
            return text;
        }
        if (length < size) {
            return std::wstring(buffer.data(), length);
        }
        size = length + 1;
    }
}

std::vector<std::wstring> NormalizeFolders(const std::vector<std::wstring>& input) {
    std::vector<std::wstring> result;
    for (const std::wstring& raw : input) {
        std::wstring value = Trim(raw);
        if (value.empty()) {
            continue;
        }
        std::wstring full = FullPath(ExpandEnv(value));
        if (full.empty()) {
            full = value;
        }
        bool duplicate = false;
        for (const std::wstring& existing : result) {
            if (EqualsCI(existing, full)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            result.push_back(full);
        }
    }
    return result;
}

std::wstring FileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring FolderLabel(const std::wstring& path) {
    std::wstring trimmed = path;
    while (trimmed.size() > 1 && (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        return path;
    }
    std::wstring name = FileName(trimmed);
    if (name.empty()) {
        return trimmed;
    }
    return name;
}

std::wstring FileLabel(const std::wstring& path) {
    std::wstring name = FileName(path);
    if (!g_hideExtensions) {
        return name;
    }
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        return name.substr(0, dot);
    }
    return name;
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::wstring& path) {
    int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    std::wstring path = dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    path += name;
    return path;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n' || text[i] == L'\r') {
            lines.push_back(text.substr(start, i - start));
            if (i < text.size() && text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            start = i + 1;
        }
    }
    return lines;
}

std::wstring Utf8ToWide(const std::string& bytes) {
    if (bytes.empty()) {
        return L"";
    }
    UINT codepage = CP_UTF8;
    int count = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (count <= 0) {
        codepage = CP_ACP;
        count = MultiByteToWideChar(codepage, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    }
    if (count <= 0) {
        return L"";
    }
    std::wstring out((size_t)count, L'\0');
    MultiByteToWideChar(codepage, 0, bytes.data(), (int)bytes.size(), &out[0], count);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return std::string();
    }
    std::string out((size_t)count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], count, nullptr, nullptr);
    return out;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::string bytes((size_t)size.QuadPart, '\0');
    DWORD total = 0;
    while (total < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, &bytes[total], (DWORD)(bytes.size() - total), &read, nullptr) || read == 0) {
            break;
        }
        total += read;
    }
    CloseHandle(file);
    bytes.resize(total);
    if (bytes.size() >= 3 &&
        (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF) {
        bytes.erase(0, 3);
    }
    out = Utf8ToWide(bytes);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    std::string bytes = WideToUtf8(text);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == bytes.size();
}

bool HasFoldersSection(const std::vector<std::wstring>& lines) {
    for (const std::wstring& raw : lines) {
        std::wstring line = Trim(raw);
        if (line.size() < 2 || line.front() != L'[' || line.back() != L']') {
            continue;
        }
        std::wstring section = Trim(line.substr(1, line.size() - 2));
        if (EqualsCI(section, L"Folders") || EqualsCI(section, L"Pastas")) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> ParseIniFolders(const std::vector<std::wstring>& lines) {
    std::vector<std::wstring> folders;
    bool inFolders = false;
    for (const std::wstring& raw : lines) {
        std::wstring line = Trim(raw);
        if (line.empty() || line.front() == L';' || line.front() == L'#') {
            continue;
        }
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            std::wstring section = Trim(line.substr(1, line.size() - 2));
            inFolders = EqualsCI(section, L"Folders") || EqualsCI(section, L"Pastas");
            continue;
        }
        if (!inFolders) {
            continue;
        }
        size_t separator = line.find(L'=');
        if (separator == std::wstring::npos || separator == 0) {
            continue;
        }
        std::wstring value = Trim(line.substr(separator + 1));
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
            value = value.substr(1, value.size() - 2);
        }
        folders.push_back(value);
    }
    return NormalizeFolders(folders);
}

AppSettings ParseIniSettings(const std::vector<std::wstring>& lines) {
    AppSettings settings;
    bool inSettings = false;
    for (const std::wstring& raw : lines) {
        std::wstring line = Trim(raw);
        if (line.empty() || line.front() == L';' || line.front() == L'#') {
            continue;
        }
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            std::wstring section = Trim(line.substr(1, line.size() - 2));
            inSettings = EqualsCI(section, L"Settings");
            continue;
        }
        if (!inSettings) {
            continue;
        }
        size_t separator = line.find(L'=');
        if (separator == std::wstring::npos || separator == 0) {
            continue;
        }
        std::wstring key = Trim(line.substr(0, separator));
        std::wstring value = ToLower(Trim(line.substr(separator + 1)));
        if (EqualsCI(key, L"HideFileExtensions")) {
            if (value == L"1" || value == L"true" || value == L"sim" || value == L"yes") {
                settings.hideFileExtensions = true;
            } else if (value == L"0" || value == L"false" || value == L"nao" || value == L"não" || value == L"no") {
                settings.hideFileExtensions = false;
            }
        } else if (EqualsCI(key, L"Theme")) {
            settings.theme = ThemeFromIni(value);
        }
    }
    return settings;
}

std::wstring ThemeToIni(AppTheme theme) {
    switch (theme) {
        case AppTheme::Light: return L"Claro";
        case AppTheme::Dark: return L"Escuro";
        default: return L"Sistema";
    }
}

AppTheme ThemeFromIni(const std::wstring& value) {
    std::wstring lower = ToLower(Trim(value));
    if (lower == L"claro" || lower == L"light") {
        return AppTheme::Light;
    }
    if (lower == L"escuro" || lower == L"dark") {
        return AppTheme::Dark;
    }
    return AppTheme::System;
}

bool WriteIniFile(const std::wstring& path, const std::vector<std::wstring>& folders, const AppSettings& settings) {
    std::vector<std::wstring> normalized = NormalizeFolders(folders);
    std::wstring text = L"[Folders]\r\n";
    for (size_t i = 0; i < normalized.size(); ++i) {
        text += L"Folder" + std::to_wstring(i + 1) + L"=" + normalized[i] + L"\r\n";
    }
    text += L"\r\n[Settings]\r\n";
    text += std::wstring(L"HideFileExtensions=") + (settings.hideFileExtensions ? L"1" : L"0") + L"\r\n";
    text += L"Theme=" + ThemeToIni(settings.theme) + L"\r\n";
    return WriteTextFile(path, text);
}

bool ReadIniFile(const std::wstring& path, std::vector<std::wstring>& folders, AppSettings& settings) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }
    std::vector<std::wstring> lines = SplitLines(text);
    if (!HasFoldersSection(lines)) {
        return false;
    }
    folders = ParseIniFolders(lines);
    settings = ParseIniSettings(lines);
    return true;
}

std::vector<std::wstring> ParseLegacyJson(const std::wstring& text) {
    std::vector<std::wstring> result;
    size_t pos = text.find(L"\"Folders\"");
    if (pos == std::wstring::npos) {
        return result;
    }
    pos = text.find(L':', pos);
    if (pos == std::wstring::npos) {
        return result;
    }
    pos = text.find(L'[', pos);
    if (pos == std::wstring::npos) {
        return result;
    }
    ++pos;
    while (pos < text.size()) {
        while (pos < text.size() && (iswspace(text[pos]) || text[pos] == L',')) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] == L']') {
            break;
        }
        if (text[pos] != L'"') {
            break;
        }
        ++pos;
        std::wstring value;
        bool closed = false;
        while (pos < text.size()) {
            wchar_t c = text[pos++];
            if (c == L'"') {
                closed = true;
                break;
            }
            if (c != L'\\') {
                value += c;
                continue;
            }
            if (pos >= text.size()) {
                break;
            }
            wchar_t escaped = text[pos++];
            switch (escaped) {
                case L'"': value += L'"'; break;
                case L'\\': value += L'\\'; break;
                case L'/': value += L'/'; break;
                case L'b': value += L'\b'; break;
                case L'f': value += L'\f'; break;
                case L'n': value += L'\n'; break;
                case L'r': value += L'\r'; break;
                case L't': value += L'\t'; break;
                case L'u': {
                    if (pos + 4 <= text.size()) {
                        unsigned int code = 0;
                        bool valid = true;
                        for (int i = 0; i < 4; ++i) {
                            wchar_t h = text[pos + i];
                            int digit = -1;
                            if (h >= L'0' && h <= L'9') digit = h - L'0';
                            else if (h >= L'a' && h <= L'f') digit = 10 + h - L'a';
                            else if (h >= L'A' && h <= L'F') digit = 10 + h - L'A';
                            if (digit < 0) { valid = false; break; }
                            code = code * 16 + (unsigned int)digit;
                        }
                        if (valid) {
                            value += (wchar_t)code;
                            pos += 4;
                        }
                    }
                    break;
                }
                default: value += escaped; break;
            }
        }
        if (!closed) {
            return result;
        }
        result.push_back(value);
    }
    return result;
}

std::vector<std::wstring> LoadLegacyFolders() {
    std::wstring text;
    if (!ReadTextFile(LegacyJsonPath(), text)) {
        return std::vector<std::wstring>();
    }
    return NormalizeFolders(ParseLegacyJson(text));
}

std::vector<std::wstring> DefaultFolders() {
    std::vector<std::wstring> folders;
    std::wstring desktop = GetSpecialFolder(CSIDL_DESKTOPDIRECTORY);
    std::wstring profile = GetSpecialFolder(CSIDL_PROFILE);
    std::wstring documents = GetSpecialFolder(CSIDL_PERSONAL);
    if (!desktop.empty()) {
        folders.push_back(desktop);
    }
    if (!profile.empty()) {
        folders.push_back(profile + L"\\Downloads");
    }
    if (!documents.empty()) {
        folders.push_back(documents);
    }
    return NormalizeFolders(folders);
}

std::vector<std::wstring> LoadFolders() {
    std::wstring text;
    if (ReadTextFile(IniPath(), text)) {
        return ParseIniFolders(SplitLines(text));
    }
    std::vector<std::wstring> legacy = LoadLegacyFolders();
    if (!legacy.empty()) {
        return legacy;
    }
    return DefaultFolders();
}

AppSettings LoadSettings() {
    std::wstring text;
    if (ReadTextFile(IniPath(), text)) {
        return ParseIniSettings(SplitLines(text));
    }
    return AppSettings();
}

bool SaveConfig(const std::vector<std::wstring>& folders, const AppSettings& settings) {
    if (!EnsureDirectory(AppDataDir())) {
        return false;
    }
    return WriteIniFile(IniPath(), folders, settings);
}

void EnsureConfigCreated() {
    if (GetFileAttributesW(IniPath().c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    std::vector<std::wstring> folders = LoadLegacyFolders();
    if (folders.empty()) {
        folders = DefaultFolders();
    }
    SaveConfig(folders, AppSettings());
}

bool HasVisibleEntries(const std::wstring& path) {
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool found = false;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0) {
            found = true;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

bool ListEntries(const std::wstring& path, std::vector<DirEntry>& entries) {
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0) {
            continue;
        }
        DirEntry entry;
        entry.name = data.cFileName;
        entry.path = JoinPath(path, data.cFileName);
        entry.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return true;
}

bool IsDarkTheme(AppTheme theme) {
    if (theme == AppTheme::Dark) {
        return true;
    }
    if (theme == AppTheme::Light) {
        return false;
    }
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&value, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && value == 0;
}

void InitThemeApi() {
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxtheme) {
        return;
    }
    FARPROC setMode = GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    FARPROC flushThemes = GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
    static_assert(sizeof(setMode) == sizeof(g_setPreferredAppMode), "unexpected function pointer size");
    static_assert(sizeof(flushThemes) == sizeof(g_flushMenuThemes), "unexpected function pointer size");
    std::memcpy(&g_setPreferredAppMode, &setMode, sizeof(setMode));
    std::memcpy(&g_flushMenuThemes, &flushThemes, sizeof(flushThemes));
}

void SetAppThemeMode(bool dark) {
    if (g_setPreferredAppMode) {
        g_setPreferredAppMode(dark ? 2 : 3);
    }
    if (g_flushMenuThemes) {
        g_flushMenuThemes();
    }
}

void ApplyDarkTitleBar(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value)))) {
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
    }
}

bool StartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, kAppName, nullptr, &type, (LPBYTE)buffer, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && buffer[0] != 0;
}

bool StartupSetEnabled(bool enabled) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    if (enabled) {
        std::wstring command = L"\"" + ExePath() + L"\"";
        result = RegSetValueExW(key, kAppName, 0, REG_SZ, (const BYTE*)command.c_str(),
                                (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kAppName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

HICON LoadShellIcon(const std::wstring& path, bool useAttributes, DWORD attributes) {
    SHFILEINFOW info = {};
    UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
    if (useAttributes) {
        flags |= SHGFI_USEFILEATTRIBUTES;
    }
    if (SHGetFileInfoW(path.c_str(), attributes, &info, sizeof(info), flags) && info.hIcon) {
        return info.hIcon;
    }
    return nullptr;
}

HICON GetFolderIconCached() {
    if (!g_folderIcon) {
        g_folderIcon = LoadShellIcon(L"folder", true, FILE_ATTRIBUTE_DIRECTORY);
        if (!g_folderIcon) {
            g_folderIcon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        }
    }
    return g_folderIcon;
}

HICON GetFileIconCached(const std::wstring& path) {
    auto it = g_fileIcons.find(path);
    if (it != g_fileIcons.end()) {
        return it->second;
    }
    HICON icon = LoadShellIcon(path, false, 0);
    if (!icon) {
        std::wstring name = FileName(path);
        size_t dot = name.find_last_of(L'.');
        std::wstring extension = dot == std::wstring::npos ? L"" : name.substr(dot);
        icon = LoadShellIcon(L"file" + extension, true, FILE_ATTRIBUTE_NORMAL);
    }
    if (!icon) {
        icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }
    g_fileIcons[path] = icon;
    return icon;
}

void ClearFileIcons() {
    for (auto& item : g_fileIcons) {
        if (item.second) {
            DestroyIcon(item.second);
        }
    }
    g_fileIcons.clear();
}

void CleanupIcons() {
    ClearFileIcons();
    if (g_folderIcon) {
        DestroyIcon(g_folderIcon);
        g_folderIcon = nullptr;
    }
    if (g_appIcon) {
        DestroyIcon(g_appIcon);
        g_appIcon = nullptr;
    }
}

} // namespace
namespace {

// Forward declarations (implemented in later sections).
void ExecuteAction(HWND owner, const MenuAction& action);
void OpenPath(HWND owner, const std::wstring& path);
std::wstring BrowseForFolder(HWND owner);
bool ShowSettingsDialog(HWND owner, AppSettings& settings, std::vector<std::wstring>& folders);
void ShowAboutDialog(HWND owner, const AppSettings& settings);
void ExitApplication();
bool AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);

void ApplyMenuInfo(HMENU menu) {
    if (!menu || !g_menuBrush) {
        return;
    }
    MENUINFO info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS | MIM_STYLE;
    info.dwStyle = MNS_NOCHECK;
    info.hbrBack = g_menuBrush;
    SetMenuInfo(menu, &info);
}

UINT NewMenuId() {
    return g_nextMenuId++;
}

void AddMenuItem(HMENU menu, const std::wstring& text, HICON icon, HMENU submenu,
                 bool disabled, const MenuAction& action) {
    UINT id = NewMenuId();
    MenuItemData* data = new MenuItemData();
    data->text = text;
    data->icon = icon;
    data->hasSubmenu = submenu != nullptr;
    data->disabled = disabled;
    g_menuItems.emplace_back(std::unique_ptr<MenuItemData>(data));

    std::wstring escaped = EscapeAmp(text);
    MENUITEMINFOW item = {};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STRING | MIIM_STATE;
    item.fType = MFT_OWNERDRAW;
    item.fState = disabled ? MFS_DISABLED : MFS_ENABLED;
    item.wID = id;
    item.dwTypeData = escaped.data();
    item.cch = (UINT)escaped.size();
    item.dwItemData = (ULONG_PTR)data;
    if (submenu) {
        item.fMask |= MIIM_SUBMENU;
        item.hSubMenu = submenu;
    }
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item);
    if (action.type != ActionType::None) {
        g_menuActions[id] = action;
    }
}

void AddSeparator(HMENU menu) {
    MenuItemData* data = new MenuItemData();
    data->separator = true;
    data->disabled = true;
    g_menuItems.emplace_back(std::unique_ptr<MenuItemData>(data));

    MENUITEMINFOW item = {};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STRING | MIIM_STATE;
    item.fType = MFT_OWNERDRAW;
    item.fState = MFS_DISABLED;
    item.wID = NewMenuId();
    item.dwTypeData = const_cast<wchar_t*>(L"");
    item.cch = 0;
    item.dwItemData = (ULONG_PTR)data;
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item);
}

void AddFolderItem(HMENU menu, const std::wstring& path) {
    std::wstring label = FolderLabel(path);
    HICON icon = GetFolderIconCached();
    if (!HasVisibleEntries(path)) {
        MenuAction action;
        action.type = ActionType::OpenPath;
        action.path = path;
        AddMenuItem(menu, label, icon, nullptr, false, action);
        return;
    }

    HMENU submenu = CreatePopupMenu();
    ApplyMenuInfo(submenu);
    MenuAction open;
    open.type = ActionType::OpenPath;
    open.path = path;
    AddMenuItem(submenu, L"Abrir esta pasta", icon, nullptr, false, open);
    AddSeparator(submenu);
    AddMenuItem(submenu, L"Carregando...", nullptr, nullptr, true, MenuAction());
    g_lazyMenus[submenu] = path;
    AddMenuItem(menu, label, icon, submenu, false, MenuAction());
}

void AddFileItem(HMENU menu, const std::wstring& path) {
    MenuAction action;
    action.type = ActionType::OpenPath;
    action.path = path;
    AddMenuItem(menu, FileLabel(path), GetFileIconCached(path), nullptr, false, action);
}

void PopulateSubmenu(HMENU menu, const std::wstring& path) {
    while (GetMenuItemCount(menu) > 0) {
        DeleteMenu(menu, 0, MF_BYPOSITION);
    }

    MenuAction open;
    open.type = ActionType::OpenPath;
    open.path = path;
    AddMenuItem(menu, L"Abrir esta pasta", GetFolderIconCached(), nullptr, false, open);
    AddSeparator(menu);

    std::vector<DirEntry> entries;
    if (!ListEntries(path, entries)) {
        AddMenuItem(menu, L"(pasta indisponível)", nullptr, nullptr, true, MenuAction());
        return;
    }

    std::vector<DirEntry> directories;
    std::vector<DirEntry> files;
    for (DirEntry& entry : entries) {
        if (entry.isDir) {
            directories.push_back(std::move(entry));
        } else {
            files.push_back(std::move(entry));
        }
    }

    auto compare = [](const DirEntry& a, const DirEntry& b) {
        int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE,
                                     a.name.c_str(), -1, b.name.c_str(), -1,
                                     nullptr, nullptr, 0);
        return result == CSTR_LESS_THAN;
    };
    std::sort(directories.begin(), directories.end(), compare);
    std::sort(files.begin(), files.end(), compare);

    if (directories.empty() && files.empty()) {
        AddMenuItem(menu, L"(vazio)", nullptr, nullptr, true, MenuAction());
        return;
    }

    for (const DirEntry& entry : directories) {
        AddFolderItem(menu, entry.path);
    }
    if (!directories.empty() && !files.empty()) {
        AddSeparator(menu);
    }
    for (const DirEntry& entry : files) {
        AddFileItem(menu, entry.path);
    }
}

HMENU BuildMenu() {
    AppSettings settings = LoadSettings();
    g_hideExtensions = settings.hideFileExtensions;
    g_menuDark = IsDarkTheme(settings.theme);
    SetAppThemeMode(g_menuDark);

    g_menuItems.clear();
    g_menuActions.clear();
    g_lazyMenus.clear();
    g_nextMenuId = kMenuIdBase;

    if (g_menuBrush) {
        DeleteObject(g_menuBrush);
        g_menuBrush = nullptr;
    }
    if (g_menuBgBrush) {
        DeleteObject(g_menuBgBrush);
        g_menuBgBrush = nullptr;
    }
    if (g_menuSelBrush) {
        DeleteObject(g_menuSelBrush);
        g_menuSelBrush = nullptr;
    }

    COLORREF background = g_menuDark ? RGB(37, 37, 38) : GetSysColor(COLOR_MENU);
    COLORREF selected = g_menuDark ? RGB(62, 62, 66) : GetSysColor(COLOR_HIGHLIGHT);
    g_menuBrush = CreateSolidBrush(background);
    g_menuBgBrush = CreateSolidBrush(background);
    g_menuSelBrush = CreateSolidBrush(selected);

    HMENU root = CreatePopupMenu();
    ApplyMenuInfo(root);

    std::vector<std::wstring> folders = LoadFolders();
    int available = 0;
    for (const std::wstring& folder : folders) {
        if (DirectoryExists(folder)) {
            AddFolderItem(root, folder);
            ++available;
        }
    }
    if (available == 0) {
        AddMenuItem(root, L"Nenhuma pasta configurada", nullptr, nullptr, true, MenuAction());
    }
    AddSeparator(root);

    HMENU manage = CreatePopupMenu();
    ApplyMenuInfo(manage);
    MenuAction add;
    add.type = ActionType::AddFolder;
    AddMenuItem(manage, L"Adicionar pasta...", nullptr, nullptr, false, add);

    HMENU remove = CreatePopupMenu();
    ApplyMenuInfo(remove);
    if (folders.empty()) {
        AddMenuItem(manage, L"Remover pasta", nullptr, nullptr, true, MenuAction());
    } else {
        for (const std::wstring& folder : folders) {
            MenuAction action;
            action.type = ActionType::RemoveFolder;
            action.path = folder;
            AddMenuItem(remove, FolderLabel(folder) + L"  —  " + folder,
                        nullptr, nullptr, false, action);
        }
        AddMenuItem(manage, L"Remover pasta", nullptr, remove, false, MenuAction());
    }
    AddMenuItem(root, L"Pastas", nullptr, manage, false, MenuAction());

    MenuAction settingsAction;
    settingsAction.type = ActionType::Settings;
    AddMenuItem(root, L"Configurações...", nullptr, nullptr, false, settingsAction);

    MenuAction aboutAction;
    aboutAction.type = ActionType::About;
    AddMenuItem(root, L"Sobre", nullptr, nullptr, false, aboutAction);

    AddSeparator(root);
    MenuAction exitAction;
    exitAction.type = ActionType::ExitApp;
    AddMenuItem(root, L"Sair", nullptr, nullptr, false, exitAction);

    return root;
}

void ShowTrayMenu(HWND owner, POINT point) {
    HMENU root = BuildMenu();
    SetForegroundWindow(owner);
    UINT command = TrackPopupMenuEx(root,
                                    TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                                    point.x, point.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);

    MenuAction action;
    if (command != 0) {
        auto it = g_menuActions.find(command);
        if (it != g_menuActions.end()) {
            action = it->second;
        }
    }

    g_lazyMenus.clear();
    DestroyMenu(root);
    if (g_menuBrush) {
        DeleteObject(g_menuBrush);
        g_menuBrush = nullptr;
    }
    if (g_menuBgBrush) {
        DeleteObject(g_menuBgBrush);
        g_menuBgBrush = nullptr;
    }
    if (g_menuSelBrush) {
        DeleteObject(g_menuSelBrush);
        g_menuSelBrush = nullptr;
    }
    g_menuItems.clear();
    g_menuActions.clear();
    ClearFileIcons();

    if (action.type != ActionType::None) {
        ExecuteAction(owner, action);
    }
}

void ExecuteAction(HWND owner, const MenuAction& action) {
    switch (action.type) {
        case ActionType::OpenPath:
            OpenPath(owner, action.path);
            break;
        case ActionType::AddFolder: {
            std::wstring selected = BrowseForFolder(owner);
            if (selected.empty()) {
                break;
            }
            std::vector<std::wstring> folders = LoadFolders();
            AppSettings settings = LoadSettings();
            bool exists = false;
            for (const std::wstring& folder : folders) {
                if (EqualsCI(folder, selected)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                folders.push_back(selected);
            }
            SaveConfig(folders, settings);
            break;
        }
        case ActionType::RemoveFolder: {
            std::vector<std::wstring> folders = LoadFolders();
            AppSettings settings = LoadSettings();
            folders.erase(std::remove_if(folders.begin(), folders.end(),
                                         [&](const std::wstring& folder) {
                                             return EqualsCI(folder, action.path);
                                         }),
                          folders.end());
            SaveConfig(folders, settings);
            break;
        }
        case ActionType::Settings: {
            std::vector<std::wstring> folders = LoadFolders();
            AppSettings settings = LoadSettings();
            if (ShowSettingsDialog(owner, settings, folders)) {
                SaveConfig(folders, settings);
                SetAppThemeMode(IsDarkTheme(settings.theme));
            }
            break;
        }
        case ActionType::About:
            ShowAboutDialog(owner, LoadSettings());
            break;
        case ActionType::ExitApp:
            ExitApplication();
            break;
        default:
            break;
    }
}

void OpenPath(HWND owner, const std::wstring& path) {
    HINSTANCE result = ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        std::wstring message = L"Não foi possível abrir:\n" + path +
                               L"\n\nCódigo: " + std::to_wstring((INT_PTR)result);
        MessageBoxW(owner, message.c_str(), kAppName, MB_OK | MB_ICONERROR);
    }
}

void OpenFirstFolder(HWND owner) {
    std::vector<std::wstring> folders = LoadFolders();
    for (const std::wstring& folder : folders) {
        if (DirectoryExists(folder)) {
            OpenPath(owner, folder);
            return;
        }
    }
}

std::wstring BrowseForFolder(HWND owner) {
    BROWSEINFOW info = {};
    info.hwndOwner = owner;
    info.lpszTitle = L"Escolha uma pasta para exibir no menu";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&info);
    if (!pidl) {
        return L"";
    }
    wchar_t path[MAX_PATH] = {};
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) {
        result = path;
    }
    CoTaskMemFree(pidl);
    return result;
}

void ExitApplication() {
    if (g_hwndMain) {
        RemoveTrayIcon(g_hwndMain);
        DestroyWindow(g_hwndMain);
        g_hwndMain = nullptr;
    }
}

} // namespace
namespace {

struct SettingsState {
    HWND hwnd = nullptr;
    bool ok = false;
    bool dark = false;
    AppSettings settings;
    std::vector<std::wstring> folders;
    HWND startup = nullptr;
    HWND hideExtensions = nullptr;
    HWND lightRadio = nullptr;
    HWND darkRadio = nullptr;
    HWND systemRadio = nullptr;
    HBRUSH brush = nullptr;
};

void CenterWindow(HWND hwnd) {
    RECT window = {};
    GetWindowRect(hwnd, &window);
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = window.right - window.left;
    int height = window.bottom - window.top;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text,
                 DWORD style, int x, int y, int width, int height, int id, bool dark) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                   Scale(x), Scale(y), Scale(width), Scale(height),
                                   parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    if (control) {
        SendMessageW(control, WM_SETFONT,
                     (WPARAM)(g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        if (dark) {
            SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
        }
    }
    return control;
}

int CheckState(HWND control) {
    return (int)SendMessageW(control, BM_GETCHECK, 0, 0);
}

void SetChecked(HWND control, bool checked) {
    if (control) {
        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void RepaintDialogTheme(HWND hwnd, SettingsState* state, bool dark) {
    state->dark = dark;
    HBRUSH oldBrush = state->brush;
    state->brush = CreateSolidBrush(dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
    if (oldBrush) {
        DeleteObject(oldBrush);
    }
    ApplyDarkTitleBar(hwnd, dark);
    EnumChildWindows(hwnd, [](HWND child, LPARAM parameter) -> BOOL {
        SetWindowTheme(child, parameter ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        return TRUE;
    }, dark ? 1 : 0);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CreateSettingsControls(HWND hwnd, SettingsState* state) {
    bool dark = state->dark;
    state->startup = CreateChild(hwnd, L"BUTTON", L"Iniciar com o Windows",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 24, 24, 260, 22, kIdStartup, dark);
    state->hideExtensions = CreateChild(hwnd, L"BUTTON", L"Não exibir a extensão do arquivo",
                                        BS_AUTOCHECKBOX | WS_TABSTOP, 24, 58, 300, 22, kIdHideExt, dark);
    SetChecked(state->startup, StartupEnabled());
    SetChecked(state->hideExtensions, state->settings.hideFileExtensions);

    CreateChild(hwnd, L"BUTTON", L"Tema", BS_GROUPBOX, 24, 98, 332, 118, 0, dark);
    state->lightRadio = CreateChild(hwnd, L"BUTTON", L"Claro",
                                    BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                                    44, 126, 140, 22, kIdThemeLight, dark);
    state->darkRadio = CreateChild(hwnd, L"BUTTON", L"Escuro",
                                   BS_AUTORADIOBUTTON | WS_TABSTOP,
                                   44, 154, 140, 22, kIdThemeDark, dark);
    state->systemRadio = CreateChild(hwnd, L"BUTTON", L"Sistema",
                                     BS_AUTORADIOBUTTON | WS_TABSTOP,
                                     44, 182, 140, 22, kIdThemeSystem, dark);

    SetChecked(state->lightRadio, state->settings.theme == AppTheme::Light);
    SetChecked(state->darkRadio, state->settings.theme == AppTheme::Dark);
    SetChecked(state->systemRadio, state->settings.theme == AppTheme::System);

    CreateChild(hwnd, L"BUTTON", L"Exportar configurações", BS_PUSHBUTTON | WS_TABSTOP,
                24, 232, 164, 32, kIdExport, dark);
    CreateChild(hwnd, L"BUTTON", L"Importar configurações", BS_PUSHBUTTON | WS_TABSTOP,
                192, 232, 164, 32, kIdImport, dark);
    CreateChild(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
                166, 284, 90, 32, IDOK, dark);
    CreateChild(hwnd, L"BUTTON", L"Cancelar", BS_PUSHBUTTON | WS_TABSTOP,
                266, 284, 90, 32, IDCANCEL, dark);
}

AppSettings ReadSettingsControls(const SettingsState* state) {
    AppSettings settings;
    settings.hideFileExtensions = CheckState(state->hideExtensions) == BST_CHECKED;
    if (CheckState(state->darkRadio) == BST_CHECKED) {
        settings.theme = AppTheme::Dark;
    } else if (CheckState(state->lightRadio) == BST_CHECKED) {
        settings.theme = AppTheme::Light;
    } else {
        settings.theme = AppTheme::System;
    }
    return settings;
}

void ApplySettingsAndClose(HWND hwnd, SettingsState* state) {
    bool startWithWindows = CheckState(state->startup) == BST_CHECKED;
    if (!StartupSetEnabled(startWithWindows)) {
        MessageBoxW(hwnd, L"Não foi possível alterar a inicialização com o Windows.",
                    kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    state->settings = ReadSettingsControls(state);
    state->ok = true;
    DestroyWindow(hwnd);
}

void ExportSettings(HWND hwnd, SettingsState* state) {
    AppSettings settings = ReadSettingsControls(state);
    wchar_t file[MAX_PATH] = L"MyFolderMenu.ini";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Configuração do MyFolderMenu (*.ini)\0*.ini\0Todos os arquivos (*.*)\0*.*\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"ini";
    dialog.lpstrTitle = L"Exportar configurações";
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }
    if (WriteIniFile(file, state->folders, settings)) {
        std::wstring message = L"Configurações exportadas para:\n" + std::wstring(file);
        MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring message = L"Não foi possível exportar as configurações para:\n" + std::wstring(file);
        MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONERROR);
    }
}

void ImportSettings(HWND hwnd, SettingsState* state) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Configuração do MyFolderMenu (*.ini)\0*.ini\0Todos os arquivos (*.*)\0*.*\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"ini";
    dialog.lpstrTitle = L"Importar configurações";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    std::vector<std::wstring> folders;
    AppSettings settings;
    if (!ReadIniFile(file, folders, settings)) {
        std::wstring message = L"Arquivo de configuração inválido ou inacessível:\n" + std::wstring(file);
        MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONERROR);
        return;
    }

    state->folders = folders;
    SetChecked(state->hideExtensions, settings.hideFileExtensions);
    SetChecked(state->lightRadio, settings.theme == AppTheme::Light);
    SetChecked(state->darkRadio, settings.theme == AppTheme::Dark);
    SetChecked(state->systemRadio, settings.theme == AppTheme::System);

    bool dark = IsDarkTheme(settings.theme);
    SetAppThemeMode(dark);
    RepaintDialogTheme(hwnd, state, dark);

    std::wstring message = L"Configurações importadas de:\n" + std::wstring(file) +
                           L"\n\nClique em OK para aplicar.";
    MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsState* state = (SettingsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
        case WM_CREATE: {
            CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
            state = (SettingsState*)create->lpCreateParams;
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            state->brush = CreateSolidBrush(state->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
            CreateSettingsControls(hwnd, state);
            ApplyDarkTitleBar(hwnd, state->dark);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            if (!state || !state->brush) {
                break;
            }
            HDC dc = (HDC)wParam;
            SetTextColor(dc, state->dark ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT));
            SetBkColor(dc, state->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
            return (LRESULT)state->brush;
        }
        case WM_ERASEBKGND: {
            if (!state || !state->brush) {
                break;
            }
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            FillRect((HDC)wParam, &rect, state->brush);
            return 1;
        }
        case WM_COMMAND: {
            if (!state) {
                break;
            }
            switch (LOWORD(wParam)) {
                case IDOK:
                    ApplySettingsAndClose(hwnd, state);
                    return 0;
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
                case kIdExport:
                    ExportSettings(hwnd, state);
                    return 0;
                case kIdImport:
                    ImportSettings(hwnd, state);
                    return 0;
                default:
                    break;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state && state->brush) {
                DeleteObject(state->brush);
                state->brush = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

struct AboutState {
    bool dark = false;
    HBRUSH brush = nullptr;
};

std::wstring GetAppVersionString() {
    std::wstring exe = ExePath();
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (size == 0) {
        return L"1.0.0";
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) {
        return L"1.0.0";
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&info, &length) && info) {
        return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
               std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
               std::to_wstring(HIWORD(info->dwFileVersionLS));
    }
    return L"1.0.0";
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AboutState* state = (AboutState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
        case WM_CREATE: {
            CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
            state = (AboutState*)create->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            state->brush = CreateSolidBrush(state->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
            CreateChild(hwnd, L"STATIC", L"MyFolderMenu", SS_LEFT, 24, 20, 380, 24, 0, state->dark);
            std::wstring version = L"Versão " + GetAppVersionString();
            CreateChild(hwnd, L"STATIC", version.c_str(), SS_LEFT, 24, 55, 380, 22, 0, state->dark);
            CreateChild(hwnd, L"STATIC", L"(2026) Copyleft by adrian.barbosa@gmail.com",
                        SS_LEFT, 24, 85, 380, 22, 0, state->dark);
            CreateChild(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
                        316, 128, 90, 30, IDOK, state->dark);
            ApplyDarkTitleBar(hwnd, state->dark);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            if (!state || !state->brush) {
                break;
            }
            HDC dc = (HDC)wParam;
            SetTextColor(dc, state->dark ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT));
            SetBkColor(dc, state->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
            return (LRESULT)state->brush;
        }
        case WM_ERASEBKGND: {
            if (!state || !state->brush) {
                break;
            }
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            FillRect((HDC)wParam, &rect, state->brush);
            return 1;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state && state->brush) {
                DeleteObject(state->brush);
                state->brush = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RunModal(HWND hwnd, HWND owner, HWND firstFocus, bool dark) {
    if (!hwnd) {
        return false;
    }
    CenterWindow(hwnd);
    if (owner) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(hwnd, SW_SHOW);
    ApplyDarkTitleBar(hwnd, dark);
    UpdateWindow(hwnd);
    if (firstFocus) {
        SetFocus(firstFocus);
    }

    MSG msg = {};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(hwnd, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return true;
}

bool ShowSettingsDialog(HWND owner, AppSettings& settings, std::vector<std::wstring>& folders) {
    SettingsState state;
    state.settings = settings;
    state.folders = folders;
    state.dark = IsDarkTheme(settings.theme);
    SetAppThemeMode(state.dark);

    RECT rect = {0, 0, Scale(380), Scale(340)};
    DWORD style = WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    HWND hwnd = CreateWindowExW(exStyle, L"MFMSettingsDialog", L"Configurações", style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                owner, nullptr, g_hInst, &state);
    if (!hwnd) {
        return false;
    }
    RunModal(hwnd, owner, state.startup, state.dark);
    if (state.ok) {
        settings = state.settings;
        folders = state.folders;
        return true;
    }
    return false;
}

void ShowAboutDialog(HWND owner, const AppSettings& settings) {
    AboutState state;
    state.dark = IsDarkTheme(settings.theme);
    SetAppThemeMode(state.dark);

    RECT rect = {0, 0, Scale(430), Scale(180)};
    DWORD style = WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    HWND hwnd = CreateWindowExW(exStyle, L"MFMAboutDialog", L"Sobre o MyFolderMenu", style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                owner, nullptr, g_hInst, &state);
    if (!hwnd) {
        return;
    }
    RunModal(hwnd, owner, nullptr, state.dark);
}

} // namespace
namespace {

bool AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = (UINT)kTrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = g_appIcon ? g_appIcon : g_folderIcon;
    lstrcpynW(data.szTip, kAppName, ARRAYSIZE(data.szTip));
    return Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void RemoveTrayIcon(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = (UINT)kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void CreateFonts() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        g_menuFont = CreateFontIndirectW(&metrics.lfMenuFont);
        g_uiFont = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (!g_menuFont) {
        g_menuFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (!g_uiFont) {
        g_uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kTrayMessage: {
            UINT event = LOWORD(lParam);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                POINT point = {};
                GetCursorPos(&point);
                ShowTrayMenu(hwnd, point);
            } else if (event == WM_LBUTTONDBLCLK) {
                OpenFirstFolder(hwnd);
            }
            return 0;
        }
        case WM_INITMENUPOPUP: {
            if (!HIWORD(lParam)) {
                HMENU menu = (HMENU)wParam;
                auto it = g_lazyMenus.find(menu);
                if (it != g_lazyMenus.end()) {
                    std::wstring path = it->second;
                    g_lazyMenus.erase(it);
                    PopulateSubmenu(menu, path);
                }
            }
            return 0;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* measure = (MEASUREITEMSTRUCT*)lParam;
            if (!measure || measure->CtlType != ODT_MENU) {
                break;
            }
            MenuItemData* data = (MenuItemData*)measure->itemData;
            if (!data) {
                break;
            }
            if (data->separator) {
                measure->itemHeight = (UINT)Scale(7);
                measure->itemWidth = 1;
                return TRUE;
            }

            int textWidth = 0;
            int textHeight = 0;
            HDC dc = GetDC(hwnd);
            if (dc) {
                HGDIOBJ oldFont = SelectObject(dc, g_menuFont);
                SIZE size = {};
                if (!data->text.empty()) {
                    GetTextExtentPoint32W(dc, data->text.c_str(), (int)data->text.size(), &size);
                }
                textWidth = (int)size.cx;
                textHeight = (int)size.cy;
                SelectObject(dc, oldFont);
                ReleaseDC(hwnd, dc);
            }

            int iconSize = GetSystemMetrics(SM_CXSMICON);
            int height = std::max(iconSize + Scale(8), textHeight + Scale(8));
            int width = Scale(8) + iconSize + Scale(8) + textWidth + Scale(24) +
                        (data->hasSubmenu ? Scale(20) : 0);
            measure->itemHeight = (UINT)height;
            measure->itemWidth = (UINT)width;
            return TRUE;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* draw = (DRAWITEMSTRUCT*)lParam;
            if (!draw || draw->CtlType != ODT_MENU) {
                break;
            }
            MenuItemData* data = (MenuItemData*)draw->itemData;
            if (!data) {
                break;
            }

            if (data->separator) {
                FillRect(draw->hDC, &draw->rcItem, g_menuBgBrush);
                int y = draw->rcItem.top + (draw->rcItem.bottom - draw->rcItem.top) / 2;
                HPEN pen = CreatePen(PS_SOLID, 1, g_menuDark ? RGB(78, 78, 82) : GetSysColor(COLOR_3DSHADOW));
                HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
                MoveToEx(draw->hDC, draw->rcItem.left + Scale(8), y, nullptr);
                LineTo(draw->hDC, draw->rcItem.right - Scale(8), y);
                SelectObject(draw->hDC, oldPen);
                DeleteObject(pen);
                return TRUE;
            }

            bool selected = (draw->itemState & ODS_SELECTED) != 0;
            bool disabled = data->disabled ||
                            (draw->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;

            COLORREF background = selected
                ? (g_menuDark ? RGB(62, 62, 66) : GetSysColor(COLOR_HIGHLIGHT))
                : (g_menuDark ? RGB(37, 37, 38) : GetSysColor(COLOR_MENU));
            COLORREF foreground = disabled
                ? (g_menuDark ? RGB(130, 130, 130) : GetSysColor(COLOR_GRAYTEXT))
                : selected
                    ? (g_menuDark ? RGB(255, 255, 255) : GetSysColor(COLOR_HIGHLIGHTTEXT))
                    : (g_menuDark ? RGB(255, 255, 255) : GetSysColor(COLOR_MENUTEXT));

            HBRUSH brush = CreateSolidBrush(background);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);

            int iconSize = GetSystemMetrics(SM_CXSMICON);
            int iconX = draw->rcItem.left + Scale(8);
            int iconY = draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top) - iconSize) / 2;
            if (data->icon) {
                DrawIconEx(draw->hDC, iconX, iconY, data->icon, iconSize, iconSize,
                           0, nullptr, DI_NORMAL);
            }

            RECT textRect = draw->rcItem;
            textRect.left += Scale(8) + iconSize + Scale(8);
            textRect.right -= data->hasSubmenu ? Scale(24) : Scale(12);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, foreground);
            HGDIOBJ oldFont = SelectObject(draw->hDC, g_menuFont);
            DrawTextW(draw->hDC, data->text.c_str(), -1, &textRect,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(draw->hDC, oldFont);

            if (data->hasSubmenu) {
                int arrowX = draw->rcItem.right - Scale(14);
                int middleY = (draw->rcItem.top + draw->rcItem.bottom) / 2;
                POINT points[3] = {
                    { arrowX, middleY - Scale(4) },
                    { arrowX + Scale(5), middleY },
                    { arrowX, middleY + Scale(4) }
                };
                HBRUSH arrowBrush = CreateSolidBrush(foreground);
                HGDIOBJ oldBrush = SelectObject(draw->hDC, arrowBrush);
                HGDIOBJ oldPen = SelectObject(draw->hDC, GetStockObject(NULL_PEN));
                Polygon(draw->hDC, points, 3);
                SelectObject(draw->hDC, oldPen);
                SelectObject(draw->hDC, oldBrush);
                DeleteObject(arrowBrush);
            }
            return TRUE;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage) {
                AddTrayIcon(hwnd);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterWindowClasses() {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = g_hInst;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = g_appIcon;
    windowClass.hIconSm = g_appIcon;

    windowClass.lpfnWndProc = MainWndProc;
    windowClass.lpszClassName = L"MFMMainWindow";
    RegisterClassExW(&windowClass);

    windowClass.lpfnWndProc = SettingsWndProc;
    windowClass.lpszClassName = L"MFMSettingsDialog";
    RegisterClassExW(&windowClass);

    windowClass.lpfnWndProc = AboutWndProc;
    windowClass.lpszClassName = L"MFMAboutDialog";
    RegisterClassExW(&windowClass);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_hInst = instance;

    bool diagnosticMenu = false;
    bool diagnosticSettings = false;
    bool diagnosticAbout = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--menu") == 0) {
                diagnosticMenu = true;
            } else if (_wcsicmp(argv[i], L"--settings") == 0) {
                diagnosticSettings = true;
            } else if (_wcsicmp(argv[i], L"--about") == 0) {
                diagnosticAbout = true;
            }
        }
        LocalFree(argv);
    }
    bool diagnostics = diagnosticMenu || diagnosticSettings || diagnosticAbout;

    HANDLE mutex = nullptr;
    if (!diagnostics) {
        mutex = CreateMutexW(nullptr, FALSE, kMutexName);
        if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            return 0;
        }
    }

    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitialized = SUCCEEDED(comResult);

    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);

    HDC screen = GetDC(nullptr);
    if (screen) {
        g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }
    if (g_dpi <= 0) {
        g_dpi = 96;
    }

    InitThemeApi();
    EnsureConfigCreated();
    CreateFonts();

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    g_folderIcon = GetFolderIconCached();

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    if (ExtractIconExW(ExePath().c_str(), 0, &largeIcon, &smallIcon, 1) > 0) {
        if (smallIcon) {
            g_appIcon = smallIcon;
            if (largeIcon) {
                DestroyIcon(largeIcon);
            }
        } else if (largeIcon) {
            g_appIcon = largeIcon;
        }
    }
    if (!g_appIcon) {
        g_appIcon = CopyIcon(g_folderIcon);
    }

    RegisterWindowClasses();
    g_hwndMain = CreateWindowExW(0, L"MFMMainWindow", kAppName, WS_OVERLAPPED,
                                 0, 0, 0, 0, nullptr, nullptr, g_hInst, nullptr);
    if (!g_hwndMain) {
        CleanupIcons();
        if (comInitialized) {
            CoUninitialize();
        }
        if (mutex) {
            CloseHandle(mutex);
        }
        return 1;
    }

    AppSettings settings = LoadSettings();
    SetAppThemeMode(IsDarkTheme(settings.theme));

    if (!diagnostics) {
        AddTrayIcon(g_hwndMain);
    }

    if (diagnosticMenu) {
        RECT work = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        POINT point = {};
        point.x = work.left + (work.right - work.left) / 2;
        point.y = work.top + (work.bottom - work.top) / 2;
        ShowTrayMenu(g_hwndMain, point);
        PostQuitMessage(0);
    } else if (diagnosticSettings) {
        std::vector<std::wstring> folders = LoadFolders();
        if (ShowSettingsDialog(g_hwndMain, settings, folders)) {
            SaveConfig(folders, settings);
        }
        PostQuitMessage(0);
    } else if (diagnosticAbout) {
        ShowAboutDialog(g_hwndMain, settings);
        PostQuitMessage(0);
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    RemoveTrayIcon(g_hwndMain);
    if (g_hwndMain) {
        DestroyWindow(g_hwndMain);
        g_hwndMain = nullptr;
    }
    CleanupIcons();
    HGDIOBJ stockFont = GetStockObject(DEFAULT_GUI_FONT);
    if (g_menuFont && g_menuFont != (HFONT)stockFont) {
        DeleteObject(g_menuFont);
    }
    if (g_uiFont && g_uiFont != (HFONT)stockFont) {
        DeleteObject(g_uiFont);
    }
    g_menuFont = nullptr;
    g_uiFont = nullptr;

    if (comInitialized) {
        CoUninitialize();
    }
    if (mutex) {
        CloseHandle(mutex);
    }
    return 0;
}
