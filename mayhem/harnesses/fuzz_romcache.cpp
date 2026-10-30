// Fuzzes RMG-Core's ROM header & settings cache file reader (and writer on whatever it read).
// CachedRomHeaderAndSettings.cpp is included so the cache entries can be reset between inputs.
#include "CachedRomHeaderAndSettings.cpp"
#include "tmpfile.hpp"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    // CoreGetUserCacheDirectory() resolves to $XDG_CACHE_HOME/RMG
    const std::string cacheHome = scratch_dir() + "/cache";
    setenv("XDG_CACHE_HOME", cacheHome.c_str(), 1);
    std::filesystem::create_directories(cacheHome + "/RMG");
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!write_scratch_file(get_cache_file_name().string(), data, size))
    {
        std::abort();
    }

    l_CacheEntries.clear();
    CoreReadRomHeaderAndSettingsCache();
    const bool hadEntries = !l_CacheEntries.empty();

    l_CacheEntriesChanged = true;
    CoreSaveRomHeaderAndSettingsCache();

    return hadEntries ? 0 : -1;
}
