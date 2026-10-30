// Fuzzes RMG-Core's 7z ROM extraction (the bundled LZMA SDK through CoreReadArchiveFile).
#include "Archive.hpp"
#include "tmpfile.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string path = scratch_dir() + "/input.7z";
    if (!write_scratch_file(path, data, size))
    {
        std::abort();
    }

    std::filesystem::path extractedFileName;
    bool isDisk = false;
    std::vector<char> buffer;
    if (!CoreReadArchiveFile(path, extractedFileName, isDisk, buffer))
    {
        return -1;
    }
    return 0;
}
