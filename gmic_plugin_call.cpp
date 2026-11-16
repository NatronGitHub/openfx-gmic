// gmic_plugin_call(): see gmic_plugin_call.h. Based on gmic_call() from gmic_libc.cpp (G'MIC's C
// bridge by Tobias Fleischer), which copies every resulting image into the caller's array: an
// aborted G'MIC returns its image list as it was when it stopped, possibly with more images than
// the plug-in's fixed array (issue #46).
#include <cstdio>
#include <cstring>
#include <string>
#include "gmic.h"
#include "gmic_plugin_call.h"

static void setError(gmic_interface_options* options, const std::string& message)
{
	if (options->error_message_buffer) {
		std::strncpy(options->error_message_buffer, message.c_str(), 255);
		options->error_message_buffer[255] = '\0';
	}
}

int gmic_plugin_call(const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options)
{
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
	if (options->p_is_abort && *options->p_is_abort) {
		return 1;
	}
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
