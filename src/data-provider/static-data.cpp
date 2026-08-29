#include "static-data.h"

#include <mutex>

namespace {

std::string& staticDataDirStorage()
{
    static std::string path = "./data";
    return path;
}

std::mutex& staticDataDirMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace

void setStaticDataDir(const std::string &path)
{
    std::lock_guard<std::mutex> lock(staticDataDirMutex());
    staticDataDirStorage() = path;
}

std::string getStaticDataDir()
{
    std::lock_guard<std::mutex> lock(staticDataDirMutex());
    return staticDataDirStorage();
}

std::string getStaticDataFilePath(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(staticDataDirMutex());
    return staticDataDirStorage() + "/" + filename;
}
