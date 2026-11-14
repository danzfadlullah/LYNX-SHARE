#pragma once

// Android paths (docs/PLATFORMS.md, Android section). On Android there is no XDG layout and no Documents folder the
// way desktops have one: LYNX keeps its settings and caches in the app's private data dir (Context.getFilesDir /
// getCacheDir), and reads/writes projects through the Storage Access Framework (SAF) document trees the user
// grants (engine/ProjectFile.cpp asks for those trees). Everything else asks for "the settings folder" and never
// spells out a path.

#include <juce_core/juce_core.h>

namespace lynx::platform
{
// The app's private data directory (Context.getFilesDir): settings, autosave, waveform cache, bundled fonts.
juce::File settingsFolder();
juce::File cacheFolder (const juce::String& name);

// The bundled fonts live in the APK assets (resources/fonts); the path is filled in by the JNI glue from the
// Application's getFilesDir()/assets, so the rest of the code can treat them like any other file.
juce::File bundledFontsFolder();

// A file or sub-folder inside the settings folder, e.g. settingsFile ("autosave").
juce::File settingsFile (const juce::String& relativePath);

// LYNX_PROFILE_DIR (core/TestProfile.h): the folder that replaces every user-data location, or an empty File.
juce::File profileOverride();
// LYNX_TEST_MODE: no prompts (they are logged to test-mode.log in the settings folder instead).
bool testMode();
void testModeLog (const juce::String& what);

// The user's shared storage (Environment.getExternalStorageDirectory, /sdcard or /storage/emulated/0): where LYNX
// looks for projects the user put there and where it offers to save. Empty when the permission is not granted.
juce::File sharedStorageFolder();

// A file named by a path stored in a project or written by the FL import. On Android the path is almost always a
// content:// URI or a path under /storage; the exact spelling is matched against the disk and backslashes become '/'.
juce::File localFile (const juce::String& path);
juce::File localFile (const std::string& utf8Path);

// True when the app is running under the Android test harness (LYNX_TEST_MODE), so the UI can skip prompts.
bool isTestHarness();

// Touch / mobile mode: true when running on a touch screen device (phone, tablet) or when the user enables
// "Touch mode" in Settings. When true, the UI uses larger hit targets, the on-screen keyboard is shown by default
// for instrument tracks, and the UI scale defaults to 125% instead of 100%.
bool isTouchMode();
void setTouchMode (bool);

// SAF (Storage Access Framework) file access for Android 10+ (API 29+)
// These are called from the Java FileAccessHelper via JNI
void setFileOpenCallback (std::function<void (const juce::String&, bool)>);
void setFileSaveCallback (std::function<void (const juce::String&, bool)>);
void setFolderPickCallback (std::function<void (const juce::String&, bool)>);

// Trigger SAF document picker from C++
void openDocument (const juce::String& mimeTypes);
void saveDocument (const juce::String& suggestedName, const juce::String& mimeType);
void pickFolder ();

// Convert content:// URI to local file path (copies to app cache)
juce::String uriToFile (const juce::String& uri, const juce::String& fallbackExtension);
// Save local file to content:// URI
bool saveFileToUri (const juce::String& localPath, const juce::String& uri);
} // namespace lynx::platform