// gmic_plugin_call(): see gmic_plugin_call.h. Based on gmic_call() from gmic_libc.cpp (G'MIC's C
// bridge by Tobias Fleischer), which copies every resulting image into the caller's array: an
// aborted G'MIC returns its image list as it was when it stopped, possibly with more images than
// the plug-in's fixed array (issue #46).
// This file also defines the other functions of gmic_libc.h that the plug-in uses, so that
// gmic_libc.cpp (one more compilation of CImg.h) is not built.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "gmic.h"
#include "gmic_plugin_call.h"
#if defined(_WIN32) && defined(_OPENMP)
#include <omp.h>
#endif

// as in gmic_libc.cpp
GMIC_DLLINTERFACE int GMIC_CALLCONV gmic_delete_external(float* p)
{
	delete[] p;
	return 0;
}

// The plug-in doesn't use libcurl: CImg downloads filter data by running the curl command
// (cimg::load_network), then wget (and gunzip) if curl fails, and on Windows PowerShell's
// Invoke-WebRequest before wget. By default, CImg runs ./curl (Windows: SearchPath, which also
// looks in the current directory, else .\curl.exe) if the host's current directory has such a
// file: set the commands once, when the plug-in is loaded, so that they are not looked up in the
// current directory (these functions only store the given string).
// - macOS: the system's /usr/bin/curl and /usr/bin/gunzip; wget (not part of macOS) from PATH.
// - Linux and other Unix systems: curl, wget and gunzip from PATH (run through /bin/sh, so a PATH
//   with "." or an empty entry would still find them in the current directory).
// - Windows: the curl.exe of Windows 10 (1803) and later, and PowerShell, in the system directory
//   (System32), by full path: cimg::system() starts them with CreateProcess(module, ...), which
//   doesn't search PATH and resolves a bare name in the current directory. Windows has no wget or
//   gunzip: they are pinned to the system directory too (where they normally don't exist), so that
//   the fallback never runs a wget.exe from the current directory. If the system directory is
//   unknown, all of them get a path that can't exist (an invalid drive), and downloads fail.
// OFX_GMIC_TEST_CURL_ONLY (any non-empty value; for CI's download test): the fallbacks (wget,
// gunzip, PowerShell) get a path that can't exist, so that a download succeeds only with curl.
namespace {
struct DownloadCommands
{
	DownloadCommands()
	{
		const char* const curlOnly = std::getenv("OFX_GMIC_TEST_CURL_ONLY");
		const bool fallbacks = !(curlOnly && *curlOnly);
#if cimg_OS == 2
		// a path that no file can have ('?' is not a drive letter), never resolved in the current
		// directory
		const std::string none = "?:\\openfx-gmic-no-such-command";
		char dir[MAX_PATH];
		const UINT len = GetSystemDirectoryA(dir, MAX_PATH);
		const std::string sysdir = (len == 0 || len >= MAX_PATH) ? none : std::string(dir);
		cimg::curl_path((sysdir + "\\curl.exe").c_str());
		cimg::powershell_path(((fallbacks ? sysdir : none) + "\\WindowsPowerShell\\v1.0\\powershell.exe").c_str());
		cimg::wget_path(((fallbacks ? sysdir : none) + "\\wget.exe").c_str());
		cimg::gunzip_path(((fallbacks ? sysdir : none) + "\\gunzip.exe").c_str());
#else
		const char* const none = "/nonexistent/openfx-gmic-no-such-command";
#if defined(__APPLE__)
		cimg::curl_path("/usr/bin/curl");
		cimg::wget_path(fallbacks ? "wget" : none);
		cimg::gunzip_path(fallbacks ? "/usr/bin/gunzip" : none);
#else
		cimg::curl_path("curl");
		cimg::wget_path(fallbacks ? "wget" : none);
		cimg::gunzip_path(fallbacks ? "gunzip" : none);
#endif
#endif
	}
};
const DownloadCommands downloadCommands;
}

