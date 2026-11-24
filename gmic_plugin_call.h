// gmic_plugin_call(): run G'MIC on the plug-in's images, like gmic_call() from gmic_libc, but
// never writing more than maxImages results. gmic_plugin_call.cpp also defines
// gmic_delete_external() and gmic_get_stdlib() from gmic_libc.h (gmic_libc.cpp is not built), and
// gmicUpdateFileName() and gmicRcPath(), which need gmic.h.
#ifndef GMIC_PLUGIN_CALL_H
#define GMIC_PLUGIN_CALL_H

#include "gmic_libc.h"
#include <string>

// The input images must be planar float images (E_FORMAT_FLOAT, not interleaved), and the result
// images are planar float images: options->interleave_output and options->output_format are
// ignored. Returns:
// - 0 on success: *nofImages and images[0..*nofImages-1] hold the result (free each data pointer
//   that is not an input's with gmic_delete_external()). G'MIC also returns normally when it was
//   aborted through options->p_is_abort, or after the 'quit' command (which sets that flag too):
//   then the result is the image list as it was at that point.
// - -1 on a G'MIC error, or if the result has more than maxImages images: the message is in
//   options->error_message_buffer (at least 256 bytes), and *nofImages and images[] are left
//   unchanged.
int gmic_plugin_call(const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options);

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

#endif
