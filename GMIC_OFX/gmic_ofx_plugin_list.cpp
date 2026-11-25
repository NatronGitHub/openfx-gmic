// Build tool: writes the list of the plug-ins of a GMIC.ofx (the bundle's
// Contents/Resources/gmic_ofx_plugins.txt, see README, "Choosing the plug-ins"), every plug-in
// enabled.
//
//   gmic_ofx_plugin_list <GMIC.ofx> <output file>
//
// It loads the plug-in and calls OfxGetNumberOfPlugins, with OFX_GMIC_WRITE_PLUGIN_LIST set: the
// plug-in writes the list when it builds it (gmic_plugin.cpp). HOME, APPDATA and USERPROFILE are
// set to a directory that does not exist, so that no G'MIC configuration of the user (a
// gmic_ofx.gmic, a gmic_ofx_plugins.txt, or the user source ~/.gmic or user.gmic) changes the
// list. It fails unless the list has one entry per plug-in.
// Built and run by CMakeLists.txt (GMIC_PLUGIN_LIST) and GMIC_OFX/Makefile.
//
// The plug-in reads OFX_GMIC_WRITE_PLUGIN_LIST with the narrow getenv and writes the list with a
// narrow ofstream, which can't represent every path (e.g. a non-ASCII one on Windows): the tool
// runs in the output's directory and gives the list (and the folder that HOME, APPDATA and
// USERPROFILE name) relative ASCII names, then renames the list to the output.
// Windows: UTF-16 paths (wmain; MinGW links it with -municode). No std::filesystem, which macOS
// only has from 10.15 (the plug-in may target older versions).
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

#ifdef _WIN32
using Path = std::wstring; // (UTF-16)
const wchar_t* const kSeparators = L"/\\";
#else
using Path = std::string;
const char* const kSeparators = "/";
#endif

// the names that the plug-in gets, relative to the output's directory
constexpr const char* kListName = "gmic_ofx_plugin_list.out";
constexpr const char* kNoConfig = "gmic_ofx_plugin_list.no-config"; // (not created)

// the ASCII string "s" as a Path
Path ascii(const char* s)
{
    return Path(s, s + std::char_traits<char>::length(s));
}

// "path" for messages (Windows: UTF-8)
std::string display(const Path& path)
{
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return std::string();
    }
    std::string s(static_cast<std::string::size_type>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &s[0], n, nullptr, nullptr);
    s.resize(s.size() - 1); // (the terminating null)
    return s;
#else
    return path;
#endif
}

// "path" made absolute, or empty if it can't be
Path absolute(const Path& path)
{
#ifdef _WIN32
    wchar_t* const p = _wfullpath(nullptr, path.c_str(), 0);
#else
    char* const p = realpath(path.c_str(), nullptr);
#endif
    const Path result = p ? Path(p) : Path();
    std::free(p);
    return result;
}

// The number of entries of the list "name" (its lines that are neither empty nor comments), -1 if
// it can't be read
int countEntries(const char* name)
{
    std::ifstream in(name);
    if (!in) {
        return -1;
    }
    int n = 0;
    std::string line;
    while (std::getline(in, line)) {
        const std::string::size_type b = line.find_first_not_of(" \t\r");
        if (b != std::string::npos && line[b] != '#') {
            ++n;
        }
    }
    return n;
}

// Set the environment variable "name" to "value" (both ASCII), for the plug-in loaded afterwards
bool setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    // _putenv_s also sets the process environment (SetEnvironmentVariable), from which the C
    // runtime of a plug-in built with /MT (its own copy) takes its environment when it starts, at
    // LoadLibrary: so the plug-in's getenv sees it.
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

// Load the plug-in and return its number of plug-ins, or -1 (with a message) if it can't be loaded
int numberOfPlugins(const Path& plugin)
{
    using GetNumberOfPlugins = int (*)();
#ifdef _WIN32
    const HMODULE lib = LoadLibraryW(plugin.c_str());
    const GetNumberOfPlugins getNumberOfPlugins =
        lib ? reinterpret_cast<GetNumberOfPlugins>(GetProcAddress(lib, "OfxGetNumberOfPlugins")) : nullptr;
    if (!getNumberOfPlugins) {
        fprintf(stderr, "gmic_ofx_plugin_list: could not load %s (error %lu)\n", display(plugin).c_str(),
            static_cast<unsigned long>(GetLastError()));
        return -1;
    }
#else
    void* const lib = dlopen(plugin.c_str(), RTLD_NOW | RTLD_LOCAL);
    const GetNumberOfPlugins getNumberOfPlugins =
        lib ? reinterpret_cast<GetNumberOfPlugins>(dlsym(lib, "OfxGetNumberOfPlugins")) : nullptr;
    if (!getNumberOfPlugins) {
        fprintf(stderr, "gmic_ofx_plugin_list: could not load %s: %s\n", plugin.c_str(), dlerror());
        return -1;
    }
#endif
    // (the plug-in stays loaded until exit, as in a host)
    return getNumberOfPlugins();
}

int run(const Path& pluginArg, const Path& output)
{
    const Path plugin = absolute(pluginArg); // (before the chdir)
    const Path::size_type slash = output.find_last_of(kSeparators);
    const Path dir = slash == Path::npos ? ascii(".") : output.substr(0, slash + 1);
    const Path name = slash == Path::npos ? output : output.substr(slash + 1);
#ifdef _WIN32
    const bool inDir = _wchdir(dir.c_str()) == 0;
    if (inDir) {
        _wremove(name.c_str());
    }
#else
    const bool inDir = chdir(dir.c_str()) == 0;
    if (inDir) {
        std::remove(name.c_str());
    }
#endif
    if (plugin.empty() || !inDir) {
        fprintf(stderr, "gmic_ofx_plugin_list: %s not found, or no directory %s\n", display(pluginArg).c_str(), display(dir).c_str());
        return 1;
    }
    std::remove(kListName);
    if (!setEnv("OFX_GMIC_WRITE_PLUGIN_LIST", kListName) || !setEnv("HOME", kNoConfig) || !setEnv("APPDATA", kNoConfig) ||
        !setEnv("USERPROFILE", kNoConfig)) {
        fprintf(stderr, "gmic_ofx_plugin_list: could not set the environment\n");
        return 1;
    }
    const int n = numberOfPlugins(plugin);
    if (n < 0) {
        return 1;
    }
    const int entries = countEntries(kListName);
    if (n == 0 || entries != n) {
        fprintf(stderr, "gmic_ofx_plugin_list: %s has %d plug-ins, but its list has %d entries\n", display(plugin).c_str(), n, entries);
        return 1;
    }
#ifdef _WIN32
    const bool renamed = _wrename(ascii(kListName).c_str(), name.c_str()) == 0;
#else
    const bool renamed = std::rename(kListName, name.c_str()) == 0;
#endif
    if (!renamed) {
        fprintf(stderr, "gmic_ofx_plugin_list: could not write %s\n", display(output).c_str());
        return 1;
    }
    printf("%s: %d plug-ins\n", display(output).c_str(), n);
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc != 3) {
        fprintf(stderr, "Usage: gmic_ofx_plugin_list <GMIC.ofx> <output file>\n");
        return 2;
    }
    return run(argv[1], argv[2]);
}
