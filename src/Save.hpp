#pragma once

#include <array>
#include <string>

// What a shrine remembers. Stored as plain text in save.txt.
struct SaveData
{
    float x = 0.0f, y = 0.0f;               // player top-left, world space
    std::array<bool, 3> spores{};           // which spores are carried
    bool potion = false;                    // a brewed potion is carried
    bool slimeDead = false;
    int hearts = 3;
    float seconds = 0.0f;                   // play time so far
    int deaths = 0;
};

namespace save {

bool exists(const std::string& path);
bool read(const std::string& path, SaveData& out);
bool write(const std::string& path, const SaveData& data);
void erase(const std::string& path);

} // namespace save
