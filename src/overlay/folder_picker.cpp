// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "overlay/folder_picker.h"

#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <vector>

namespace tubelight::overlay {

namespace {

std::string wide_to_utf8(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::vector<char> buf(static_cast<size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf.data(), len, nullptr, nullptr);
    return std::string(buf.data());
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

} // namespace

std::string browse_for_folder(const std::string& title) {
    HRESULT hr = CoInitializeEx(nullptr,
                                 COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool we_initialised = SUCCEEDED(hr);

    std::string result;
    IFileOpenDialog* dlg = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        DWORD options = 0;
        dlg->GetOptions(&options);
        dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                  FOS_PATHMUSTEXIST | FOS_FORCESHOWHIDDEN);

        if (!title.empty()) {
            std::wstring wt = utf8_to_wide(title);
            dlg->SetTitle(wt.c_str());
        }

        // Own the dialog to the overlay window so it appears ABOVE it —
        // the overlay is WS_EX_TOPMOST, and an unowned (Show(nullptr))
        // dialog renders behind a topmost window (and behind the console).
        // An owned dialog is always above its owner in z-order. When the
        // menu is open the overlay is the active/foreground window.
        HWND owner = GetActiveWindow();
        if (!owner) owner = GetForegroundWindow();
        hr = dlg->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = wide_to_utf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (we_initialised) CoUninitialize();
    return result;
}

} // namespace tubelight::overlay

#else // !_WIN32

namespace tubelight::overlay {
std::string browse_for_folder(const std::string&) {
    std::fprintf(stderr,
        "[overlay] folder browse on this platform: pending v1.1 "
        "(GTK / Qt / xdg-desktop-portal binding).\n");
    return {};
}
} // namespace tubelight::overlay

#endif
