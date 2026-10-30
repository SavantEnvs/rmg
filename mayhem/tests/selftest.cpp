// Known-answer tests for the RMG-Core parsers the fuzz harnesses exercise (cheat files, zip/7z ROM
// extraction, ROM header cache). Built with the project's normal flags and run by mayhem/test.sh.
#include "Cheats.cpp"
#include "CachedRomHeaderAndSettings.cpp"
#include "Archive.hpp"

#include <cstdlib>
#include <iostream>

static int l_Failed = 0;

static void check(const char* name, bool ok)
{
    std::cout << (ok ? "TEST PASS " : "TEST FAIL ") << name << std::endl;
    if (!ok)
    {
        l_Failed++;
    }
}

static std::vector<char> rom_pattern(void)
{
    std::vector<char> pattern(256);
    for (size_t i = 0; i < pattern.size(); i++)
    {
        pattern[i] = static_cast<char>((i * 7 + 3) & 0xFF);
    }
    return pattern;
}

static void test_cheats(void)
{
    CoreCheat cheat;
    check("cheat_simple_parse",
          CoreParseCheat({"$Infinite Health", "Author=Bob", "Note=hello world", "801CC8B9 0001", "81234568 FFFF"}, cheat) &&
          cheat.Name == "Infinite Health" && cheat.Author == "Bob" && cheat.Note == "hello world" &&
          !cheat.HasOptions && cheat.CheatCodes.size() == 2 &&
          cheat.CheatCodes[0].Address == 0x801CC8B9 && cheat.CheatCodes[0].Value == 1 && !cheat.CheatCodes[0].UseOptions &&
          cheat.CheatCodes[1].Address == 0x81234568 && cheat.CheatCodes[1].Value == 0xFFFF);

    CoreCheat oldValue;
    check("cheat_old_value_stripped",
          CoreParseCheat({"$Old", "80000000 0003:0002"}, oldValue) &&
          oldValue.CheatCodes.size() == 1 && oldValue.CheatCodes[0].Value == 3);

    CoreCheat options;
    const bool optionsOk = CoreParseCheat({"$Select Level", "8010A000 00??", "00 Level 1", "01 Level 2"}, options);
    check("cheat_options_parse",
          optionsOk && options.HasOptions && options.CheatCodes.size() == 1 &&
          options.CheatCodes[0].UseOptions && options.CheatCodes[0].OptionIndex == 2 && options.CheatCodes[0].OptionSize == 2 &&
          options.CheatOptions.size() == 2 &&
          options.CheatOptions[0].Name == "Level 1" && options.CheatOptions[0].Value == 0 && options.CheatOptions[0].Size == 2 &&
          options.CheatOptions[1].Name == "Level 2" && options.CheatOptions[1].Value == 1);

    int32_t combined = -1;
    check("cheat_option_combine",
          optionsOk && combine_cheat_code_and_option(options.CheatCodes[0], options.CheatOptions[1], combined) && combined == 0x0001);

    std::vector<std::string> codeLines, optionLines;
    check("cheat_lines_roundtrip",
          optionsOk && CoreGetCheatLines(options, codeLines, optionLines) &&
          codeLines == std::vector<std::string>{"8010A000 00??"} &&
          optionLines == std::vector<std::string>{"00 Level 1", "01 Level 2"});

    CoreCheat bad;
    check("cheat_invalid_line_rejected", !CoreParseCheat({"$Bad", "zz"}, bad));
    CoreCheat noCodes;
    check("cheat_without_codes_rejected", !CoreParseCheat({"$Empty"}, noCodes));

    CoreCheatFile file;
    check("cheat_file_parse",
          parse_cheat_file({"[04DAF07F-0D18E688-C:45]", "Name=Test Game (U)", "", "$Cheat A", "801CC8B9 0001", "", "$Cheat B", "Author=Eve", "801CC8BA 0002"}, file) &&
          file.CRC1 == 0x04DAF07F && file.CRC2 == 0x0D18E688 && file.CountryCode == 0x45 && file.Name == "Test Game (U)" &&
          file.Cheats.size() == 2 && file.Cheats[0].Name == "Cheat A" && file.Cheats[1].Name == "Cheat B" &&
          file.Cheats[1].Author == "Eve" && file.Cheats[1].CheatCodes[0].Value == 2);

    CoreCheatFile md5File;
    check("cheat_file_md5_header",
          parse_cheat_file({"[0123456789ABCDEF0123456789ABCDEF]", "Name=Disk"}, md5File) &&
          md5File.MD5 == "0123456789ABCDEF0123456789ABCDEF" && md5File.Cheats.empty());

    CoreCheatFile badHeader;
    check("cheat_file_bad_header_rejected", !parse_cheat_file({"[1234]", "Name=x"}, badHeader));
    CoreCheatFile noName;
    check("cheat_file_missing_name_rejected", !parse_cheat_file({"[04DAF07F-0D18E688-C:45]"}, noName));
}

