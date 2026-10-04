#include "universalfnaf/UI/Renderer.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/WinError.h"

#include <algorithm>
#include <iterator>

namespace universalfnaf {
namespace {

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
};

constexpr float kTransparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};

} // namespace

Renderer::Renderer(ILogger& logger) : logger_(logger) {}

Renderer::~Renderer()
{
    Shutdown();
}

bool Renderer::CreateDevice(std::wstring& error)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;   // required for DirectComposition
#if defined(UNIVERSALFNAF_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)),
                                     D3D11_SDK_VERSION, &device_, &achieved, &context_);

    if (hr == E_INVALIDARG) {
        // D3D_FEATURE_LEVEL_11_1 is rejected by pre-Windows-8 runtimes; retry
        // without it (documented behaviour of D3D11CreateDevice).
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 kFeatureLevels + 1,
                                 static_cast<UINT>(std::size(kFeatureLevels)) - 1,
                                 D3D11_SDK_VERSION, &device_, &achieved, &context_);
    }
#if defined(UNIVERSALFNAF_DEBUG)
    if (FAILED(hr)) {
        // Debug layer unavailable (SDK feature not installed): fall back.
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 kFeatureLevels + 1,
                                 static_cast<UINT>(std::size(kFeatureLevels)) - 1,
                                 D3D11_SDK_VERSION, &device_, &achieved, &context_);
    }
#endif
    if (FAILED(hr)) {
        // Software fallback keeps the UI usable on VMs / RDP sessions.
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                 kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)),
                                 D3D11_SDK_VERSION, &device_, &achieved, &context_);
    }
    if (FAILED(hr)) {
        error = L"D3D11CreateDevice failed: " + FormatHresultMessage(hr);
        return false;
    }

    if (device_.Get() == nullptr || context_.Get() == nullptr) {
        error = L"D3D11CreateDevice returned no device";
        return false;
    }

    LogInfo(logger_, "D3D", Sprintf("device created (feature level 0x%04X)",
                                    static_cast<unsigned>(achieved)));
    return true;
}

bool Renderer::CreateSwapChain(HWND window, std::wstring& error)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        error = L"ID3D11Device does not expose IDXGIDevice";
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        error = L"IDXGIDevice::GetAdapter failed: " + FormatHresultMessage(hr);
        return false;
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = L"IDXGIAdapter::GetParent(IDXGIFactory2) failed: " + FormatHresultMessage(hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width_);
    description.Height = static_cast<UINT>(height_);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.Scaling = DXGI_SCALING_STRETCH;

    if (options_.mode == TransparencyMode::DirectComposition) {
        description.BufferCount = std::max<UINT>(options_.bufferCount, 2u);
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        hr = factory->CreateSwapChainForComposition(device_.Get(), &description, nullptr,
                                                    &swapChain_);
        if (FAILED(hr)) {
            error = L"CreateSwapChainForComposition failed: " + FormatHresultMessage(hr);
            return false;
        }

        hr = ::DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositionDevice_));
        if (FAILED(hr)) {
            error = L"DCompositionCreateDevice failed: " + FormatHresultMessage(hr);
            return false;
        }
        hr = compositionDevice_->CreateTargetForHwnd(window, TRUE, &compositionTarget_);
        if (FAILED(hr)) {
            error = L"IDCompositionDevice::CreateTargetForHwnd failed: " + FormatHresultMessage(hr);
            return false;
        }
        hr = compositionDevice_->CreateVisual(&compositionVisual_);
        if (FAILED(hr)) {
            error = L"IDCompositionDevice::CreateVisual failed: " + FormatHresultMessage(hr);
            return false;
        }
        hr = compositionVisual_->SetContent(swapChain_.Get());
        if (FAILED(hr)) {
            error = L"IDCompositionVisual::SetContent failed: " + FormatHresultMessage(hr);
            return false;
        }
        hr = compositionTarget_->SetRoot(compositionVisual_.Get());
        if (FAILED(hr)) {
            error = L"IDCompositionTarget::SetRoot failed: " + FormatHresultMessage(hr);
            return false;
        }
        hr = compositionDevice_->Commit();
        if (FAILED(hr)) {
            error = L"IDCompositionDevice::Commit failed: " + FormatHresultMessage(hr);
            return false;
        }
    } else {
        // Colour-key path: the bitblt model (DISCARD) is the one that reliably
        // honours LWA_COLORKEY; flip-model swap chains do not.
        description.BufferCount = 1;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        hr = factory->CreateSwapChainForHwnd(device_.Get(), window, &description, nullptr,
                                             nullptr, &swapChain_);
        if (FAILED(hr)) {
            error = L"CreateSwapChainForHwnd failed: " + FormatHresultMessage(hr);
            return false;
        }
    }

    LogInfo(logger_, "D3D", Sprintf("swap chain created: %dx%d mode=%s",
                                    width_, height_,
                                    Utf8FromWide(TransparencyModeName(options_.mode)).c_str()));
    return true;
}

