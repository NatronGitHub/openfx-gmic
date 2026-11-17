// gmic_plugin_call(): run G'MIC on the plug-in's images, like gmic_call() from gmic_libc, but
// never writing more than maxImages results. gmic_plugin_call.cpp also defines
// gmic_delete_external() and gmic_get_stdlib() from gmic_libc.h (gmic_libc.cpp is not built).
#ifndef GMIC_PLUGIN_CALL_H
#define GMIC_PLUGIN_CALL_H

#include "gmic_libc.h"

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

#endif
