// Android implementation of lynx::platform path functions.
// On Android, all user data lives inside the app's private data directory.
// The settings folder is <data-dir>/files/LYNX (created on first use).
// Caches go to <cache-dir>/LYNX/<name>.
// Documents go to the public Documents/LYNX (MediaStore) or the app's private Documents folder.

#include "platform/Paths.h"
#include "core/TestProfile.h"

#include <jni.h>
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

namespace lynx::platform
{
namespace
{
// The ANativeActivity* we get from JUCE's Android startup (Main.cpp calls android_main).
// JUCE stores it in juce::android::getNativeActivity().
extern ANativeActivity* androidNativeActivity();

AAssetManager* assetManager()
{
    if (auto* act = androidNativeActivity())
        return act->assetManager;
    return nullptr;
}

// Returns the app's internal files directory (context.getFilesDir()).
juce::File internalFilesDir()
{
    if (auto* act = androidNativeActivity())
    {
        // The ANativeActivity has internalDataPath pointing to the app's private data dir.
        // The files dir is <data-dir>/files.
        juce::File dataDir (act->internalDataPath);
        return dataDir.getChildFile ("files");
    }
    return juce::File();
}

// Returns the app's cache directory (context.getCacheDir()).
juce::File cacheDir()
{
    if (auto* act = androidNativeActivity())
    {
        juce::File dataDir (act->internalDataPath);
        return dataDir.getChildFile ("cache");
    }
    return juce::File();
}

// Returns the app's external files directory (context.getExternalFilesDir(null)).
// This is on shared storage but app-specific (deleted when app uninstalled).
// May be null if external storage is not available.
juce::File externalFilesDir()
{
    if (auto* act = androidNativeActivity())
    {
        // externalDataPath is set by the framework when available.
        if (act->externalDataPath != nullptr && act->externalDataPath[0] != '\0')
            return juce::File (act->externalDataPath);
    }
    return juce::File();
}

// Returns the public Documents directory via MediaStore (API 29+).
// For older API, falls back to Environment.getExternalStoragePublicDirectory.
juce::File publicDocumentsDir()
{
    // On Android 10+ (API 29), direct file access to shared storage is restricted.
    // We use the MediaStore API via JNI. For simplicity, we use the app's private
    // Documents folder which doesn't need special permissions.
    if (auto* act = androidNativeActivity())
    {
        juce::File filesDir = internalFilesDir();
        return filesDir.getChildFile ("Documents");
    }
    return juce::File();
}
} // namespace

juce::File settingsFolder()
{
    if (const auto profile = profileOverride(); profile != juce::File())
    {
        auto folder = profile.getChildFile ("LYNX");
        folder.createDirectory();
        return folder;
    }

    // Default: <internal-files-dir>/LYNX
    auto folder = internalFilesDir().getChildFile ("LYNX");
    folder.createDirectory();
    return folder;
}

juce::File settingsFile (const juce::String& relativePath)
{
    return settingsFolder().getChildFile (relativePath);
}

juce::File cacheFolder (const juce::String& name)
{
    if (const auto profile = profileOverride(); profile != juce::File())
    {
        auto folder = profile.getChildFile ("cache").getChildFile (name);
        folder.createDirectory();
        return folder;
    }

    auto folder = cacheDir().getChildFile ("LYNX").getChildFile (name);
    folder.createDirectory();
    return folder;
}

juce::File documentsFolder()
{
    if (const auto profile = profileOverride(); profile != juce::File())
        return profile.getChildFile ("Documents");

    // Use the app's private Documents folder (no special permissions needed).
    return internalFilesDir().getChildFile ("Documents");
}

juce::FileSearchPath standardPluginFolders()
{
    juce::FileSearchPath path;
    // Android doesn't have system-wide VST/CLAP folders like desktop.
    // Plugins would be bundled in the APK or loaded from the app's private directory.
    // For now, return empty - plugin hosting on Android is not the primary target.
    return path;
}

juce::String pluginFormatNames()
{
    // On Android we only support CLAP (cross-platform) if built with LYNX_BUILD_PLUGINS.
    return "CLAP";
}

juce::File localFile (const juce::String& path)
{
    // On Android, paths from FL projects use backslashes. Convert to forward slashes.
    // Also, the case-sensitive filesystem means we need to match case.
    juce::String p = path.replaceCharacter ('\\', '/');
    juce::File f (p);
    if (f.exists())
        return f;

    // Try case-insensitive match by iterating the parent directory.
    auto parent = f.getParentDirectory();
    if (parent.exists())
    {
        const auto targetName = f.getFileName();
        for (auto& entry : juce::RangedDirectoryIterator (parent, false, "*", juce::File::findFiles))
        {
            if (entry.getFile().getFileName().equalsIgnoreCase (targetName))
                return entry.getFile();
        }
    }
    return f; // return the original (may not exist)
}

juce::File localFile (const std::string& utf8Path)
{
    return localFile (juce::String::fromUTF8 (utf8Path.c_str()));
}

bool relaunchFromAsciiPath (int& exitCode)
{
    // Android paths are always UTF-8 and don't have the 8.3 short name issue.
    (void) exitCode;
    return false;
}

// Android-specific: access to the AssetManager for reading bundled assets (fonts, etc.)
AAssetManager* androidAssetManager()
{
    return assetManager();
}

// Android-specific: the JNIEnv for the current thread (attaches if needed).
JNIEnv* androidGetJNIEnv()
{
    if (auto* act = androidNativeActivity())
    {
        JNIEnv* env = nullptr;
        act->vm->GetEnv ((void**) &env, JNI_VERSION_1_6);
        return env;
    }
    return nullptr;
}

// Android-specific: the application context (android.app.Application or Activity).
jobject androidGetContext()
{
    if (auto* act = androidNativeActivity())
    {
        JNIEnv* env = androidGetJNIEnv();
        if (env != nullptr)
            return env->NewGlobalRef (act->clazz);
    }
    return nullptr;
}
// Touch / mobile mode: on Android we default to true (larger hit targets, on-screen keyboard, 125% UI scale).
// The user can toggle it in Settings.
namespace
{
bool g_touchMode = true;
}

bool isTouchMode() { return g_touchMode; }
void setTouchMode (bool on) { g_touchMode = on; }

} // namespace lynx::platform