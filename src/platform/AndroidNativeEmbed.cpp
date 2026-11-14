// Android implementation of NativeEmbed for plugin editor embedding.
// On Android, there's no HWND / NSView / X11 window to embed into.
// JUCE on Android uses a single GL surface (EGL) for the whole UI.
// Plugin editors that support Android (CLAP with Android window API, or VST3 with
// JUCE's Android wrapper) would need to render into a texture or sub-surface.
//
// For now, this is a stub that returns false for create() and nullptr for handles.
// If/when CLAP Android window API or JUCE Android plugin hosting matures, this
// would be implemented with a SurfaceView / TextureView or EGL surface sharing.

#include "platform/NativeEmbed.h"

#include <android/log.h>

namespace lynx::platform
{
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) {}
    ~Impl() = default;

    bool create()
    {
        __android_log_print (ANDROID_LOG_VERBOSE, "LYNX", "NativeEmbed::create() not implemented on Android");
        return false;
    }

    void* handle() const noexcept { return nullptr; }
    unsigned long x11() const noexcept { return 0; }
    void resized() {}

    juce::Component& owner;
};

NativeEmbed::NativeEmbed() : impl (std::make_unique<Impl> (*this)) {}
NativeEmbed::~NativeEmbed() = default;

bool NativeEmbed::create() { return impl->create(); }
bool NativeEmbed::isCreated() const noexcept { return impl->handle() != nullptr; }
void* NativeEmbed::nativeHandle() const noexcept { return impl->handle(); }
unsigned long NativeEmbed::x11Window() const noexcept { return impl->x11(); }
void NativeEmbed::resized() { impl->resized(); }

const char* NativeEmbed::clapWindowApi() noexcept
{
    // CLAP on Android would use "android" window API (experimental).
    // For now return empty to indicate not supported.
    return "";
}

} // namespace lynx::platform