// Android implementation of real-time thread promotion (lynx::rt namespace).
// On Android, there's no SCHED_FIFO/SCHED_RR for regular apps (requires root or special permissions).
// JUCE's Android audio device uses AAudio/OpenSL ES with low-latency paths and sets
// thread priority to THREAD_PRIORITY_URGENT_AUDIO (nice -16) internally.
// This file provides no-op implementations that return "denied" status.

#include "platform/LinuxRealtime.h"

#include <android/log.h>

namespace lynx::rt
{

namespace
{
std::atomic<Status> g_status { Status::denied };
std::atomic<bool> g_hintShown { false };
} // namespace

void noteAudioThread (bool /*deviceThread*/) noexcept
{
    // On Android, JUCE's audio thread already runs at high priority.
    // Nothing to do here.
}

bool promotePending()
{
    // No real-time promotion available on Android.
    return false;
}

Status status()
{
    return g_status.load();
}

void resetForNewDevice()
{
    g_status.store (Status::denied);
}

juce::String describe()
{
    return "Android: real-time scheduling not available (uses JUCE's high-priority audio thread)";
}

bool shouldShowHint (const juce::File& /*flagFile*/)
{
    // Show hint once per install.
    if (g_hintShown.exchange (true))
        return false;
    return true;
}

} // namespace lynx::rt