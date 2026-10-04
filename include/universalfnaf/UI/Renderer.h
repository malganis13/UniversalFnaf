#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: UI/Renderer.h
//
//  DirectX 11 renderer for a per-pixel-alpha, click-through overlay window.
//
//  Two documented transparency paths are implemented:
//
//   1. DirectComposition (default, best quality)
//        swap chain created with CreateSwapChainForComposition(),
//        DXGI_ALPHA_MODE_PREMULTIPLIED, presented through an
//        IDCompositionVisual bound to the window. DWM composites it with true
//        per-pixel alpha. The window must NOT have WS_EX_LAYERED.
//
//   2. Colour-key layered window (compatibility fallback)
//        classic WS_EX_LAYERED + SetLayeredWindowAttributes(hwnd, key, 0,
//        LWA_COLORKEY) with a bitblt-model swap chain. Everything painted in
//        the key colour (pure black) is transparent; alpha gradients are not
//        supported, so anti-aliased edges look hard.
//
//  Neither path renders into, reads from, or hooks any other window's surface.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "universalfnaf/Common/TransparencyMode.h"

#include <cstdint>
#include <string>

namespace universalfnaf {

class ILogger;

struct RendererOptions {
    TransparencyMode mode = TransparencyMode::DirectComposition;
    std::uint32_t colorKey = 0x00000000u;   // pure black
    bool vsync = true;
    UINT bufferCount = 2;
};

class Renderer {
public:
    explicit Renderer(ILogger& logger);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(HWND window, int width, int height,
                    const RendererOptions& options, std::wstring& error);
    void Shutdown();

    bool Resize(int width, int height, std::wstring& error);

    // Clears the swap-chain buffer to fully transparent black.
    bool BeginFrame();
    // Presents. false means the device was lost/removed and the app must exit.
    bool EndFrame();

    bool initialized() const noexcept
    {
        return device_.Get() != nullptr && swapChain_.Get() != nullptr;
    }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    const RendererOptions& options() const noexcept { return options_; }

    ID3D11Device* device() const noexcept { return device_.Get(); }
    ID3D11DeviceContext* context() const noexcept { return context_.Get(); }

    // Uploads a straight-alpha RGBA8 buffer. Caller owns the returned view and
    // must hand it back through ReleaseTexture().
    ID3D11ShaderResourceView* CreateRgbaTexture(const std::uint8_t* pixels, int width, int height);
    void ReleaseTexture(ID3D11ShaderResourceView* view);

private:
    bool CreateDevice(std::wstring& error);
    bool CreateSwapChain(HWND window, std::wstring& error);
    bool CreateRenderTarget(std::wstring& error);
    void ReleaseRenderTarget();
    void ReleaseSwapChain();

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ILogger& logger_;
    RendererOptions options_{};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    D3D11_VIEWPORT viewport_{};

    ComPtr<IDCompositionDevice> compositionDevice_;
    ComPtr<IDCompositionTarget> compositionTarget_;
    ComPtr<IDCompositionVisual> compositionVisual_;

    HWND window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool deviceLost_ = false;
};

} // namespace universalfnaf
