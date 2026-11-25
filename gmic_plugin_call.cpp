// gmic_plugin_call(): see gmic_plugin_call.h. Based on gmic_call() from gmic_libc.cpp (G'MIC's C
// bridge by Tobias Fleischer), which copies every resulting image into the caller's array: an
// aborted G'MIC returns its image list as it was when it stopped, possibly with more images than
// the plug-in's fixed array (issue #46).
// This file also defines the other functions of gmic_libc.h that the plug-in uses, so that
// gmic_libc.cpp (one more compilation of CImg.h) is not built.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <utility>
#include <string>
#include "gmic.h"
#include "gmic_plugin_call.h"
#include "gmic_log.h"
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

std::string gmicUpdateFileName()
{
	return "update" + std::to_string(gmic_version) + ".gmic";
}

std::string gmicRcPath()
{
	// gmic::path_rc() falls back to the temporary folder (TMP, TEMP, TMPDIR) when none of these is
	// set: never read a filter file from a folder that other users can write to
#if cimg_OS == 2
	static const char* const kVars[] = { "GMIC_PATH", "XDG_CONFIG_HOME", "APPDATA" };
#else
	static const char* const kVars[] = { "GMIC_PATH", "XDG_CONFIG_HOME", "HOME" };
#endif
	bool set = false;
	for (const char* const var : kVars) {
		set = set || std::getenv(var) != nullptr;
	}
	const char* const path = set ? gmic::path_rc() : nullptr;
	return path ? std::string(path) : std::string();
}

// the log file is emptied when it is opened if it is larger than this, so that it doesn't grow
// without bounds (G'MIC-Qt empties its gmic_qt_log at each start)
constexpr long kMaxLogBytes = 16L * 1024L * 1024L;

void gmicOpenLog(const std::string& path)
{
	if (path.empty()) return;
	// opt-in: only a file that the user made (never created here). cimg::std_fopen: on Windows, also
	// a UTF-8 path (_wfopen), e.g. in a profile folder with non-ASCII characters (get_gmic_rc_path()
	// gives %APPDATA% in UTF-8, issue #201)
	std::FILE* file = cimg::std_fopen(path.c_str(), "r");
	if (!file) return;
	std::fclose(file);
	file = cimg::std_fopen(path.c_str(), "a");
	if (!file) {
		gmicLog("openfx-gmic: Error: could not open the log file %s: the messages stay on stderr\n", path.c_str());
		return;
	}
	if (std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) > kMaxLogBytes) {
		std::fclose(file);
		file = cimg::std_fopen(path.c_str(), "w");
		if (!file) {
			gmicLog("openfx-gmic: Error: could not empty the log file %s: the messages stay on stderr\n", path.c_str());
			return;
		}
		std::fprintf(file, "[openfx-gmic] log emptied: it was larger than %ld MB\n", kMaxLogBytes / (1024L * 1024L));
		std::fclose(file);
		// append mode again: each write goes to the end of the file, after the lines that other host
		// processes append (in "w" mode, this process would write at its own offset, over them)
		file = cimg::std_fopen(path.c_str(), "a");
		if (!file) {
			gmicLog("openfx-gmic: Error: could not open the log file %s: the messages stay on stderr\n", path.c_str());
			return;
		}
	}
	std::tm local{};
	const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
	const bool dated = localtime_s(&local, &now) == 0;
#else
	const bool dated = localtime_r(&now, &local) != nullptr;
#endif
	char date[64] = "";
	if (dated) std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local);
	gmicSetLogFile(file); // (gmic_log.cpp: never closed)
	cimg::output(file);
	gmicLog("\n[openfx-gmic] %s: G'MIC %d, log started\n", date, (int)gmic_version);
}

static void setError(gmic_interface_options* options, const std::string& message)
{
	if (options->error_message_buffer) {
		std::strncpy(options->error_message_buffer, message.c_str(), 255);
		options->error_message_buffer[255] = '\0';
	}
}

