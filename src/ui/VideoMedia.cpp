#include "ui/VideoMedia.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/VisualEditors.h"

#if JUCE_ANDROID
 #include <jni.h>
 #include <android/native_window.h>
 #include <android/native_window_jni.h>
 #include <thread>
 #include <mutex>
 #include <chrono>
#endif

namespace lynx::media
{
namespace
{
// Forward declaration for Android JNI environment accessor
// (defined in platform/AndroidPaths.cpp, returns void* to avoid JNI header dependency in headers)
#if JUCE_ANDROID
extern "C" void* lynx_android_get_jni_env();  // forward declaration using C linkage
#endif
}
// ---------------------------------------------------------------------------------------------------------------
// ffmpeg (the program)
Probe probeWithFfmpeg (const juce::String& path)
{
    Probe p;
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return p;
    juce::ChildProcess process;
    if (process.start (juce::StringArray { ffmpeg.getFullPathName(), "-hide_banner", "-nostdin", "-i", path }, juce::ChildProcess::wantStdErr))
    {
        p.report = process.readAllProcessOutput();
        process.waitForProcessToFinish (2000);
    }
    p.info = video::parseFfmpegReport (p.report.toStdString());
    if (p.info.hasVideo || p.info.hasAudio)
        p.backend = "ffmpeg";
    return p;
}

std::shared_ptr<const video::AudioTrack> decodeTrackWithFfmpeg (const juce::File& file, const video::Info& info)
{
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return nullptr;
    juce::ChildProcess process;
    if (! process.start (juce::StringArray { ffmpeg.getFullPathName(), "-nostdin", "-v", "quiet", "-i", file.getFullPathName(), "-vn", "-sn",
                                             "-ac", "2", "-ar", juce::String ((int) trackRate), "-f", "s16le", "-" },
                         juce::ChildProcess::wantStdOut))
        return nullptr;
    auto track = std::make_shared<video::AudioTrack>();
    track->rate = trackRate;
    if (info.duration > 0.0)
        track->samples.reserve ((std::size_t) (info.duration * trackRate * 2.0) + 4096);
    std::vector<char> buffer ((std::size_t) 1 << 16);
    bool odd = false; // a byte of a sample split between two reads
    char carry = 0;
    for (;;)
    {
        const auto got = process.readProcessOutput (buffer.data(), (int) buffer.size());
        if (got <= 0)
            break;
        int i = 0;
        if (odd)
        {
            track->samples.push_back ((std::int16_t) ((unsigned char) carry | ((unsigned char) buffer[0] << 8)));
            odd = false;
            i = 1;
        }
        for (; i + 1 < got; i += 2)
            track->samples.push_back ((std::int16_t) ((unsigned char) buffer[(std::size_t) i] | ((unsigned char) buffer[(std::size_t) i + 1] << 8)));
        if (i < got)
        {
            carry = buffer[(std::size_t) i];
            odd = true;
        }
    }
    process.waitForProcessToFinish (2000);
    if (track->samples.size() % 2 != 0)
        track->samples.pop_back();
    if (track->samples.empty())
        return nullptr;
    return track;
}

std::mutex probeLock;
std::map<juce::String, Probe> probes;

Probe probe (const juce::String& path)
{
    {
        std::lock_guard<std::mutex> lock (lynx::media::probeLock);
        if (const auto it = lynx::media::probes.find (path); it != lynx::media::probes.end())
            return it->second;
    }
    const juce::File file (path);
    if (! file.existsAsFile())
        return {}; // (not cached: it may appear later)
    Probe p;
    if (auto reader = lynx::media::openNative (file))
    {
        p.info = reader->info();
        p.backend = lynx::media::nativeBackendName();
    }
    if (! p.info.hasVideo)
    {
        // the system could not read the picture: ffmpeg may (a sound-only reading of the system is kept otherwise)
        auto f = lynx::media::probeWithFfmpeg (path);
        if (f.info.hasVideo || ! p.info.hasAudio)
            p = f;
    }
    if (p.backend.isNotEmpty() || lynx::findFfmpeg().existsAsFile())
    {
        std::lock_guard<std::mutex> lock (lynx::media::probeLock);
        lynx::media::probes[path] = p; // (without any decoder not cached: ffmpeg may be located later)
    }
    return p;
}

void clearCaches()
{
    {
        std::lock_guard<std::mutex> lock (lynx::media::probeLock);
        lynx::media::probes.clear();
    }
    std::lock_guard<std::mutex> lock (trackLock);
    tracks.clear();
}

bool canDecodeVideo()
{
    return lynx::media::nativeBackendName().isNotEmpty() || lynx::findFfmpeg().existsAsFile();
}

juce::String ffmpegInstallHelp (bool forExport)
{
    juce::String s = forExport ? tr ("To export with video, install ffmpeg (free), then export again:")
                               : tr ("To read this video, install ffmpeg (free), then load the video again:");
    s << "\n";
#if JUCE_WINDOWS
    s << tr ("Windows: open a terminal and type") << "  winget install Gyan.FFmpeg\n";
#elif JUCE_MAC
    s << tr ("macOS: install Homebrew, then type in a terminal") << "  brew install ffmpeg\n";
#elif JUCE_ANDROID
    s << tr ("Android: ffmpeg is not available on Android; use the system MediaCodec decoder.") << "\n";
#else
    s << "Ubuntu, Debian, Mint:  sudo apt install ffmpeg\n";
    s << "Fedora:  sudo dnf install ffmpeg-free  (" << tr ("every codec: ffmpeg from RPM Fusion") << ")\n";
    s << "Arch, Manjaro:  sudo pacman -S ffmpeg\n";
    s << "openSUSE:  sudo zypper install ffmpeg\n";
#endif
    s << tr ("Or download it from ffmpeg.org and choose \"Locate ffmpeg...\".");
    return s;
}

juce::String cannotReadMessage (const juce::String& path)
{
    const auto native = lynx::media::nativeBackendName();
    if (! lynx::findFfmpeg().existsAsFile())
    {
        if (native.isNotEmpty())
            return tr ("This system cannot read this video file ({0}).", { juce::File (path).getFileExtension().trimCharactersAtStart (".") }) + "\n" + ffmpegInstallHelp();
        return ffmpegInstallHelp();
    }
    return tr ("Neither this system nor ffmpeg could read a video in this file");
}

// ---------------------------------------------------------------------------------------------------------------
std::shared_ptr<const video::AudioTrack> provideTrack (const std::string& path, bool wait, std::function<void()> ready)
{
    const auto file = platform::localFile (path);
    {
        std::lock_guard<std::mutex> lock (trackLock);
        auto& e = tracks[path];
        if (e.done)
            return e.track;
        if (! wait)
        {
            if (! e.pending)
            {
                e.pending = true;
                trackPool().addJob ([path, file, ready] {
                    auto track = decodeTrack (file);
                    {
                        std::lock_guard<std::mutex> l (trackLock);
                        auto& entry = tracks[path];
                        entry.track = track;
                        entry.done = true;
                        entry.pending = false;
                    }
                    if (ready)
                        juce::MessageManager::callAsync (ready);
                });
            }
            return nullptr;
        }
    }
    // exporting: decode now (the export must have the sound)
    auto track = decodeTrack (file);
    std::lock_guard<std::mutex> lock (trackLock);
    auto& e = tracks[path];
    if (! e.done)
    {
        e.track = track;
        e.done = true;
    }
    return e.track;
}

bool extractAudioToWav (const juce::File& video, const juce::File& wav)
{
    if (! video.existsAsFile())
        return false;
    const auto p = probe (video.getFullPathName());
    if (! p.info.hasAudio)
        return false;
    if (p.backend != "ffmpeg")
        if (auto t = lynx::media::decodeNativeAudio (video, trackRate))
        {
            wav.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream().release());
            if (stream == nullptr)
                return false;
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), t->rate, 2, 16, {}, 0));

// ... (rest of file)