#include "Save.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace save {

namespace {
constexpr const char* MAGIC = "spore-lantern-save";
constexpr int VERSION = 1;

// In the browser the save directory is an in-memory copy of IndexedDB
// (mounted in main.cpp); this copies the change back so it survives a reload.
void persist()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        FS.syncfs(false, function(err) {
            if (err) console.warn("Spore Lantern: could not store the save", err);
        });
    });
#endif
}
}

bool exists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool read(const std::string& path, SaveData& out)
{
    std::ifstream in{ path };
    if (!in.is_open())
        return false;

    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != MAGIC || version != VERSION)
        return false;

    SaveData data;
    bool havePlayer = false;
    std::string key;
    while (in >> key)
    {
        if (key == "player") {
            havePlayer = static_cast<bool>(in >> data.x >> data.y);
        }
        else if (key == "spores") {
            for (bool& spore : data.spores) {
                int v = 0;
                in >> v;
                spore = (v != 0);
            }
        }
        else if (key == "potion") { int v = 0; in >> v; data.potion = (v != 0); }
        else if (key == "slime_dead") { int v = 0; in >> v; data.slimeDead = (v != 0); }
        else if (key == "hearts") { in >> data.hearts; }
        else if (key == "seconds") { in >> data.seconds; }
        else if (key == "deaths") { in >> data.deaths; }
        else {
            // unknown key: skip the rest of the line
            std::string rest;
            std::getline(in, rest);
        }
        if (in.fail())
            return false;
    }

    if (!havePlayer || !std::isfinite(data.x) || !std::isfinite(data.y))
        return false;
    if (data.hearts < 1 || data.hearts > 3)
        data.hearts = 3;
    if (!std::isfinite(data.seconds) || data.seconds < 0.0f)
        data.seconds = 0.0f;
    if (data.deaths < 0)
        data.deaths = 0;

    out = data;
    return true;
}

bool write(const std::string& path, const SaveData& data)
{
    std::ostringstream text;
    text << MAGIC << ' ' << VERSION << '\n'
         << "player " << data.x << ' ' << data.y << '\n'
         << "spores " << data.spores[0] << ' ' << data.spores[1] << ' ' << data.spores[2] << '\n'
         << "potion " << data.potion << '\n'
         << "slime_dead " << data.slimeDead << '\n'
         << "hearts " << data.hearts << '\n'
         << "seconds " << data.seconds << '\n'
         << "deaths " << data.deaths << '\n';

    bool ok = false;
    {
        std::ofstream out{ path, std::ios::trunc };
        if (!out.is_open())
            return false;
        out << text.str();
        ok = static_cast<bool>(out);
    }
    persist();
    return ok;
}

void erase(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    persist();
}

} // namespace save
