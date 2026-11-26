/*
 #
 #  File        : gmic_log.h
 #
 #  Description : The plug-in's messages: on stderr, or in the user's log file (README,
 #                "Messages"). No dependency on G'MIC, so that the parser (gmic_parser.cpp) can
 #                use it without gmic.h.
 #
 #  Licenses    : CeCILL-C or CeCILL v2.0, as gmic_plugin.cpp
 #
*/
#ifndef GMIC_LOG_H
#define GMIC_LOG_H

#include <cstdio>

#if defined(__GNUC__) || defined(__clang__)
#define GMIC_LOG_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define GMIC_LOG_PRINTF_FORMAT(f, a)
#endif

// Where the messages go: the log file given to gmicSetLogFile(), else stderr. Thread-safe. (G'MIC
// writes its own messages to cimg::output(), which gmicOpenLog() sets to the same file.)
std::FILE* gmicLogStream();

// From now on, the messages go to file, which the caller opened and gives up: it is never closed
// (see gmic_log.cpp). Called once, when the plug-in loads the filters, before any render.
void gmicSetLogFile(std::FILE* file);

// Writes a printf-style message to gmicLogStream() (the format ends the line with its own '\n'),
// in one vfprintf call, whose stream lock keeps the lines of concurrent threads apart, then
// flushes it, so that the message is in the file at once (also before a crash). A C-style
// variadic function, against C++ Core Guidelines ES.34
// (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-ellipses): it replaces the
// fprintf(stderr, ...) calls with their format strings unchanged, and compilers check its
// arguments as printf's (GMIC_LOG_PRINTF_FORMAT).
void gmicLog(const char* format, ...) GMIC_LOG_PRINTF_FORMAT(1, 2);

#endif
