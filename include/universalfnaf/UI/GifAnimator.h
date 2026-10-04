#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: UI/GifAnimator.h
//  GIF decoding and playback for the decorative animation in the top-left
//  corner of the overlay.
//
//  Decoding uses GDI+ (Windows SDK, no third-party binary). Every frame is
//  converted once into a straight-alpha RGBA8 buffer; the renderer then only
//  has to upload textures, so playback costs no per-frame decoding.
//
//  Known limitation (documented on purpose): GDI+ does not expose the GIF
//  "disposal method". Frames are therefore composited cumulatively, which is
//  correct for the usual disposal values 0/1 but can leave trails in GIFs that
//  use disposal value 2 (restore to background).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace universalfnaf {

class GdiPlusSession;
class ILogger;

struct GifFrame {
    std::vector<std::uint8_t> rgba;    // RGBA8, top-down, straight alpha
    int delayMilliseconds = 100;
};

class GifAnimator {
public:
    GifAnimator(const GdiPlusSession& gdiPlus, ILogger& logger);
    ~GifAnimator();

    GifAnimator(const GifAnimator&) = delete;
    GifAnimator& operator=(const GifAnimator&) = delete;

    // Decodes every frame. On failure the animator stays empty and `error`
    // describes why; the previous animation is dropped.
    bool Load(const std::wstring& path, std::wstring& error);
    void Clear();

    // Advances playback; call once per frame with the frame delta in seconds.
    void Update(double deltaSeconds);

    void SetPlaying(bool playing) noexcept { playing_ = playing; }
    bool playing() const noexcept { return playing_; }
    void Restart() noexcept;

    const GifFrame* CurrentFrame() const noexcept;
    bool loaded() const noexcept { return !frames_.empty(); }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    std::size_t frameCount() const noexcept { return frames_.size(); }
    std::size_t currentIndex() const noexcept { return index_; }
    const std::wstring& path() const noexcept { return path_; }

private:
    const GdiPlusSession& gdiPlus_;
    ILogger& logger_;

    std::vector<GifFrame> frames_;
    std::wstring path_;
    std::size_t index_ = 0;
    double accumulatorMilliseconds_ = 0.0;
    int width_ = 0;
    int height_ = 0;
    bool playing_ = true;
};

} // namespace universalfnaf
