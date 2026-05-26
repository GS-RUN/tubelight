// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// tubelight_inject.exe — injects tubelight_backend.dll into a target process.
//
// Usage:
//   tubelight_inject.exe --pid <PID> [--dll <path-to-backend.dll>]
//
// Mechanism: OpenProcess → VirtualAllocEx → WriteProcessMemory → CreateRemoteThread
// pointing at kernel32!LoadLibraryA. This is the standard "stable" injection
// technique (the same pattern ReShade and most overlays use).

#if defined(_WIN32)

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

bool inject(DWORD pid, const std::string& dll_path) {
    HANDLE proc = ::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                FALSE, pid);
    if (!proc) {
        std::fprintf(stderr, "OpenProcess(%lu) failed: GLE=%lu\n", pid, ::GetLastError());
        return false;
    }

    const size_t cb = dll_path.size() + 1;
    LPVOID remote = ::VirtualAllocEx(proc, nullptr, cb, MEM_COMMIT, PAGE_READWRITE);
    if (!remote) {
        std::fprintf(stderr, "VirtualAllocEx failed: GLE=%lu\n", ::GetLastError());
        ::CloseHandle(proc);
        return false;
    }

    SIZE_T written = 0;
    if (!::WriteProcessMemory(proc, remote, dll_path.c_str(), cb, &written) || written != cb) {
        std::fprintf(stderr, "WriteProcessMemory failed: GLE=%lu\n", ::GetLastError());
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        ::CloseHandle(proc);
        return false;
    }

    HMODULE k32 = ::GetModuleHandleA("kernel32.dll");
    FARPROC load = ::GetProcAddress(k32, "LoadLibraryA");
    if (!load) {
        std::fprintf(stderr, "Could not resolve LoadLibraryA\n");
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        ::CloseHandle(proc);
        return false;
    }

    HANDLE thread = ::CreateRemoteThread(proc, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load), remote, 0, nullptr);
    if (!thread) {
        std::fprintf(stderr, "CreateRemoteThread failed: GLE=%lu\n", ::GetLastError());
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        ::CloseHandle(proc);
        return false;
    }

    ::WaitForSingleObject(thread, INFINITE);
    DWORD exit_code = 0;
    ::GetExitCodeThread(thread, &exit_code);
    if (exit_code == 0) {
        std::fprintf(stderr, "Remote LoadLibraryA returned NULL — target couldn't load the DLL\n");
        ::CloseHandle(thread);
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        ::CloseHandle(proc);
        return false;
    }

    ::CloseHandle(thread);
    ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    ::CloseHandle(proc);
    return true;
}

void print_help() {
    std::printf("tubelight_inject — inject tubelight_backend.dll into a running process\n"
                "\n"
                "Usage:\n"
                "  tubelight_inject.exe --pid <PID> [--dll <absolute-path-to-backend.dll>]\n"
                "\n"
                "Defaults:\n"
                "  --dll  Resolves to tubelight_backend.dll in the same directory as the\n"
                "         injector executable.\n");
}

} // namespace

int main(int argc, char** argv) {
    DWORD pid = 0;
    std::string dll_path;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h") { print_help(); return 0; }
        if (a == "--pid" && i + 1 < argc) {
            pid = static_cast<DWORD>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--dll" && i + 1 < argc) {
            dll_path = argv[++i];
        }
    }

    if (pid == 0) {
        print_help();
        return 1;
    }

    if (dll_path.empty()) {
        char self[MAX_PATH] = {};
        ::GetModuleFileNameA(nullptr, self, MAX_PATH);
        dll_path = (fs::path(self).parent_path() / "tubelight_backend.dll").string();
    }

    if (!fs::exists(dll_path)) {
        std::fprintf(stderr, "DLL not found: %s\n", dll_path.c_str());
        return 2;
    }

    std::printf("[tubelight] injecting %s into PID %lu...\n", dll_path.c_str(), pid);
    if (!inject(pid, dll_path)) {
        std::fprintf(stderr, "Injection failed.\n");
        return 3;
    }
    std::printf("[tubelight] injection succeeded.\n");
    return 0;
}

#else
int main() { return 0; }
#endif
