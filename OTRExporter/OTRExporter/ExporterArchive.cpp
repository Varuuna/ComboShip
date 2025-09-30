#include "ExporterArchive.h"
#include "Utils/StringHelper.h"
#include <utils/StrHash64.h>
#include <filesystem>

ExporterArchive::ExporterArchive(const std::string& path, bool enableWriting) : mPath(path) {
}

ExporterArchive::~ExporterArchive() {
}

