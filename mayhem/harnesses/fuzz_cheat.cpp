// Fuzzes RMG-Core's .cht cheat file parser, and the cheat line/option helpers on whatever it accepts.
// Cheats.cpp is included so the file-local parser entry points can be called directly.
#include "Cheats.cpp"
#include "tmpfile.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string path = scratch_dir() + "/input.cht";
    if (!write_scratch_file(path, data, size))
    {
        std::abort();
    }

    std::vector<std::string> lines;
    if (!read_file_lines(path, lines))
    {
        return -1;
    }

    CoreCheatFile cheatFile;
    const bool fileOk = parse_cheat_file(lines, cheatFile);

    CoreCheat cheat;
    const bool cheatOk = CoreParseCheat(lines, cheat);

    std::vector<CoreCheat> cheats;
    if (fileOk)
    {
        cheats = cheatFile.Cheats;
    }
    if (cheatOk)
    {
        cheats.push_back(cheat);
    }

    for (const CoreCheat& c : cheats)
    {
        std::vector<std::string> codeLines;
        std::vector<std::string> optionLines;
        CoreGetCheatLines(c, codeLines, optionLines);

        for (const CoreCheatCode& code : c.CheatCodes)
        {
            for (const CoreCheatOption& option : c.CheatOptions)
            {
                int32_t combined;
                combine_cheat_code_and_option(code, option, combined);
            }
        }
    }

    return (fileOk || cheatOk) ? 0 : -1;
}