bool Renderer::CreateRenderTarget(std::wstring& error)
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        error = L"IDXGISwapChain::GetBuffer failed: " + FormatHresultMessage(hr);
        return false;
    }
    hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    if (FAILED(hr)) {
        error = L"CreateRenderTargetView failed: " + FormatHresultMessage(hr);
        return false;
    }

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    return true;
}

void Renderer::ReleaseRenderTarget()
{
    renderTarget_.Reset();
}

void Renderer::ReleaseSwapChain()
{
    ReleaseRenderTarget();
    compositionVisual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    swapChain_.Reset();
}

bool Renderer::Initialize(HWND window, int width, int height,
                          const RendererOptions& options, std::wstring& error)
{
    Shutdown();

    if (window == nullptr || width <= 0 || height <= 0) {
        error = L"Renderer::Initialize received an invalid window or size";
        return false;
    }

    window_ = window;
    width_ = width;
    height_ = height;
    options_ = options;

    if (!CreateDevice(error)) {
        Shutdown();
        return false;
    }
    if (!CreateSwapChain(window, error)) {
        Shutdown();
        return false;
    }
    if (!CreateRenderTarget(error)) {
        Shutdown();
        return false;
    }
    return true;
}

void Renderer::Shutdown()
{
    ReleaseSwapChain();
    context_.Reset();
    device_.Reset();
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
    deviceLost_ = false;
}

bool Renderer::Resize(int width, int height, std::wstring& error)
{
    if (!initialized() || width <= 0 || height <= 0) {
        return true;
    }
    if (width == width_ && height == height_) {
        return true;
    }

    ReleaseRenderTarget();

    HRESULT hr = swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                           DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        error = L"IDXGISwapChain::ResizeBuffers failed: " + FormatHresultMessage(hr);
        return false;
    }

    width_ = width;
    height_ = height;
    if (!CreateRenderTarget(error)) {
        return false;
    }
    if (compositionDevice_.Get() != nullptr) {
        compositionDevice_->Commit();
    }
    LogInfo(logger_, "D3D", Sprintf("resized to %dx%d", width_, height_));
    return true;
}

bool Renderer::BeginFrame()
{
    if (!initialized() || renderTarget_.Get() == nullptr) {
        return false;
    }
    ID3D11RenderTargetView* targets[1] = {renderTarget_.Get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->ClearRenderTargetView(renderTarget_.Get(), kTransparentBlack);
    context_->RSSetViewports(1, &viewport_);
    return true;
}

bool Renderer::EndFrame()
{
    if (!initialized()) {
        return false;
    }

    const HRESULT hr = swapChain_->Present(options_.vsync ? 1u : 0u, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        deviceLost_ = true;
        LogError(logger_, "D3D", Sprintf("device lost (0x%08lX); shutting the overlay down",
                                         static_cast<unsigned long>(hr)));
        return false;
    }
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
        LogWarn(logger_, "D3D", Sprintf("Present failed (0x%08lX)", static_cast<unsigned long>(hr)));
    }
    return true;
}

ID3D11ShaderResourceView* Renderer::CreateRgbaTexture(const std::uint8_t* pixels,
                                                      int width, int height)
{
    if (device_.Get() == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels;
    initialData.SysMemPitch = static_cast<UINT>(width) * 4u;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device_->CreateTexture2D(&description, &initialData, &texture);
    if (FAILED(hr)) {
        LogWarn(logger_, "D3D", Sprintf("CreateTexture2D(%dx%d) failed (0x%08lX)",
                                        width, height, static_cast<unsigned long>(hr)));
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = description.Format;
    viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* view = nullptr;
    hr = device_->CreateShaderResourceView(texture.Get(), &viewDescription, &view);
    if (FAILED(hr)) {
        LogWarn(logger_, "D3D", Sprintf("CreateShaderResourceView failed (0x%08lX)",
                                        static_cast<unsigned long>(hr)));
        return nullptr;
    }
    return view;
}

void Renderer::ReleaseTexture(ID3D11ShaderResourceView* view)
{
    if (view != nullptr) {
        view->Release();
    }
}

} // namespace universalfnaf
