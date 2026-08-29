#include "ui/library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <direct.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "ppu/video.h"
#include "ui/font.h"

// ---------------------------------------------------------------------------
// Small 5x7 text renderer (no SDL_ttf dependency)
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t COL_BG      = 0x081820; // darkest DMG green
constexpr uint32_t COL_TEXT    = 0xE0F8D0; // lightest DMG green
constexpr uint32_t COL_DIM     = 0x88C070; // light DMG green
constexpr uint32_t COL_HILITE  = 0x346856; // dark DMG green (selection bar)
constexpr uint32_t COL_ACCENT  = 0xE0F8D0;

void set_color(SDL_Renderer* r, uint32_t c) {
    SDL_SetRenderDrawColor(r, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 0xFF);
}

int text_width(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale;
}

void draw_text(SDL_Renderer* r, int x, int y, const std::string& text,
               int scale, uint32_t color) {
    set_color(r, color);
    int cx = x;
    for (unsigned char ch : text) {
        int idx = ch - 0x20;
        const uint8_t* g = (idx < 0 || idx >= 95) ? FONT5X7['?' - 0x20] : FONT5X7[idx];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    SDL_Rect rc = { cx + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(r, &rc);
                }
            }
        }
        cx += 6 * scale;
    }
}

void draw_text_centered(SDL_Renderer* r, int y, const std::string& text,
                        int scale, uint32_t color) {
    int w = text_width(text, scale);
    int x = (SCREEN_WIDTH * SCALE - w) / 2;
    if (x < 0) x = 0;
    draw_text(r, x, y, text, scale, color);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string printable_only(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c <= 0x7E) out += static_cast<char>(c);
    }
    return out;
}

std::string truncate(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 3) + "...";
}

// ---------------------------------------------------------------------------
// Minimal filesystem helpers (dirent.h / sys/stat.h — no std::filesystem)
// ---------------------------------------------------------------------------

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

std::string base_name(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string stem_of(const std::string& path) {
    std::string base = base_name(path);
    size_t pos = base.find_last_of('.');
    if (pos == std::string::npos) return base;
    return base.substr(0, pos);
}

std::string ext_of(const std::string& path) {
    std::string base = base_name(path);
    size_t pos = base.find_last_of('.');
    if (pos == std::string::npos) return "";
    return to_lower(base.substr(pos));
}

// Returns the parent directory, or "" when there is none (filesystem root).
std::string parent_dir(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    size_t pos = p.find_last_of("/\\");
    if (pos == std::string::npos) return "";      // single component ("roms")
    if (pos == 0) return "";                       // at "/"
    std::string parent = p.substr(0, pos);
    if (parent.size() == 2 && parent[1] == ':') return ""; // drive root "C:"
    return parent;
}

bool is_dir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFDIR) != 0;
}

bool is_regular_file(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFREG) != 0;
}

uint64_t get_file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
}

std::string current_dir() {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return ".";
    std::string s(buf);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool ensure_dir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return (st.st_mode & S_IFDIR) != 0;
    return _mkdir(path.c_str()) == 0;
}

bool copy_file(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

std::string abs_path(const std::string& path) {
    char buf[4096];
    if (_fullpath(buf, path.c_str(), sizeof(buf))) {
        std::string s(buf);
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }
    return path;
}

struct DirEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    uint64_t size = 0;
};

std::vector<DirEntry> list_dir(const std::string& path) {
    std::vector<DirEntry> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        DirEntry de;
        de.name = name;
        de.path = join_path(path, name);
        de.is_dir = is_dir(de.path);
        de.size = de.is_dir ? 0 : get_file_size(de.path);
        out.push_back(de);
    }
    closedir(d);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Library scanning / import
// ---------------------------------------------------------------------------

std::string read_rom_title(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    char buf[16] = {0};
    f.seekg(0x134);
    f.read(buf, 16);
    std::string title(buf, 16);
    title = printable_only(title);
    while (!title.empty() && (title.back() == ' ' || title.back() == '\0'))
        title.pop_back();
    if (title.empty()) title = stem_of(path);
    return title;
}

std::vector<GameEntry> scan_library(const std::string& rom_dir) {
    std::vector<GameEntry> games;
    ensure_dir(rom_dir);

    for (const DirEntry& de : list_dir(rom_dir)) {
        if (de.is_dir) continue;
        std::string ext = ext_of(de.name);
        if (ext != ".gb" && ext != ".gbc") continue;

        GameEntry g;
        g.path = de.path;
        g.filename = de.name;
        g.title = read_rom_title(g.path);
        g.size = de.size;
        games.push_back(std::move(g));
    }

    std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
        return to_lower(a.title) < to_lower(b.title);
    });
    return games;
}

