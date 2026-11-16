// gmic_plugin_call(): run G'MIC on the plug-in's images, like gmic_call() from gmic_libc, but
// never writing more than maxImages results, and returning no result when G'MIC was aborted.
#ifndef GMIC_PLUGIN_CALL_H
#define GMIC_PLUGIN_CALL_H

#include "gmic_libc.h"

// The input images must be planar float images (E_FORMAT_FLOAT, not interleaved), and the result
// images are planar float images: options->interleave_output and options->output_format are
// ignored. Returns:
// - 0 on success: *nofImages and images[0..*nofImages-1] hold the result (free each data pointer
//   that is not an input's with gmic_delete_external())
// - 1 if *options->p_is_abort was set when G'MIC returned (an abort, whose result is incomplete)
// - -1 on a G'MIC error, or if the result has more than maxImages images: the message is in
//   options->error_message_buffer (at least 256 bytes)
// In both error cases, *nofImages and images[] are left unchanged.
int gmic_plugin_call(const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options);

#endif
