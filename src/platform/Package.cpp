#include "platform/Package.h"

#include "core/StorePackage.h"
#include "platform/Paths.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shellapi.h>
#endif

namespace lynx::platform
{
namespace
{
struct Identity
{
    juce::String family, fullName;
    bool simulated = false;
};

const Identity& identity()
{
    static const Identity id = [] {
        Identity result;
       #if JUCE_WINDOWS
        // looked up at run time: kernel32 has them since Windows 8, and MinGW's appmodel.h depends on _WIN32_WINNT
        using GetName = LONG (WINAPI*) (UINT32*, PWSTR);
        using GetPackageId = LONG (WINAPI*) (UINT32*, PWSTR);
        static GetName getName = (GetName) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "GetPackageFamilyName");
        static GetPackageId getPackageId = (GetPackageId) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "GetPackageFullName");
        if (getName != nullptr && getPackageId != nullptr)
        {
            UINT32 length = 0;
            getName (&length, nullptr);
            std::unique_ptr<wchar_t[]> buffer (new wchar_t[length]);
            getName (&length, buffer.get());
            result.family = juce::String (buffer.get(), length - 1); // -1 for trailing '\0'
            length = 0;
            getPackageId (&length, nullptr);
            buffer.reset (new wchar_t[length]);
            getPackageId (&length, buffer.get());
            result.fullName = juce::String (buffer.get(), length - 1);
        }
        else
        {
            result.simulated = true;
            const char* sim = std::getenv ("LYNX_SIMULATE_PACKAGE_FAMILY");
            if (sim != nullptr)
            {
                result.family = juce::String (sim);
                result.fullName = result.family + "_987654321";
            }
        }
       #endif
        return result;
    }();
    return id;
}

} // namespace

juce::String packageFamilyName()
{
    return identity().family;
}

juce::String packageFullName()
{
    return identity().fullName;
}

bool runningPackaged()
{
   #if JUCE_WINDOWS
    // GetCurrentPackageFamilyName returns APPMODEL_ERROR_NO_PACKAGE when not packaged
    using GetName = LONG (WINAPI*) (UINT32*, PWSTR);
    static GetName getName = (GetName) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "GetPackageFamilyName");
    if (getName != nullptr)
    {
        UINT32 length = 0;
        const auto err = getName (nullptr, nullptr);
        if (err == 0)
            return true;
        if (err == APPMODEL_ERROR_NO_PACKAGE)
        {
            // Check if we're simulating a package family
            const char* sim = std::getenv ("LYNX_SIMULATE_PACKAGE_FAMILY");
            if (sim != nullptr && *sim != '\0')
                return true;
        }
    }
   #endif
    return false;
}

juce::File packagedSettingsFolder()
{
   #if JUCE_WINDOWS
    // the Microsoft Store build: the real path of the package's redirected %APPDATA%\LYNX
    if (runningPackaged())
    {
        // get the real path of the package's LocalAppData\LYNX (redirected from %APPDATA%)
        wchar_t path[MAX_PATH * 4] {};
        const auto length = GetModuleFileNameW (nullptr, path, (DWORD) std::size (path));
        if (length > 0 && length < std::size (path))
        {
            juce::File exe (path);
            auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
            // the real path is something like: C:\Users\Name\AppData\Local\Packages\FamilyName\LocalCache\Local\LYNX
            // we find it by going from the exe up to the package root, then down to LocalCache\Local\LYNX
            for (auto p = exe.getParentDirectory(); p != juce::File(); p = p.getParentDirectory())
                if (p.getFileName().endsWithIgnoreCase ("LYNX"))
                    return p;
        }
    }
   #endif
    return juce::File();
}

// Starts LYNX again (after a language change): through the package when packaged, else this exe.
// False when it could not be started.
bool relaunchApp()
{
   #if JUCE_WINDOWS
    if (runningPackaged() && ! identity().simulated)
    {
        const auto target = juce::String (store::appsFolderTarget (packageFamilyName().toStdString()));
        const auto result = (INT_PTR) ShellExecuteW (nullptr, L"open", target.toWideCharPointer(), nullptr, nullptr, SW_SHOWNORMAL);
        if (result > 32)
            return true;
    }
   #endif
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).startAsProcess();
}

juce::String packageReport()
{
    juce::StringArray lines;
    bool ok = true;
    const auto& id = identity();
    lines.add ("packaged=" + juce::String (runningPackaged() ? (id.simulated ? "simulated" : "yes") : "no"));
    lines.add ("family=" + id.family);
    lines.add ("fullName=" + id.fullName);
    lines.add ("exe=" + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
    const auto settings = settingsFolder();
    lines.add ("settings=" + settings.getFullPathName());
    lines.add ("cache=" + cacheFolder ("wavecache").getFullPathName());
    lines.add ("temp=" + juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName());
    lines.add ("pluginFolders=" + standardPluginFolders().toString());

    if (runningPackaged())
    {
        // the settings folder must be the package's real folder, so Explorer and other programs find its files
        // (LYNX_PROFILE_DIR, a test profile, wins over the package folder on purpose)
        const auto expected = packagedSettingsFolder();
        const bool profile = profileOverride() != juce::File();
        const bool inPackageData = expected != juce::File() && settings == expected;
        lines.add ("settingsIsPackageFolder=" + juce::String (inPackageData ? "yes" : profile ? "no (LYNX_PROFILE_DIR)" : "NO"));
        ok = ok && (inPackageData || profile);
        lines.add ("migration=" + settings.getChildFile (store::migrationMarker).loadFileAsString().replace ("\n", " | ").trim());
    }

    // a file written to the settings folder can be read back (the CI step checks it from outside the package too)
    const auto probe = settings.getChildFile ("package-probe.txt");
    const auto stamp = juce::String (juce::Time::currentTimeMillis());
    const bool wrote = probe.replaceWithText (stamp) && probe.loadFileAsString() == stamp;
    lines.add ("probe=" + probe.getFullPathName() + (wrote ? " written" : " FAILED"));
    ok = ok && wrote;
    if (! runningPackaged())
        probe.deleteFile(); // (packaged, it stays: the release workflow looks for it from outside the package)
    lines.add (juce::String ("result=") + (ok ? "OK" : "FAILED"));
    return lines.joinIntoString ("\n") + "\n";
}

#if JUCE_ANDROID
// Android stubs for package functions (Windows Store only)
bool runningPackaged() { return false; }
juce::String packageFamilyName() { return {}; }
juce::String packageFullName() { return {}; }
juce::File packagedSettingsFolder() { return {}; }
bool relaunchApp() { return false; }
juce::String packageReport() { return "packaged=no\nresult=OK\n"; }
#endif

} // namespace lynx::platform