static void test_archive(const std::filesystem::path& fixtures, const char* ext)
{
    const std::string e = ext;
    std::filesystem::path name;
    bool isDisk = true;
    std::vector<char> buffer;

    check(("archive_rom_" + e).c_str(),
          CoreReadArchiveFile(fixtures / ("rom." + e), name, isDisk, buffer) &&
          name.filename() == "game.z64" && !isDisk && buffer == rom_pattern());

    name.clear(); isDisk = false; buffer.clear();
    check(("archive_disk_" + e).c_str(),
          CoreReadArchiveFile(fixtures / ("disk." + e), name, isDisk, buffer) &&
          name.filename() == "game.ndd" && isDisk && buffer == rom_pattern());

    name.clear(); buffer.clear();
    check(("archive_without_rom_rejected_" + e).c_str(),
          !CoreReadArchiveFile(fixtures / ("none." + e), name, isDisk, buffer) && buffer.empty());

    check(("archive_missing_file_rejected_" + e).c_str(),
          !CoreReadArchiveFile(fixtures / ("missing." + e), name, isDisk, buffer));
}

static void test_cache(void)
{
    char dirTemplate[] = "/tmp/rmg-selftest-XXXXXX";
    const char* dir = mkdtemp(dirTemplate);
    if (dir == nullptr)
    {
        check("cache_scratch_dir", false);
        return;
    }
    setenv("XDG_CACHE_HOME", dir, 1);
    std::filesystem::create_directories(std::string(dir) + "/RMG");

    CoreRomHeader header;
    header.CRC1 = 0x11223344;
    header.CRC2 = 0x55667788;
    header.CountryCode = 0x45;
    header.Name = "SELFTEST ROM";
    header.GameID = "NST";
    header.Region = "North America";
    header.SystemType = CoreSystemType::PAL;

    CoreRomSettings settings;
    settings.GoodName = "Selftest Rom (U)";
    settings.MD5 = "0123456789ABCDEF0123456789ABCDEF";
    settings.SaveType = 2;
    settings.DisableExtraMem = true;
    settings.TransferPak = false;
    settings.CountPerOp = 3;
    settings.SiDMADuration = 4000;

    CoreRomSettings defaults = settings;
    defaults.CountPerOp = 2;

    l_CacheEntries.clear();
    add_cache_entry("/roms/a.z64", CoreRomType::Cartridge, header, defaults, settings);
    add_invalid_cache_entry("/roms/broken.z64");
    check("cache_save", CoreSaveRomHeaderAndSettingsCache() && l_CacheEntries.size() == 2);

    l_CacheEntries.clear();
    CoreReadRomHeaderAndSettingsCache();
    // the reader's `while (!eof())` loop appends one empty trailing entry, so only the first two are checked
    check("cache_read_back",
          l_CacheEntries.size() >= 2 &&
          l_CacheEntries[0].fileName == "/roms/a.z64" && l_CacheEntries[0].valid &&
          l_CacheEntries[0].type == CoreRomType::Cartridge &&
          l_CacheEntries[0].header == header &&
          l_CacheEntries[0].settings.GoodName == "Selftest Rom (U)" &&
          l_CacheEntries[0].settings.MD5 == "0123456789ABCDEF0123456789ABCDEF" &&
          l_CacheEntries[0].settings.SaveType == 2 && l_CacheEntries[0].settings.DisableExtraMem &&
          l_CacheEntries[0].settings.CountPerOp == 3 && l_CacheEntries[0].settings.SiDMADuration == 4000 &&
          l_CacheEntries[0].defaultSettings.CountPerOp == 2 &&
          l_CacheEntries[1].fileName == "/roms/broken.z64" && !l_CacheEntries[1].valid);

    std::filesystem::remove_all(dir);
}

int main(int argc, char** argv)
{
    const std::filesystem::path fixtures = (argc > 1) ? argv[1] : "mayhem/tests/fixtures";

    test_cheats();
    test_archive(fixtures, "zip");
    test_archive(fixtures, "7z");
    test_cache();

    std::cout << "selftest done, failed=" << l_Failed << std::endl;
    return l_Failed == 0 ? 0 : 1;
}
