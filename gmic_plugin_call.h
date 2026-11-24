// gmic_plugin_call(): run G'MIC on the plug-in's images, like gmic_call() from gmic_libc, but
// never writing more than maxImages results, and with G'MIC-Qt's state between the runs of a
// filter (GmicRunState). gmic_plugin_call.cpp also defines gmic_delete_external() and
// gmic_get_stdlib() from gmic_libc.h (gmic_libc.cpp is not built), and gmicUpdateFileName() and
// gmicRcPath(), which need gmic.h.
#ifndef GMIC_PLUGIN_CALL_H
#define GMIC_PLUGIN_CALL_H

#include "gmic_libc.h"
#include <string>
#include <vector>

// What a run leaves for the next run of the same filter, as G'MIC-Qt keeps it between its preview
// and its Apply (FilterThread.cpp, PersistentMemory.cpp in c-koi/gmic-qt).
struct GmicRunState
{
	// G'MIC's global variable _persistent: set before the run if not empty, and read back after a
	// successful run (its raw bytes: a string, or a G'MIC-encoded image list from 'store')
	std::string persistent;
	// the run's status ('u' command), if it is a list of values "{a}{b}..." (a "_0", "_1" or "_2"
	// visibility after a value is ignored): the values, as G'MIC-Qt splits them
	// (FilterThread::status2StringList()); else empty. Set after a successful run.
	std::vector<std::string> status;
};

// What to do with the images that G'MIC returns
enum class GmicResults
{
	kKeep, // return them in images[]
	kDiscard, // free them, and leave *nofImages and images[] unchanged (a run for its state only)
};

// The input images must be planar float images (E_FORMAT_FLOAT, not interleaved), and the result
// images are planar float images: options->interleave_output and options->output_format are
// ignored. Each call runs a new G'MIC interpreter. state: if not null, its persistent is given to
// G'MIC before the run, and set with the status after it (see GmicRunState). Returns:
// - 0 on success: with GmicResults::kKeep, *nofImages and images[0..*nofImages-1] hold the result
//   (free each data pointer that is not an input's with gmic_delete_external()). G'MIC also
//   returns normally when it was aborted through options->p_is_abort, or after the 'quit' command
//   (which sets that flag too): then the result is the image list as it was at that point.
// - -1 on a G'MIC error, or if the result has more than maxImages images (kKeep only): the message
//   is in options->error_message_buffer (at least 256 bytes), *nofImages and images[] are left
//   unchanged, and so is *state.
int gmic_plugin_call(const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options, GmicRunState* state = nullptr, GmicResults results = GmicResults::kKeep);

// The name of the filter update file of the G'MIC version the plug-in is built with,
// "update<gmic_version>.gmic" (update408.gmic for G'MIC 4.0.8), as G'MIC's 'update' command and
// G'MIC-Qt name it in the G'MIC folder.
std::string gmicUpdateFileName();

// G'MIC's own folder, with a trailing separator, as G'MIC and G'MIC-Qt find it (gmic::path_rc()):
// $GMIC_PATH, else $XDG_CONFIG_HOME/gmic, else $HOME/.config/gmic if $HOME/.config exists, else
// $HOME/gmic (Windows: %APPDATA%/gmic); empty when none of these variables is set, where G'MIC
// would use the temporary folder. G'MIC-Qt downloads the update file there. (The plug-in's own
// files, gmic_ofx.gmic..., are looked up in get_gmic_rc_path().)
std::string gmicRcPath();

// If the file "path" (UTF-8) exists, the plug-in's messages (gmicLog, Helpers/gmic_log.h) and
// G'MIC's (cimg::output) are appended to it from now on, as G'MIC-Qt's "log file" output messages
// do with its gmic_qt_log; else they stay on stderr. For hosts started from a menu, which show no
// stderr. A file larger than 16 MB is emptied first. Called once, when the plug-in loads the
// filters (the file stays open until the process exits).
void gmicOpenLog(const std::string& path);

#endif
