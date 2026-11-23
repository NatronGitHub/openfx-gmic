// Build tool: writes the list of the plug-ins of a GMIC.ofx (the bundle's
// Contents/Resources/gmic_ofx_plugins.txt, see README, "Choosing the plug-ins"), every plug-in
// enabled.
//
//   gmic_ofx_plugin_list <GMIC.ofx> <output file>
//
// It loads the plug-in and calls OfxGetNumberOfPlugins, with OFX_GMIC_WRITE_PLUGIN_LIST=<output
// file>: the plug-in writes the list when it builds it (gmic_plugin.cpp). HOME and APPDATA are set
// to a directory that does not exist, so that no G'MIC configuration of the user (a
// gmic_ofx.gmic, or a gmic_ofx_plugins.txt) changes the list. It fails unless the list has one
// entry per plug-in. Built and run by CMakeLists.txt (GMIC_PLUGIN_LIST) and GMIC_OFX/Makefile.
// C++17. Windows: UTF-16 paths (wmain; MinGW links it with -municode).
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

// The number of entries of the list at "path" (its lines that are neither empty nor comments), -1
// if it can't be read
int countEntries(const fs::path& path)
{
    std::ifstream in(path);
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

// "path" in UTF-8, for messages
std::string display(const fs::path& path)
{
    const auto s = path.u8string(); // (C++20: a std::u8string)
    return std::string(s.begin(), s.end());
}

// Set the environment variable "name" (ASCII) to "value", for the plug-in loaded afterwards
bool setEnv(const char* name, const fs::path& value)
{
#ifdef _WIN32
    // _wputenv_s also sets the process environment (SetEnvironmentVariableW), from which the C
    // runtime of a plug-in built with /MT (its own copy) takes its environment when it starts, at
    // LoadLibrary: so the plug-in's getenv sees it.
    return _wputenv_s(fs::path(name).c_str(), value.c_str()) == 0;
#else
    return setenv(name, value.c_str(), 1) == 0;
#endif
}

// Load the plug-in and return its number of plug-ins, or -1 (with a message) if it can't be loaded
int numberOfPlugins(const fs::path& plugin)
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

int run(const fs::path& plugin, const fs::path& output)
{
    std::error_code ec;
    fs::remove(output, ec);
    fs::path noConfig = output; // (not created)
    noConfig += ".no-config";
    if (!setEnv("OFX_GMIC_WRITE_PLUGIN_LIST", output) || !setEnv("HOME", noConfig) || !setEnv("APPDATA", noConfig)) {
        fprintf(stderr, "gmic_ofx_plugin_list: could not set the environment\n");
        return 1;
    }
    const int n = numberOfPlugins(plugin);
    if (n < 0) {
        return 1;
    }
    const int entries = countEntries(output);
    if (n == 0 || entries != n) {
        fprintf(stderr, "gmic_ofx_plugin_list: %s has %d plug-ins, but the list %s has %d entries\n",
            display(plugin).c_str(), n, display(output).c_str(), entries);
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
    return run(fs::path(argv[1]), fs::path(argv[2]));
}
