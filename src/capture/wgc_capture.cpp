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

#include <algorithm>
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

ComPtr<ID3D11Device> create_plain_d3d11_device() {
    ComPtr<ID3D11Device> dev;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 fls, 2, D3D11_SDK_VERSION, &dev, nullptr, nullptr))) {
        return nullptr;
    }
    return dev;
}

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
    // The pool delivers a frame backed by one of its (few) buffers. If we
    // hold that surface, the buffer can't be recycled and the pool stops
    // producing after BufferCount frames (the "frozen capture" bug). So the
    // FrameArrived callback only PARKS the newest frame in `pending` (closing
    // any older un-consumed one); the main thread (latest_frame) then COPIES
    // it into one of our own textures and Closes it, returning the pool buffer
    // immediately. Keeping the D3D11 copy on the main thread avoids using the
    // shared D3D11On12 immediate context from two threads.
    winrt_capture::Direct3D11CaptureFrame        pending      { nullptr };

    ComPtr<ID3D11Device>                         wgc_device;     // dedicated plain D3D11 device for WGC
    ComPtr<ID3D11DeviceContext>                  wgc_ctx;        // wgc_device immediate (lazy)
    // Triple-buffered SHARED textures: `shared[i]` lives on wgc_device (the
    // CopyResource target); `opened[i]` is the SAME texture opened on the
    // consumer's D3D11On12 device, so the DX12 pipeline can sample it without a
    // CPU round-trip. latest_tex points into opened[].
    ComPtr<ID3D11Texture2D>                      shared[3];
    ComPtr<ID3D11Texture2D>                      opened[3];
    int                                          widx       = 0;
    int                                          owned_w    = 0;
    int                                          owned_h    = 0;
    DXGI_FORMAT                                  owned_fmt  = DXGI_FORMAT_UNKNOWN;
    // Optional crop (monitor-relative px); w/h 0 = full source. Set/read on the
    // main thread (set_crop + latest_frame).
    int                                          crop_x = 0, crop_y = 0;
    int                                          crop_w = 0, crop_h = 0;
    ComPtr<ID3D11Texture2D>                      latest_tex; // points into opened[]
    int                                          latest_w     = 0;
    int                                          latest_h     = 0;

    std::atomic<uint64_t>                        frames       { 0 };
    std::atomic<bool>                            running      { false };

    void on_frame_arrived(
        const winrt_capture::Direct3D11CaptureFramePool& sender,
        const winrt::Windows::Foundation::IInspectable&)
    {
        try {
            // Drain to the newest queued frame; Close the older ones so their
            // buffers recycle straight away (WGC runs on its own plain device,
            // where recycling works).
            winrt_capture::Direct3D11CaptureFrame newest { nullptr };
            for (;;) {
                auto f = sender.TryGetNextFrame();
                if (!f) break;
                if (newest) newest.Close();
                newest = f;
            }
            if (!newest) return;
            std::lock_guard<std::mutex> lk(latest_mu);
            // Park only the newest for the main thread to copy; drop any older
            // un-consumed one so its buffer recycles.
            if (pending) pending.Close();
            pending = newest;
            frames.fetch_add(1, std::memory_order_relaxed);
        } catch (const winrt::hresult_error& e) {
            std::fprintf(stderr, "[wgc] frame error 0x%08X %ls\n",
                         (unsigned)e.code(), e.message().c_str());
        } catch (...) {}
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
        // WGC frame recycling is broken on a D3D11On12-backed device (pool
        // stops after BufferCount frames). Run WGC on its OWN plain hardware
        // D3D11 device; the captured frame is copied to a shared texture for
        // the 11On12/DX12 consumer.
        impl_->wgc_device   = create_plain_d3d11_device();
        impl_->winrt_device = create_winrt_device(
            impl_->wgc_device ? impl_->wgc_device.Get() : device);
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
        // WGC frame recycling is broken on a D3D11On12-backed device (pool
        // stops after BufferCount frames). Run WGC on its OWN plain hardware
        // D3D11 device; the captured frame is copied to a shared texture for
        // the 11On12/DX12 consumer.
        impl_->wgc_device   = create_plain_d3d11_device();
        impl_->winrt_device = create_winrt_device(
            impl_->wgc_device ? impl_->wgc_device.Get() : device);
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
        //
        // CreateFreeThreaded (NOT Create): FrameArrived must be raised on a
        // system thread-pool thread, independent of any DispatcherQueue. The
        // plain Create() delivers FrameArrived via the *creating thread's*
        // DispatcherQueue — which the overlay's main thread does not have (it
        // pumps a classic Win32 message loop, not a WinRT dispatcher), so only
        // the very first frame ever arrived and the capture appeared frozen.
        // Our on_frame_arrived is already thread-safe (mutex-guarded latest_*).
        impl_->pool = winrt_capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            impl_->winrt_device,
            winrt_dx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            3,   // buffers; recycled on the dedicated wgc_device (works)
            size);
        impl_->session = impl_->pool.CreateCaptureSession(impl_->item);

        // Don't capture the mouse cursor: the OS already draws the real cursor
        // on top of our (topmost) overlay, so capturing it too would show TWO
        // cursors slightly out of sync → they jitter/ghost as the mouse moves
        // (the "vibra cuando muevo el ratón"). Requires Win10 2004+
        // (GraphicsCaptureSession2) — guarded so older builds still run.
        try {
            impl_->session.IsCursorCaptureEnabled(false);
        } catch (...) {
            std::fprintf(stderr, "[wgc] IsCursorCaptureEnabled unavailable (pre-2004?)\n");
        }

        // FrameArrived drives continuous production (a CreateFreeThreaded pool
        // delivers on a system thread-pool thread). The handler drains + keeps
        // the latest texture and recycles the previous buffer (see Impl).
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
        {
            std::lock_guard<std::mutex> lk(impl_->latest_mu);
            impl_->latest_tex.Reset();
            impl_->latest_w = impl_->latest_h = 0;
            if (impl_->pending) { impl_->pending.Close(); impl_->pending = nullptr; }
            for (auto& o : impl_->shared) o.Reset();
            for (auto& o : impl_->opened) o.Reset();
            impl_->wgc_ctx.Reset();
            impl_->owned_w = impl_->owned_h = 0;
            impl_->owned_fmt = DXGI_FORMAT_UNKNOWN;
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
        impl_->running.store(false);
    } catch (...) {
        // best-effort shutdown.
    }
}

