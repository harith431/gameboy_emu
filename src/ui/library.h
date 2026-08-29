#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A single entry in the game library.
struct GameEntry {
    std::string path;     // filesystem path to the ROM
    std::string filename; // file name (without directory)
    std::string title;    // game title read from the ROM header (or filename)
    uint64_t size = 0;    // file size in bytes
};

// Scan a directory for *.gb / *.gbc ROMs. The directory is created if it
// does not exist. Entries are sorted case-insensitively by title.
std::vector<GameEntry> scan_library(const std::string& rom_dir);

// Read the 16-byte title from a ROM header (offset 0x134). Returns a cleaned
// title, or the file stem if the header is empty.
std::string read_rom_title(const std::string& path);

// Copy a ROM file into the library directory. Returns true on success and
// fills `out_error` on failure.
bool import_rom(const std::string& src_path, const std::string& rom_dir,
                std::string& out_error);

// Interactive library screen. Lists the games in `rom_dir`, lets the user
// import new ROMs and pick one to play. Returns the selected ROM path, or an
// empty string if the user quit.
//
// SDL video must already be initialized (init_video()).
std::string run_library_screen(const std::string& rom_dir);
