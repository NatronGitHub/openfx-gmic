// Build tool: writes the list of the plug-ins of a GMIC.ofx (the bundle's
// Contents/Resources/gmic_ofx_plugins.txt, see README, "Choosing the plug-ins"), every plug-in
// enabled.
//
//   gmic_ofx_plugin_list <GMIC.ofx> <output file>
//
// It loads the plug-in and calls OfxGetNumberOfPlugins, with OFX_GMIC_WRITE_PLUGIN_LIST=<output
// file>: the plug-in writes the list when it builds it (gmic_plugin.cpp). HOME and APPDATA are set
// to a directory that does not exist, so that no G'MIC configuration of the user (a
// gmic_ofx.gmic, or a gmic_ofx_plugins.txt) changes the list. Built and run by CMakeLists.txt and
// GMIC_OFX/Makefile; not when cross-compiling.
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

bool setEnv(const char* name, const std::string& value)
{
#ifdef _WIN32
    return _putenv_s(name, value.c_str()) == 0;
#else
    return setenv(name, value.c_str(), 1) == 0;
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <GMIC.ofx> <output file>\n", argv[0]);
        return 2;
    }
    const std::string output = argv[2];
    std::remove(output.c_str());
    const std::string noConfig = output + ".no-config"; // (not created)
    if (!setEnv("OFX_GMIC_WRITE_PLUGIN_LIST", output) || !setEnv("HOME", noConfig) || !setEnv("APPDATA", noConfig)) {
        fprintf(stderr, "%s: could not set the environment\n", argv[0]);
        return 1;
    }
    using GetNumberOfPlugins = int (*)();
#ifdef _WIN32
    const HMODULE lib = LoadLibraryA(argv[1]);
    const GetNumberOfPlugins getNumberOfPlugins =
        lib ? reinterpret_cast<GetNumberOfPlugins>(GetProcAddress(lib, "OfxGetNumberOfPlugins")) : nullptr;
#else
    void* const lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const GetNumberOfPlugins getNumberOfPlugins =
        lib ? reinterpret_cast<GetNumberOfPlugins>(dlsym(lib, "OfxGetNumberOfPlugins")) : nullptr;
#endif
    if (!getNumberOfPlugins) {
#ifdef _WIN32
        fprintf(stderr, "%s: could not load %s (error %lu)\n", argv[0], argv[1], static_cast<unsigned long>(GetLastError()));
#else
        fprintf(stderr, "%s: could not load %s: %s\n", argv[0], argv[1], dlerror());
#endif
        return 1;
    }
    // (the plug-in stays loaded until exit, as in a host)
    const int n = getNumberOfPlugins();
    if (n <= 0 || !std::ifstream(output.c_str())) {
        fprintf(stderr, "%s: %s has %d plug-ins, and wrote no list to %s\n", argv[0], argv[1], n, output.c_str());
        return 1;
    }
    printf("%s: %d plug-ins\n", output.c_str(), n);
    return 0;
}
