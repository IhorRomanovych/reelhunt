// Core/platform.cpp - see platform.h.
#include "Core/platform.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Engine::Platform {

namespace fs = std::filesystem;

fs::path executableDir()
{
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0)
            return {};
        if (n < buffer.size())
        {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
#elif defined(__linux__)
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : self.parent_path();
#else
    return {};
#endif
}

namespace {

#ifndef _WIN32
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Directory -> (lower-case entry name -> the entry's real name), filled on first use. The game data
// does not change while the engine runs; files the engine writes (saves, settings) are looked up
// as written.
struct DirCache {
    std::mutex mutex;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dirs;

    const std::string* find(const fs::path& dir, const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = dirs.find(dir.string());
        if (it == dirs.end())
        {
            std::unordered_map<std::string, std::string> entries;
            std::error_code ec;
            for (fs::directory_iterator d(dir.empty() ? fs::path(".") : dir, ec), end; !ec && d != end; d.increment(ec))
            {
                const std::string real = d->path().filename().string();
                entries.emplace(lower(real), real);
            }
            it = dirs.emplace(dir.string(), std::move(entries)).first;
        }
        const auto e = it->second.find(lower(name));
        return e == it->second.end() ? nullptr : &e->second;
    }
};

DirCache& cache()
{
    static DirCache c;
    return c;
}
#endif

} // namespace

std::string resolvePath(const std::string& path)
{
#ifdef _WIN32
    return path;
#else
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::error_code ec;
    if (p.empty() || fs::exists(p, ec))
        return p;
    const fs::path in(p);
    fs::path out = in.root_path();
    bool lost = false; // a component did not match: the rest is kept as written
    for (const fs::path& part : in.relative_path())
    {
        const std::string name = part.string();
        if (lost || name.empty() || name == "." || name == "..")
        {
            out /= part;
            continue;
        }
        if (fs::exists(out / part, ec))
        {
            out /= part;
            continue;
        }
        if (const std::string* real = cache().find(out, name))
            out /= *real;
        else
        {
            out /= part;
            lost = true;
        }
    }
    return out.string();
#endif
}

bool isGameRoot(const fs::path& dir)
{
    if (dir.empty())
        return false;
    std::error_code ec;
    for (const char* sub : {"levels", "audio", "pictures"})
        if (!fs::is_directory(resolvePath((dir / sub).string()), ec))
            return false;
    return true;
}

} // namespace Engine::Platform
