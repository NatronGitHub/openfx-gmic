/*
 #
 #  File        : gmic_log.cpp
 #
 #  Description : The plug-in's messages: see gmic_log.h
 #
 #  Licenses    : CeCILL-C or CeCILL v2.0, as gmic_plugin.cpp
 #
*/
#include "gmic_log.h"

#include <atomic>
#include <cstdarg>

namespace {
// The log file, or nullptr for stderr. A global (an exception to C++ Core Guidelines I.2,
// https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-global): every message of the
// process goes to the same place, set once when the filters are loaded and then only read, from
// any thread (atomic). The file is never closed (an exception to R.1,
// https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-raii): G'MIC keeps writing to
// it through cimg::output() until the process exits, also from the destructors of statics and
// from threads still running when the host unloads the plug-in; the C runtime flushes and closes
// it at exit.
std::atomic<std::FILE*> logFile(nullptr);
}

std::FILE* gmicLogStream()
{
	std::FILE* const file = logFile.load();
	return file ? file : stderr;
}

void gmicSetLogFile(std::FILE* file)
{
	logFile.store(file);
}

void gmicLog(const char* format, ...)
{
	std::FILE* const stream = gmicLogStream();
	va_list args;
	va_start(args, format);
	std::vfprintf(stream, format, args);
	va_end(args);
	std::fflush(stream);
}
