// gmic_plugin_call(): run G'MIC on the plug-in's images, like gmic_call() from gmic_libc, but
// never writing more than maxImages results, and with G'MIC-Qt's state between the runs of a
// filter (GmicRunState). gmic_plugin_call.cpp also defines gmic_delete_external() and
// gmic_get_stdlib() from gmic_libc.h (gmic_libc.cpp is not built), and setDownloadCommands(),
// gmicUpdateFileName() and gmicFolder(), which need gmic.h.
#ifndef GMIC_PLUGIN_CALL_H
#define GMIC_PLUGIN_CALL_H

#include "gmic_libc.h"
#include "gmic_visibility.h"
#include <optional>
#include <string>
#include <vector>

// What a run of the preview command leaves for the main command of the same filter, as G'MIC-Qt
// keeps it between its preview and its Apply (FilterThread.cpp, PersistentMemory.cpp in
// c-koi/gmic-qt).
struct GmicRunState
{
	// G'MIC's global variable _persistent after the run (its raw bytes: a string, or a G'MIC-encoded
	// image list from 'store'; empty if it is undefined)
	std::string persistent;
	// the run's status ('u' command), if it is a list of values "{a}{b}..." (a "_0", "_1" or "_2"
	// visibility after a value is ignored): the values, as G'MIC-Qt splits them
	// (FilterThread::status2StringList()); else empty
	std::vector<std::string> status;
	// the visibility state that the status gives each value, as G'MIC-Qt reads them
	// (FilterThread::status2Visibilities()): hidden, disabled or visible for a value followed by
	// "_0", "_1" or "_2", none (G'MIC-Qt's Unspecified: the parameter's initial state) for one
	// without; empty if the status is not a list of values "{a}{b}...", or has another text after
	// a value
	std::vector<std::optional<gmic_ofx::ParamVisibility>> visibilities;
};

// What to do with the images that G'MIC returns
enum class GmicResults
{
	kKeep, // return them in images[]
	kDiscard, // free them, and leave *nofImages and images[] unchanged (a run for its state only)
};

// One G'MIC run (C++ Core Guidelines I.23: the arguments of gmic_plugin_call() in one struct).
// The input images must be planar float images (E_FORMAT_FLOAT, not interleaved), and the result
// images are planar float images: options->interleave_output and options->output_format are
// ignored.
struct GmicCall
{
	const char* cmd = nullptr;
	unsigned int* nofImages = nullptr; // in: the number of inputs; out: of results (kKeep)
	gmic_interface_image* images = nullptr; // maxImages entries: the inputs, then the results
	unsigned int maxImages = 0;
	gmic_interface_options* options = nullptr;
	// In-out (C++ Core Guidelines F.17), although GmicCall is passed as const: the call modifies
	// the string that this points to, not the struct. If not null and not empty: G'MIC's
	// _persistent before the run (GmicRunState::persistent of an earlier run). gmic_plugin_call()
	// then empties the string (frees its memory) once G'MIC has its copy, also if the run fails
	// later; it fails with an error if the string has more than UINT_MAX bytes (G'MIC's limit).
	std::string* persistentIn = nullptr;
	// if not null: set after a successful run (only then), with what the run left
	GmicRunState* stateOut = nullptr;
	GmicResults results = GmicResults::kKeep;
};

// Runs call.cmd in a new G'MIC interpreter. Empties *call.persistentIn (see GmicCall). Returns:
// - 0 on success: with GmicResults::kKeep, *nofImages and images[0..*nofImages-1] hold the result
//   (free each data pointer that is not an input's with gmic_delete_external()). G'MIC also
//   returns normally when it was aborted through options->p_is_abort, or after the 'quit' command
//   (which sets that flag too): then the result is the image list as it was at that point.
// - -1 on a G'MIC error, or if the result has more than maxImages images (kKeep only): the message
//   is in options->error_message_buffer (at least 256 bytes; emptied at the start of each call),
//   *nofImages, images[] and *stateOut are left unchanged.
int gmic_plugin_call(const GmicCall& call);

// The name of the filter update file of the G'MIC version the plug-in is built with,
// "update<gmic_version>.gmic" (update408.gmic for G'MIC 4.0.8), as G'MIC's 'update' command and
// G'MIC-Qt name it in the G'MIC folder.
std::string gmicUpdateFileName();

// G'MIC's own folder, with a trailing separator, as G'MIC and G'MIC-Qt find it (gmic::path_rc()):
// $GMIC_PATH, else $XDG_CONFIG_HOME/gmic, else $HOME/.config/gmic if $HOME/.config exists, else
// $HOME/gmic (Windows: %APPDATA%/gmic); empty when none of these variables is set, where G'MIC
// would use the temporary folder. G'MIC-Qt downloads the update file there. (The plug-in's own
// files, gmic_ofx.gmic..., are in pluginConfigFolder(), gmic_plugin.cpp.)
std::string gmicFolder();

// Sets the commands that CImg runs to download filter data (curl, then wget and gunzip, or
// PowerShell on Windows), so that they are never looked up in the host's current directory (see
// gmic_plugin_call.cpp). Called once, when the plug-in loads the filters, before any G'MIC run.
void setDownloadCommands();

// If the file "path" (UTF-8) exists, the plug-in's messages (gmicLog, Helpers/gmic_log.h) and
// G'MIC's (cimg::output) are appended to it from now on, as G'MIC-Qt's "log file" output messages
// do with its gmic_qt_log; else they stay on stderr. For hosts started from a menu, which show no
// stderr. A file larger than 16 MB is emptied first. Called once, when the plug-in loads the
// filters (the file stays open until the process exits).
void gmicOpenLog(const std::string& path);

#endif