ComPtr<ID3D11Texture2D> WgcCapture::latest_frame(int& out_w, int& out_h) {
    // Take the parked frame (if any) under the lock, then do the GPU copy
    // outside the lock on this (main) thread.
    winrt_capture::Direct3D11CaptureFrame frame { nullptr };
    {
        std::lock_guard<std::mutex> lk(impl_->latest_mu);
        frame = std::move(impl_->pending);
        impl_->pending = nullptr;
    }
    if (frame && impl_->d3d_device && impl_->wgc_device) {
        ComPtr<ID3D11Texture2D> src;
        auto access = frame.Surface().as<
            Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        access->GetInterface(IID_PPV_ARGS(&src));
        const auto sz = frame.ContentSize();
        if (src) {
            D3D11_TEXTURE2D_DESC sd{};
            src->GetDesc(&sd);
            if (!impl_->wgc_ctx) impl_->wgc_device->GetImmediateContext(&impl_->wgc_ctx);
            // Target texture size = crop size if cropping, else the full source.
            const bool crop = (impl_->crop_w > 0 && impl_->crop_h > 0);
            const int tw = crop ? std::min(impl_->crop_w, (int)sd.Width)  : (int)sd.Width;
            const int th = crop ? std::min(impl_->crop_h, (int)sd.Height) : (int)sd.Height;
            // (Re)create the triple-buffered SHARED textures if size/format
            // changed: created on wgc_device with MISC_SHARED, then opened on
            // the consumer's d3d_device by shared handle. (Only a SIZE change
            // reallocates — a moving window only shifts the copy box.)
            if (impl_->owned_w != tw || impl_->owned_h != th ||
                impl_->owned_fmt != sd.Format) {
                for (auto& o : impl_->shared) o.Reset();
                for (auto& o : impl_->opened) o.Reset();
                D3D11_TEXTURE2D_DESC od{};
                od.Width  = (UINT)tw;  od.Height = (UINT)th;
                od.MipLevels = 1;      od.ArraySize = 1;
                od.Format = sd.Format; od.SampleDesc.Count = 1;
                od.Usage  = D3D11_USAGE_DEFAULT;
                od.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                od.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                bool ok = true;
                for (int i = 0; i < 3 && ok; ++i) {
                    if (FAILED(impl_->wgc_device->CreateTexture2D(&od, nullptr,
                                                                  &impl_->shared[i]))) {
                        ok = false; break;
                    }
                    ComPtr<IDXGIResource> dxgi;
                    HANDLE h = nullptr;
                    if (FAILED(impl_->shared[i].As(&dxgi)) ||
                        FAILED(dxgi->GetSharedHandle(&h)) || !h ||
                        FAILED(impl_->d3d_device->OpenSharedResource(
                            h, IID_PPV_ARGS(&impl_->opened[i])))) {
                        ok = false; break;
                    }
                }
                if (ok) {
                    impl_->owned_w = tw; impl_->owned_h = th;
                    impl_->owned_fmt = sd.Format; impl_->widx = 0;
                } else {
                    std::fprintf(stderr, "[wgc] shared texture setup failed\n");
                    for (auto& o : impl_->shared) o.Reset();
                    for (auto& o : impl_->opened) o.Reset();
                }
            }
            const int i = impl_->widx;
            if (impl_->shared[i] && impl_->opened[i] && impl_->wgc_ctx) {
                if (crop) {
                    // Clamp the box inside the source so off-screen window edges
                    // don't read out of bounds.
                    int bx = std::max(0, std::min(impl_->crop_x, (int)sd.Width  - tw));
                    int by = std::max(0, std::min(impl_->crop_y, (int)sd.Height - th));
                    D3D11_BOX box{ (UINT)bx, (UINT)by, 0,
                                   (UINT)(bx + tw), (UINT)(by + th), 1 };
                    impl_->wgc_ctx->CopySubresourceRegion(
                        impl_->shared[i].Get(), 0, 0, 0, 0, src.Get(), 0, &box);
                } else {
                    impl_->wgc_ctx->CopyResource(impl_->shared[i].Get(), src.Get());
                }
                impl_->wgc_ctx->Flush();   // publish to the shared surface
                std::lock_guard<std::mutex> lk(impl_->latest_mu);
                impl_->latest_tex = impl_->opened[i];
                impl_->latest_w   = tw;
                impl_->latest_h   = th;
                impl_->widx = (i + 1) % 3;
            }
        }
        // Drop the pool surface before Close() so the buffer recycles.
        src.Reset();
        frame.Close();
    }
    std::lock_guard<std::mutex> lk(impl_->latest_mu);
    out_w = impl_->latest_w;
    out_h = impl_->latest_h;
    return impl_->latest_tex;
}

void WgcCapture::set_crop(int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lk(impl_->latest_mu);
    impl_->crop_x = x; impl_->crop_y = y;
    impl_->crop_w = (w > 0 ? w : 0);
    impl_->crop_h = (h > 0 ? h : 0);
}

uint64_t WgcCapture::frame_count() const {
    return impl_->frames.load(std::memory_order_relaxed);
}

bool WgcCapture::is_running() const {
    return impl_->running.load();
}

} // namespace tubelight

#endif // _WIN32
