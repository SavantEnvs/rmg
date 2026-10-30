#ifndef MAYHEM_TMPFILE_HPP
#define MAYHEM_TMPFILE_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

// per-process scratch directory below $TMPDIR (the image dir is read-only under Mayhem)
static inline const std::string& scratch_dir(void)
{
    static const std::string dir = []()
    {
        const char* tmp = std::getenv("TMPDIR");
        std::string tmpl = std::string((tmp != nullptr && *tmp != '\0') ? tmp : "/tmp") + "/rmg-fuzz-XXXXXX";
        if (mkdtemp(tmpl.data()) == nullptr)
        {
            std::abort();
        }
        return tmpl;
    }();
    return dir;
}

static inline bool write_scratch_file(const std::string& path, const uint8_t* data, size_t size)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        return false;
    }
    bool ok = (size == 0) || (std::fwrite(data, 1, size, f) == size);
    return (std::fclose(f) == 0) && ok;
}

#endif // MAYHEM_TMPFILE_HPP
