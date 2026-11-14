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

// Global storage for JNI-configured paths (set from Java via JNI)
juce::File g_settingsFolder;
juce::File g_cacheFolder;
juce::File g_fontsFolder;
juce::File g_sharedStorageFolder;

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

// JNI setter functions (called from AndroidJni.cpp via JNI)
void setSettingsFolder (const juce::File& f) { g_settingsFolder = f; }
void setCacheFolder (const juce::File& f) { g_cacheFolder = f; }
void setBundledFontsFolder (const juce::File& f) { g_fontsFolder = f; }
void setSharedStorageFolder (const juce::File& f) { g_sharedStorageFolder = f; }
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
void* androidGetJNIEnv()
{
    if (auto* act = androidNativeActivity())
    {
        JNIEnv* env = nullptr;
        act->vm->GetEnv ((void**) &env, JNI_VERSION_1_6);
        return static_cast<void*> (env);
    }
    return nullptr;
}

// Android-specific: the application context (android.app.Application or Activity).
jobject androidGetContext()
{
    if (auto* act = androidNativeActivity())
    {
        JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
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

// --- SAF (Storage Access Framework) file access ---
namespace
{
std::function<void (const juce::String&, bool)> g_fileOpenCallback;
std::function<void (const juce::String&, bool)> g_fileSaveCallback;
std::function<void (const juce::String&, bool)> g_folderPickCallback;
}

void setFileOpenCallback (std::function<void (const juce::String&, bool)> cb) { g_fileOpenCallback = std::move (cb); }
void setFileSaveCallback (std::function<void (const juce::String&, bool)> cb) { g_fileSaveCallback = std::move (cb); }
void setFolderPickCallback (std::function<void (const juce::String&, bool)> cb) { g_folderPickCallback = std::move (cb); }

void openDocument (const juce::String& mimeTypes)
{
    JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
    if (env == nullptr) return;
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;
    jmethodID method = env->GetStaticMethodID (cls, "openDocument", "(Ljava/lang/String;)V");
    if (method == nullptr) return;
    jstring jMime = env->NewStringUTF (mimeTypes.toRawUTF8());
    env->CallStaticVoidMethod (cls, method, jMime);
    env->DeleteLocalRef (jMime);
}

void saveDocument (const juce::String& suggestedName, const juce::String& mimeType)
{
    JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
    if (env == nullptr) return;
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;
    jmethodID method = env->GetStaticMethodID (cls, "saveDocument", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (method == nullptr) return;
    jstring jName = env->NewStringUTF (suggestedName.toRawUTF8());
    jstring jMime = env->NewStringUTF (mimeType.toRawUTF8());
    env->CallStaticVoidMethod (cls, method, jName, jMime);
    env->DeleteLocalRef (jName);
    env->DeleteLocalRef (jMime);
}

void pickFolder ()
{
    JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
    if (env == nullptr) return;
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;
    jmethodID method = env->GetStaticMethodID (cls, "pickFolder", "()V");
    if (method == nullptr) return;
    env->CallStaticVoidMethod (cls, method);
}

juce::String uriToFile (const juce::String& uri, const juce::String& fallbackExtension)
{
    JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
    if (env == nullptr) return juce::String();
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return juce::String();
    jmethodID method = env->GetStaticMethodID (cls, "uriToFile", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr) return juce::String();
    jstring jUri = env->NewStringUTF (uri.toRawUTF8());
    jstring jExt = env->NewStringUTF (fallbackExtension.toRawUTF8());
    jstring result = (jstring) env->CallStaticObjectMethod (cls, method, jUri, jExt);
    env->DeleteLocalRef (jUri);
    env->DeleteLocalRef (jExt);
    if (result == nullptr) return juce::String();
    const char* str = env->GetStringUTFChars (result, nullptr);
    juce::String ret (str);
    env->ReleaseStringUTFChars (result, str);
    env->DeleteLocalRef (result);
    return ret;
}

bool saveFileToUri (const juce::String& localPath, const juce::String& uri)
{
    JNIEnv* env = static_cast<JNIEnv*> (androidGetJNIEnv());
    if (env == nullptr) return false;
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return false;
    jmethodID method = env->GetStaticMethodID (cls, "saveFileToUri", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (method == nullptr) return false;
    jstring jPath = env->NewStringUTF (localPath.toRawUTF8());
    jstring jUri = env->NewStringUTF (uri.toRawUTF8());
    jboolean result = env->CallStaticBooleanMethod (cls, method, jPath, jUri);
    env->DeleteLocalRef (jPath);
    env->DeleteLocalRef (jUri);
    return result;
}

} // namespace lynx::platform