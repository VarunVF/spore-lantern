#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <glgame2d/Mixer.hpp>

// Sound effects and a looping ambience on top of the engine's Mixer.
//
// Only Mixer::play(const char*) is used: it is fire-and-forget. The Mixer has
// no looping, volume or stop, so volumes are baked into the files and the
// ambience is a file with a fade-in head and fade-out tail that is started
// again every MUSIC_PERIOD seconds so the copies cross-fade into each other.
class Audio
{
public:
    static constexpr float MUSIC_PERIOD = 24.0f;

    explicit Audio(bool enabled)
    {
        if (enabled)
            m_Mixer.emplace();
    }

    void play(const std::string& name)
    {
        if (!m_Mixer)
            return;
        const std::string path = "assets/sfx/" + name + ".wav";
        if (std::filesystem::exists(path))
            m_Mixer->play(path.c_str());
    }

    // Call once per frame with real elapsed seconds.
    void update(float deltaTime)
    {
        m_MusicTimer -= deltaTime;
        if (m_MusicTimer <= 0.0f)
        {
            play("ambience");
            m_MusicTimer += MUSIC_PERIOD;
            if (m_MusicTimer < 0.0f)
                m_MusicTimer = MUSIC_PERIOD;
        }
    }

private:
    std::optional<glgame2d::Mixer> m_Mixer;
    float m_MusicTimer = 0.5f;
};
