// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "capture/wgc_capture.h"

#if defined(_WIN32)

// C++/WinRT — Windows SDK 10.0.17134+ ships these headers.
#include <winrt/base.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>

// Native interop: WGC needs a DXGI device wrapped via
// IInspectable -- helper lives in windows.graphics.directx.direct3d11.interop.h
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <inspectable.h>
#include <ShellScalingApi.h>

#include <cstdio>

namespace tubelight {

namespace winrt_capture = winrt::Windows::Graphics::Capture;
namespace winrt_dx     = winrt::Windows::Graphics::DirectX;
namespace winrt_dx11   = winrt::Windows::Graphics::DirectX::Direct3D11;

using Microsoft::WRL::ComPtr;

// Bridge helper: convert a IDXGIDevice → WinRT IDirect3DDevice. Wraps
// the (non-WinRT) Windows-supplied factory CreateDirect3D11DeviceFromDXGIDevice.
extern "C" HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
    IDXGIDevice* dxgiDevice, IInspectable** graphicsDevice);

namespace {

winrt_dx11::IDirect3DDevice create_winrt_device(ID3D11Device* d3d) {
    ComPtr<IDXGIDevice> dxgi;
    d3d->QueryInterface(IID_PPV_ARGS(&dxgi));
    winrt::com_ptr<::IInspectable> inspect;
    HRESULT hr = ::CreateDirect3D11DeviceFromDXGIDevice(
        dxgi.Get(), inspect.put());
    if (FAILED(hr)) {
        return nullptr;
    }
    return inspect.as<winrt_dx11::IDirect3DDevice>();
}

} // namespace

struct WgcCapture::Impl {
    winrt_dx11::IDirect3DDevice                  winrt_device { nullptr };
    winrt_capture::GraphicsCaptureItem           item         { nullptr };
    winrt_capture::Direct3D11CaptureFramePool    pool         { nullptr };
    winrt_capture::GraphicsCaptureSession        session      { nullptr };
    winrt::event_token                           frame_token  {};
    ID3D11Device*                                d3d_device   = nullptr;

    std::mutex                                   latest_mu;
    ComPtr<ID3D11Texture2D>                      latest_tex;
    int                                          latest_w     = 0;
    int                                          latest_h     = 0;

    std::atomic<uint64_t>                        frames       { 0 };
    std::atomic<bool>                            running      { false };

    void on_frame_arrived(
        const winrt_capture::Direct3D11CaptureFramePool& sender,
        const winrt::Windows::Foundation::IInspectable&)
    {
        // Drain ALL queued frames; we only keep the latest one.
        winrt_capture::Direct3D11CaptureFrame frame { nullptr };
        ComPtr<ID3D11Texture2D> dx_tex;
        int w = 0, h = 0;
        while ((frame = sender.TryGetNextFrame())) {
            auto access = frame.Surface().as<
                Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            access->GetInterface(IID_PPV_ARGS(&dx_tex));
            const auto sz = frame.ContentSize();
            w = sz.Width;
            h = sz.Height;
        }
        if (dx_tex) {
            std::lock_guard<std::mutex> lk(latest_mu);
            latest_tex = dx_tex;
            latest_w   = w;
            latest_h   = h;
            frames.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

WgcCapture::WgcCapture() : impl_(std::make_unique<Impl>()) {}

WgcCapture::~WgcCapture() {
    stop();
}

bool WgcCapture::is_supported() {
    try {
        return winrt_capture::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool WgcCapture::init_for_window(HWND target, ID3D11Device* device) {
    if (!is_supported() || !target || !device) return false;
    try {
        impl_->d3d_device   = device;
        impl_->winrt_device = create_winrt_device(device);
        if (!impl_->winrt_device) {
            std::fprintf(stderr, "[wgc] create_winrt_device failed\n");
            return false;
        }
        auto factory = winrt::get_activation_factory<
            winrt_capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();
        winrt::com_ptr<::IInspectable> item_inspect;
        HRESULT hr = factory->CreateForWindow(
            target,
            winrt::guid_of<winrt_capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(impl_->item)));
        if (FAILED(hr)) {
            std::fprintf(stderr, "[wgc] CreateForWindow HRESULT=0x%lX\n", hr);
            return false;
        }
        return true;
    } catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "[wgc] init_for_window winrt error: %ls\n",
                     e.message().c_str());
        return false;
    }
}

bool WgcCapture::init_for_monitor(HMONITOR monitor, ID3D11Device* device) {
    if (!is_supported() || !monitor || !device) return false;
    try {
        impl_->d3d_device   = device;
        impl_->winrt_device = create_winrt_device(device);
        if (!impl_->winrt_device) {
            std::fprintf(stderr, "[wgc] create_winrt_device failed\n");
            return false;
        }
        auto factory = winrt::get_activation_factory<
            winrt_capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();
        HRESULT hr = factory->CreateForMonitor(
            monitor,
            winrt::guid_of<winrt_capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(impl_->item)));
        if (FAILED(hr)) {
            std::fprintf(stderr, "[wgc] CreateForMonitor HRESULT=0x%lX\n", hr);
            return false;
        }
        return true;
    } catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "[wgc] init_for_monitor winrt error: %ls\n",
                     e.message().c_str());
        return false;
    }
}

bool WgcCapture::start() {
    if (!impl_->item || !impl_->winrt_device) return false;
    if (impl_->running.load()) return true;
    try {
        const auto size = impl_->item.Size();
        // 2 buffers — frames arrive at the source rate; we always
        // overwrite the latest so backpressure isn't critical.
        impl_->pool = winrt_capture::Direct3D11CaptureFramePool::Create(
            impl_->winrt_device,
            winrt_dx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);
        impl_->session = impl_->pool.CreateCaptureSession(impl_->item);

        // FrameArrived runs on a background thread pool — we drain
        // queued frames + stash the latest under a mutex.
        impl_->frame_token = impl_->pool.FrameArrived(
            [impl = impl_.get()](auto&& sender, auto&& args) {
                if (impl) impl->on_frame_arrived(sender, args);
            });

        impl_->session.StartCapture();
        impl_->running.store(true);
        return true;
    } catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "[wgc] start winrt error: %ls\n",
                     e.message().c_str());
        return false;
    }
}

void WgcCapture::stop() {
    if (!impl_ || !impl_->running.load()) return;
    try {
        if (impl_->pool) {
            impl_->pool.FrameArrived(impl_->frame_token);
        }
        if (impl_->session) {
            impl_->session.Close();
            impl_->session = nullptr;
        }
        if (impl_->pool) {
            impl_->pool.Close();
            impl_->pool = nullptr;
        }
        impl_->item = nullptr;
        {
            std::lock_guard<std::mutex> lk(impl_->latest_mu);
            impl_->latest_tex.Reset();
            impl_->latest_w = impl_->latest_h = 0;
        }
        impl_->running.store(false);
    } catch (...) {
        // best-effort shutdown.
    }
}

ComPtr<ID3D11Texture2D> WgcCapture::latest_frame(int& out_w, int& out_h) {
    std::lock_guard<std::mutex> lk(impl_->latest_mu);
    out_w = impl_->latest_w;
    out_h = impl_->latest_h;
    return impl_->latest_tex;
}

uint64_t WgcCapture::frame_count() const {
    return impl_->frames.load(std::memory_order_relaxed);
}

bool WgcCapture::is_running() const {
    return impl_->running.load();
}

} // namespace tubelight

#endif // _WIN32
