#pragma once
// Real-time scheduling for LYNX's audio threads on Android (round 185a): Android's AudioManager low-latency path
// (AAudio / OpenSL ES, selected by JUCE at run time) runs its callback on a high-priority thread managed by the OS.
// LYNX does not (and cannot) promote threads through SCHED_FIFO / RealtimeKit / portal here; the OS controls the
// audio thread priority and the foreground app gets it when it holds the audio focus. This file provides the same
// API as src/platform/LinuxRealtime.h so the rest of the code compiles unchanged.

#include <juce_core/juce_core.h>

namespace lynx::rt
{
enum class Status
{
    unknown,         // no audio thread seen yet
    managed,         // the OS manages the audio thread priority (AAudio low-latency, or OpenSL ES fast path)
    denied           // the audio thread runs at normal priority (OpenSL ES fallback without fast mixer)
};

// From an audio callback (deviceThread) or a worker thread: remembers the calling thread so the message thread can
// promote it. On Android this is a no-op: the OS manages the audio thread priority.
inline void noteAudioThread (bool = true) noexcept {}

// Message thread (a timer): promotes the threads noted since the last call. On Android this is a no-op; always
// returns false (nothing changed).
inline bool promotePending() { return false; }

// The outcome for the device's audio thread (the first thread noted after resetForNewDevice()).
inline Status status() { return Status::managed; }

// A new audio device opened: its callback thread will be noted again. No-op on Android.
inline void resetForNewDevice() {}

// "managed"-style detail for the Settings page.
inline juce::String describe() { return "Android audio (OS managed)"; }

// The one-time hint when real-time was denied: never shown on Android (no user-actionable control).
inline bool shouldShowHint (const juce::File&) { return false; }
} // namespace lynx::rt