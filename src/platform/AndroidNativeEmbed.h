#pragma once

// Android native child view for a plugin's own editor (CLAP, VST3): the one place that knows what a "parent window"
// is on Android — an ANativeWindow (SurfaceView) that JUCE wraps in juce::AndroidComponentPeer. The CLAP window API
// on Android is "android" (matches CLAP_WINDOW_API_ANDROID).

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace lynx::platform
{
class NativeEmbed final : public juce::Component
{
public:
    NativeEmbed();
    ~NativeEmbed() override;

    // Creates the native view once the component is inside a window on screen; false until then (call again from a
    // timer). Safe to call again after it succeeded.
    bool create();
    bool isCreated() const noexcept;

    // The native parent for the plugin: ANativeWindow* (Android).
    void* nativeHandle() const noexcept;

    // The CLAP window API this platform uses ("android"; matches CLAP_WINDOW_API_ANDROID).
    static const char* clapWindowApi() noexcept;

    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lynx::platform