// as in gmic_libc.cpp: the decompressed G'MIC stdlib (free it with gmic_delete_external)
GMIC_DLLINTERFACE const char* GMIC_CALLCONV gmic_get_stdlib()
{
	gmic_image<char> lib = gmic::decompress_stdlib();
	lib._is_shared = true;
	return lib.data();
}

static void setError(gmic_interface_options* options, const std::string& message)
{
	if (options->error_message_buffer) {
		std::strncpy(options->error_message_buffer, message.c_str(), 255);
		options->error_message_buffer[255] = '\0';
	}
}

int gmic_plugin_call(const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options)
{
#if defined(_WIN32) && defined(_OPENMP)
	// MSVC's OpenMP runtime (vcomp140.dll) must never be unloaded once its worker threads exist:
	// when the host unloads the plug-in (FreeLibrary), the loader unmaps VCOMP140.DLL under the idle
	// workers, and the process crashes (0xC0000005) after rendering. Pin it, once, before the first
	// G'MIC run. Found with openfx's hostDemo, which unloads the plug-in before exiting: every
	// multithreaded render then crashed at exit; pinning VCOMP140.DLL (or GMIC.ofx) fixed them all.
	// This is the pattern of FFTW's unload crash on Linux (see makeFftwThreadSafe() in
	// gmic_plugin.cpp and patches/cimg-fftw-no-cleanup-at-unload.sh): a thread pool whose idle
	// threads outlive the code they run once the host unloads the plug-in; both keep the pool
	// until the process exits.
	// The module is found from the address of one of its functions, not by name: GMIC.ofx's
	// manifest loads the vcomp140.dll of the bundle, which a lookup by name, outside of the
	// plug-in's activation context, doesn't find (it looks for System32's). (Should the address be
	// an import thunk of GMIC.ofx, GMIC.ofx is pinned, which also keeps vcomp140.dll; with MinGW,
	// libgomp is pinned, which is harmless.)
	static const bool vcompPinned = [] {
		HMODULE h;
		return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
		                          (LPCWSTR)(void*)&omp_get_max_threads, &h) != 0;
	}();
	(void)vcompPinned;
#endif
	const unsigned int nofInputs = *nofImages;
	gmic_list<float> list(nofInputs);
	gmic_list<char> names(nofInputs);
	for (unsigned int i = 0; i < nofInputs; ++i) {
		// planar float images: shared (processed in place), or copied with no_inplace_processing
		list[i].assign((float*)images[i].data, images[i].width, images[i].height, images[i].depth, images[i].spectrum, !options->no_inplace_processing);
		names[i].assign((unsigned int)std::strlen(images[i].name) + 1);
		std::strcpy(names[i], images[i].name);
	}

	try {
		gmic(cmd, list, names, options->custom_commands, !options->ignore_stdlib, options->p_progress, options->p_is_abort);
	} catch (gmic_exception& e) {
		std::fprintf(stderr, "\n- Error encountered when calling G'MIC : '%s'\n", e.what());
		setError(options, e.what());
		return -1;
	}
	// G'MIC also returns normally when aborted, or after 'quit' (which sets the abort flag too):
	// the result is the image list as it was then. Whether the render was aborted is for the caller
	// to tell, from the host.
	if (list._width > maxImages) {
		char message[256];
		std::snprintf(message, sizeof(message), "G'MIC returned %u images, more than the maximum of %u.", list._width, maxImages);
		setError(options, message);
		return -1;
	}

	*nofImages = list._width;
	for (unsigned int i = 0; i < list._width; ++i) {
		gmic_image<float>& img = list[i];
		images[i].is_interleaved = false;
		images[i].format = E_FORMAT_FLOAT;
		images[i].width = img._width;
		images[i].height = img._height;
		images[i].depth = img._depth;
		images[i].spectrum = img._spectrum;
		images[i].data = img._data;
		img._is_shared = true; // the caller owns the data (gmic_delete_external)
		std::strncpy(images[i].name, (i < names._width && names[i]._data) ? names[i]._data : "", MAX_IMAGE_NAME_LENGTH);
		images[i].name[MAX_IMAGE_NAME_LENGTH] = '\0';
	}
	return 0;
}