// G'MIC-Qt's FilterThread::status2StringList(): the values of a status "{a}{b}_1{c}..." (with
// G'MIC's codes for the braces), each with G'MIC's other codes replaced back; empty if the status
// is not such a list
static std::vector<std::string> statusValues(const gmic_image<char>& status)
{
	std::vector<std::string> values;
	const std::string s = status._data ? std::string(status._data) : std::string();
	// "{" first, "}" last, maybe followed by a visibility "_0", "_1" or "_2"
	size_t end = s.size();
	if (end >= 3 && s[end - 3] == gmic_rbrace && s[end - 2] == '_' && s[end - 1] >= '0' && s[end - 1] <= '2') {
		end -= 2;
	}
	if (s.size() < 2 || s[0] != gmic_lbrace || end < 2 || s[end - 1] != gmic_rbrace) {
		return values;
	}
	// split at each "}{" or "}_N{" (N in 0..2) between the braces
	std::string value;
	for (size_t k = 1; k < end - 1; ++k) {
		if (s[k] == gmic_rbrace) {
			size_t next = k + 1;
			if (next + 1 < end && s[next] == '_' && s[next + 1] >= '0' && s[next + 1] <= '2') next += 2;
			if (next < end - 1 && s[next] == gmic_lbrace) {
				values.push_back(value);
				value.clear();
				k = next;
				continue;
			}
		}
		value += s[k];
	}
	values.push_back(value);
	for (std::string& v : values) {
		gmic::strreplace_fw(v.data()); // (in place: the same length, up to v's terminating null)
	}
	return values;
}

// G'MIC's global variable _persistent, as G'MIC-Qt sets it: an encoded image list (from 'store')
// as such, else as a string
static void setPersistent(gmic& interpreter, const std::string& persistent)
{
	if (persistent.empty()) return;
	if (persistent[0] == gmic_store) {
		// a view of the bytes, which set_variable() copies (CImg's shared images take a non-const
		// pointer: no write happens through it). Its size fits: see gmic_plugin_call()
		const auto size = static_cast<unsigned int>(persistent.size());
		const gmic_image<unsigned char> value(reinterpret_cast<const unsigned char*>(persistent.data()), size, 1, 1, 1, true);
		interpreter.set_variable("_persistent", value);
	} else {
		interpreter.set_variable("_persistent", '=', persistent.c_str());
	}
}

int gmic_plugin_call(const GmicCall& call)
{
	gmic_interface_options* const options = call.options;
	gmic_interface_image* const images = call.images;
	const GmicResults results = call.results;
	if (options->error_message_buffer) options->error_message_buffer[0] = '\0';
	// a G'MIC variable has at most UINT_MAX bytes (a CImg width): fail rather than truncate (ES.46)
	if (call.persistentIn && call.persistentIn->size() > std::numeric_limits<unsigned int>::max()) {
		setError(options, "The _persistent variable left by the preview command is too large (" +
		                  std::to_string(call.persistentIn->size()) + " bytes).");
		return -1;
	}
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
	const unsigned int nofInputs = *call.nofImages;
	gmic_list<float> list(nofInputs);
	gmic_list<char> names(nofInputs);
	// planar float images: shared (processed in place), or copied with no_inplace_processing, and
	// always for a run whose results are discarded (the inputs stay unchanged for the next run)
	const bool shared = !options->no_inplace_processing && results == GmicResults::kKeep;
	for (unsigned int i = 0; i < nofInputs; ++i) {
		list[i].assign((float*)images[i].data, images[i].width, images[i].height, images[i].depth, images[i].spectrum, shared);
		names[i].assign((unsigned int)std::strlen(images[i].name) + 1);
		std::strcpy(names[i], images[i].name);
	}

	GmicRunState state;
	try {
		// as G'MIC-Qt's FilterThread::run(): an interpreter (with the standard library and the
		// custom commands), _persistent, then the command
		gmic interpreter(nullptr, options->custom_commands, !options->ignore_stdlib, options->p_progress, options->p_is_abort, 0.f);
		if (call.persistentIn) {
			setPersistent(interpreter, *call.persistentIn);
			std::string().swap(*call.persistentIn); // (G'MIC has its own copy)
		}
		interpreter.run(call.cmd, list, names);
		if (call.stateOut) {
			state.status = statusValues(interpreter.status);
			const gmic_image<char> value = interpreter.get_variable("_persistent");
			if (value._data) state.persistent.assign(value._data, value.size());
		}
	} catch (gmic_exception& e) {
		gmicLog("\n- Error encountered when calling G'MIC : '%s'\n", e.what());
		setError(options, e.what());
		return -1;
	}
	if (results == GmicResults::kDiscard) {
		if (call.stateOut) *call.stateOut = std::move(state);
		return 0; // (list frees the images that G'MIC made)
	}
	// G'MIC also returns normally when aborted, or after 'quit' (which sets the abort flag too):
	// the result is the image list as it was then. Whether the render was aborted is for the caller
	// to tell, from the host.
	if (list._width > call.maxImages) {
		char message[256];
		std::snprintf(message, sizeof(message), "G'MIC returned %u images, more than the maximum of %u.", list._width, call.maxImages);
		setError(options, message);
		return -1;
	}
	if (call.stateOut) *call.stateOut = std::move(state);

	*call.nofImages = list._width;
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
