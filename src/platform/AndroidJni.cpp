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

#else

// Stubs for non-Android builds (the symbols are referenced by the build but never called).
extern "C" void Java_com_ka1s_lynx_JuceApp_setNativePaths (void*, void*, void*, void*, void*) {}
extern "C" void Java_com_ka1s_lynx_JuceApp_setSharedStorage (void*, void*, void*) {}
extern "C" void Java_com_ka1s_lynx_JuceApp_nativeDestroy (void*, void*) {}

#endif // JUCE_ANDROID