bool import_rom(const std::string& src_path, const std::string& rom_dir,
                std::string& out_error) {
    if (!is_regular_file(src_path)) {
        out_error = "source is not a file: " + src_path;
        return false;
    }
    ensure_dir(rom_dir);
    std::string dest = join_path(rom_dir, base_name(src_path));

    // No-op if the file is already inside the library.
    if (to_lower(abs_path(src_path)) == to_lower(abs_path(dest))) return true;

    if (!copy_file(src_path, dest)) {
        out_error = "could not copy file";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// File browser (used by the "Import" action)
// ---------------------------------------------------------------------------

namespace {

struct BrowserEntry {
    std::string name;
    std::string path;
    bool is_dir = false;
    uint64_t size = 0;
};

std::vector<BrowserEntry> list_browser_entries(const std::string& dir) {
    std::vector<BrowserEntry> dirs, files;
    for (const DirEntry& de : list_dir(dir)) {
        BrowserEntry be;
        be.name = de.name;
        be.path = de.path;
        be.is_dir = de.is_dir;
        be.size = de.size;
        if (be.is_dir) dirs.push_back(be);
        else {
            std::string ext = ext_of(de.name);
            if (ext == ".gb" || ext == ".gbc") files.push_back(be);
        }
    }
    auto cmp = [](const BrowserEntry& a, const BrowserEntry& b) {
        return to_lower(a.name) < to_lower(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    std::vector<BrowserEntry> all;
    all.reserve(dirs.size() + files.size() + 1);
    std::string up = parent_dir(dir);
    if (!up.empty()) {
        BrowserEntry e;
        e.name = "..";
        e.path = up;
        e.is_dir = true;
        all.push_back(e);
    }
    all.insert(all.end(), dirs.begin(), dirs.end());
    all.insert(all.end(), files.begin(), files.end());
    return all;
}

void render_list_view(SDL_Renderer* r, const std::string& header,
                      const std::string& path_line,
                      const std::vector<std::string>& rows,
                      int selected, int scroll, int max_visible) {
    set_color(r, COL_BG);
    SDL_RenderClear(r);

    const int rscale = 3;         // list rows
    const int hscale = 2;         // header / path
    const int row_h = 8 * rscale; // 24 px per row

    draw_text(r, 12, 10, header, hscale, COL_TEXT);
    if (!path_line.empty())
        draw_text(r, 12, 10 + 8 * hscale + 4, path_line, 2, COL_DIM);

    int list_y = 10 + 2 * (8 * hscale) + 10;
    for (int i = 0; i < (int)rows.size() && i < scroll + max_visible; i++) {
        if (i < scroll) continue;
        int y = list_y + (i - scroll) * row_h;
        bool is_sel = (i == selected);
        if (is_sel) {
            SDL_Rect bar = { 8, y - 3, SCREEN_WIDTH * SCALE - 16, row_h };
            set_color(r, COL_HILITE);
            SDL_RenderFillRect(r, &bar);
        }
        draw_text(r, 14, y, rows[i], rscale, is_sel ? COL_ACCENT : COL_TEXT);
    }

    set_color(r, COL_DIM);
    SDL_Rect footer_bar = { 8, SCREEN_HEIGHT * SCALE - 28, SCREEN_WIDTH * SCALE - 16, 1 };
    SDL_RenderFillRect(r, &footer_bar);
}

std::string browse_for_rom() {
    std::string dir = current_dir();
    int selected = 0;
    int scroll = 0;
    const int max_visible = 20;
    std::string result;

    bool running = true;
    while (running) {
        std::vector<BrowserEntry> entries = list_browser_entries(dir);

        if (selected >= (int)entries.size()) selected = (int)entries.size() - 1;
        if (selected < 0) selected = 0;
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + max_visible) scroll = selected - max_visible + 1;
        if (scroll < 0) scroll = 0;

        std::vector<std::string> rows;
        for (const auto& e : entries) {
            std::string marker = e.is_dir ? " [DIR]" : "";
            rows.push_back(truncate(e.name, 52) + marker);
        }

        render_list_view(renderer, "IMPORT ROM - pick a .gb/.gbc file",
                         truncate(dir, 100), rows, selected, scroll, max_visible);
        draw_text(renderer, 12, SCREEN_HEIGHT * SCALE - 18,
                  "UP/DOWN select   ENTER open   BACKSPACE up   ESC cancel",
                  2, COL_DIM);
        SDL_RenderPresent(renderer);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; result.clear(); }
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_UP: if (selected > 0) selected--; break;
                    case SDLK_DOWN: if (selected < (int)entries.size() - 1) selected++; break;
                    case SDLK_PAGEUP: selected -= 10; if (selected < 0) selected = 0; break;
                    case SDLK_PAGEDOWN: selected += 10; if (selected >= (int)entries.size()) selected = (int)entries.size() - 1; break;
                    case SDLK_RETURN:
                        if (entries.empty()) break;
                        if (entries[selected].is_dir) {
                            dir = entries[selected].path;
                            selected = 0; scroll = 0;
                        } else {
                            result = entries[selected].path;
                            running = false;
                        }
                        break;
                    case SDLK_BACKSPACE: {
                        std::string up = parent_dir(dir);
                        if (!up.empty()) { dir = up; selected = 0; scroll = 0; }
                        break;
                    }
                    case SDLK_ESCAPE:
                        running = false;
                        result.clear();
                        break;
                }
            }
        }
        SDL_Delay(16);
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Library screen
// ---------------------------------------------------------------------------

std::string run_library_screen(const std::string& rom_dir) {
    set_window_title("Game Boy Emulator - Library");
    std::vector<GameEntry> games = scan_library(rom_dir);
    int selected = 0;
    int scroll = 0;
    const int max_visible = 20;

    std::string result;
    bool running = true;
    bool confirming_delete = false;

    auto clamp = [&]() {
        if (selected < 0) selected = 0;
        if (!games.empty() && selected >= (int)games.size()) selected = (int)games.size() - 1;
        if (games.empty()) selected = 0;
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + max_visible) scroll = selected - max_visible + 1;
        if (scroll < 0) scroll = 0;
    };
    clamp();

    while (running) {
        set_color(renderer, COL_BG);
        SDL_RenderClear(renderer);

        draw_text_centered(renderer, 12, "GAME BOY LIBRARY", 4, COL_TEXT);

        std::string sub = std::to_string(games.size()) + " game(s) in library  [" + rom_dir + "]";
        draw_text_centered(renderer, 48, sub, 2, COL_DIM);

        int list_y = 84;
        const int row_h = 24;
        if (games.empty()) {
            draw_text(renderer, 14, list_y, "No games yet.", 3, COL_TEXT);
            draw_text(renderer, 14, list_y + 32, "Press I to import a ROM from disk.", 2, COL_DIM);
        } else {
            for (int i = scroll; i < (int)games.size() && i < scroll + max_visible; i++) {
                int y = list_y + (i - scroll) * row_h;
                bool is_sel = (i == selected);
                if (is_sel) {
                    SDL_Rect bar = { 8, y - 3, SCREEN_WIDTH * SCALE - 16, row_h };
                    set_color(renderer, COL_HILITE);
                    SDL_RenderFillRect(renderer, &bar);
                }
                std::string line = truncate(games[i].title, 40);
                if (to_lower(games[i].title) != to_lower(games[i].filename))
                    line += "  (" + truncate(games[i].filename, 20) + ")";
                std::string prefix = is_sel ? "> " : "  ";
                draw_text(renderer, 14, y, prefix + line, 3, is_sel ? COL_ACCENT : COL_TEXT);
            }
        }

        set_color(renderer, COL_DIM);
        SDL_Rect footer_bar = { 8, SCREEN_HEIGHT * SCALE - 28, SCREEN_WIDTH * SCALE - 16, 1 };
        SDL_RenderFillRect(renderer, &footer_bar);

        std::string footer;
        if (confirming_delete) {
            footer = "Delete '" + truncate(games[selected].title, 30) + "'?  Y = yes, any other key = no";
        } else {
            footer = "ENTER play   I import   DEL remove   ESC quit";
        }
        draw_text(renderer, 12, SCREEN_HEIGHT * SCALE - 18, footer, 2, COL_TEXT);

        SDL_RenderPresent(renderer);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                result.clear();
            } else if (e.type == SDL_KEYDOWN) {
                if (confirming_delete) {
                    if (e.key.keysym.sym == SDLK_y) {
                        remove(games[selected].path.c_str());
                        games = scan_library(rom_dir);
                        clamp();
                    }
                    confirming_delete = false;
                    continue;
                }
                switch (e.key.keysym.sym) {
                    case SDLK_UP: selected--; clamp(); break;
                    case SDLK_DOWN: selected++; clamp(); break;
                    case SDLK_PAGEUP: selected -= 10; clamp(); break;
                    case SDLK_PAGEDOWN: selected += 10; clamp(); break;
                    case SDLK_RETURN:
                        if (!games.empty()) {
                            result = games[selected].path;
                            running = false;
                        }
                        break;
                    case SDLK_i: {
                        std::string picked = browse_for_rom();
                        if (!picked.empty()) {
                            std::string err;
                            if (import_rom(picked, rom_dir, err)) {
                                games = scan_library(rom_dir);
                                clamp();
                            } else {
                                draw_text(renderer, 12, SCREEN_HEIGHT * SCALE - 18,
                                          "Import failed: " + err, 2, COL_TEXT);
                                SDL_RenderPresent(renderer);
                                SDL_Delay(1200);
                            }
                        }
                        break;
                    }
                    case SDLK_DELETE:
                    case SDLK_BACKSPACE:
                        if (!games.empty()) confirming_delete = true;
                        break;
                    case SDLK_ESCAPE:
                        running = false;
                        result.clear();
                        break;
                }
            }
        }
        SDL_Delay(16);
    }
    return result;
}
