// JNI glue for Android (called from JUCE's generated Java code).
// JUCE 8 generates a Java Application subclass (JuceApp) that calls into native code on startup.
// This file provides the native entry points to set the Android paths (files dir, cache dir, assets).

#include "platform/AndroidPaths.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_ANDROID
 #include <jni.h>
 #include <android/native_activity.h>

// Called from Java (JuceApp.onCreate) to set the app's private directories and assets folder.
// The Java side passes: filesDir (Context.getFilesDir()), cacheDir (Context.getCacheDir()),
// assetsDir (a folder in assets we can read fonts from).
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_JuceApp_setNativePaths (JNIEnv* env, jobject /*thiz*/,
                                           jstring filesDir, jstring cacheDir, jstring assetsDir)
{
    if (filesDir == nullptr || cacheDir == nullptr)
        return;

    const char* files = env->GetStringUTFChars (filesDir, nullptr);
    const char* cache = env->GetStringUTFChars (cacheDir, nullptr);

    lynx::platform::setSettingsFolder (juce::File (files));
    lynx::platform::setCacheFolder (juce::File (cache));

    if (assetsDir != nullptr)
    {
        const char* assets = env->GetStringUTFChars (assetsDir, nullptr);
        lynx::platform::setBundledFontsFolder (juce::File (assets));
        env->ReleaseStringUTFChars (assetsDir, assets);
    }

    env->ReleaseStringUTFChars (filesDir, files);
    env->ReleaseStringUTFChars (cacheDir, cache);
}

// Called from Java (JuceApp.onRequestPermissionsResult) when the user grants storage permission.
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_JuceApp_setSharedStorage (JNIEnv* env, jobject /*thiz*/, jstring path)
{
    if (path == nullptr)
        return;

    const char* p = env->GetStringUTFChars (path, nullptr);
    lynx::platform::setSharedStorageFolder (juce::File (p));
    env->ReleaseStringUTFChars (path, p);
}

// Optional: called from Java when the app is about to be destroyed.
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_JuceApp_nativeDestroy (JNIEnv*, jobject)
{
    // The app is shutting down; JUCE handles cleanup.
}

// --- SAF File Access (called from com.ka1s.lynx.file.FileAccessHelper) ---

// Global callback for file operations
namespace lynx::platform
{
    static std::function<void (const juce::String&, bool)> g_fileOpenCallback;
    static std::function<void (const juce::String&, bool)> g_fileSaveCallback;
    static std::function<void (const juce::String&, bool)> g_folderPickCallback;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_nativeOnFileOpened (JNIEnv* env, jobject /*thiz*/,
                                                              jstring uri, jstring displayName, jboolean success)
{
    if (uri == nullptr) {
        if (lynx::platform::g_fileOpenCallback)
            lynx::platform::g_fileOpenCallback (juce::String(), false);
        return;
    }
    const char* uriStr = env->GetStringUTFChars (uri, nullptr);
    const char* nameStr = env->GetStringUTFChars (displayName, nullptr);
    lynx::platform::g_fileOpenCallback (juce::String (uriStr), success);
    env->ReleaseStringUTFChars (uri, uriStr);
    env->ReleaseStringUTFChars (displayName, nameStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_nativeOnFileSaved (JNIEnv* env, jobject /*thiz*/,
                                                             jstring uri, jstring displayName, jboolean success)
{
    if (uri == nullptr) {
        if (lynx::platform::g_fileSaveCallback)
            lynx::platform::g_fileSaveCallback (juce::String(), false);
        return;
    }
    const char* uriStr = env->GetStringUTFChars (uri, nullptr);
    const char* nameStr = env->GetStringUTFChars (displayName, nullptr);
    lynx::platform::g_fileSaveCallback (juce::String (uriStr), success);
    env->ReleaseStringUTFChars (uri, uriStr);
    env->ReleaseStringUTFChars (displayName, nameStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_nativeOnFolderPicked (JNIEnv* env, jobject /*thiz*/,
                                                                jstring uri, jstring displayName, jboolean success)
{
    if (uri == nullptr) {
        if (lynx::platform::g_folderPickCallback)
            lynx::platform::g_folderPickCallback (juce::String(), false);
        return;
    }
    const char* uriStr = env->GetStringUTFChars (uri, nullptr);
    const char* nameStr = env->GetStringUTFChars (displayName, nullptr);
    lynx::platform::g_folderPickCallback (juce::String (uriStr), success);
    env->ReleaseStringUTFChars (uri, uriStr);
    env->ReleaseStringUTFChars (displayName, nameStr);
}

// Called from C++ to open a document via SAF
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_openDocument (JNIEnv* env, jobject /*thiz*/,
                                                        jstring mimeTypes, jobject callbackObj)
{
    // The actual implementation is in FileAccessHelper.java static methods
    // This is called from C++ via JNI to trigger the document picker
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;

    jmethodID method = env->GetStaticMethodID (cls, "openDocument", "(Ljava/lang/String;)V");
    if (method == nullptr) return;

    env->CallStaticVoidMethod (cls, method, mimeTypes);
}

// Called from C++ to save a document via SAF
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_saveDocument (JNIEnv* env, jobject /*thiz*/,
                                                        jstring suggestedName, jstring mimeType)
{
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;

    jmethodID method = env->GetStaticMethodID (cls, "saveDocument", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (method == nullptr) return;

    env->CallStaticVoidMethod (cls, method, suggestedName, mimeType);
}

// Called from C++ to pick a folder via SAF
extern "C" JNIEXPORT void JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_pickFolder (JNIEnv* env, jobject /*thiz*/)
{
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return;

    jmethodID method = env->GetStaticMethodID (cls, "pickFolder", "()V");
    if (method == nullptr) return;

    env->CallStaticVoidMethod (cls, method);
}

// Called from C++ to convert content:// URI to local file
extern "C" JNIEXPORT jstring JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_uriToFile (JNIEnv* env, jobject /*thiz*/,
                                                     jstring uri, jstring fallbackExtension)
{
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return nullptr;

    jmethodID method = env->GetStaticMethodID (cls, "uriToFile", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr) return nullptr;

    return (jstring) env->CallStaticObjectMethod (cls, method, uri, fallbackExtension);
}

// Called from C++ to save local file to content:// URI
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ka1s_lynx_file_FileAccessHelper_saveFileToUri (JNIEnv* env, jobject /*thiz*/,
                                                         jstring localPath, jstring uri)
{
    jclass cls = env->FindClass ("com/ka1s/lynx/file/FileAccessHelper");
    if (cls == nullptr) return JNI_FALSE;

    jmethodID method = env->GetStaticMethodID (cls, "saveFileToUri", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (method == nullptr) return JNI_FALSE;

    return env->CallStaticBooleanMethod (cls, method, localPath, uri);
}

#else

// Stubs for non-Android builds (the symbols are referenced by the build but never called).
extern "C" void Java_com_ka1s_lynx_JuceApp_setNativePaths (void*, void*, void*, void*, void*) {}
extern "C" void Java_com_ka1s_lynx_JuceApp_setSharedStorage (void*, void*, void*) {}
extern "C" void Java_com_ka1s_lynx_JuceApp_nativeDestroy (void*, void*) {}

#endif // JUCE_ANDROID