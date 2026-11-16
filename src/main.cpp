#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glgame2d/glgame2d.hpp>

#include "Audio.hpp"
#include "Game.hpp"
#include "Gfx.hpp"

namespace {

using Key = glgame2d::Keyboard;

// Optional scripted input, used for automated play-testing:
//   SPORE_SCRIPT=<file>   lines of "<seconds> <action> <0|1>" or "<seconds> quit"
//   SPORE_SPEED=<n>       run the simulation n times faster
//   SPORE_TRACE=1         log the player position twice a second
//   SPORE_MUTE=1          do not open an audio device
// Times are simulation seconds, so a script replays the same way every run.
struct Script
{
    struct Event { float time; std::string action; bool down; };

    bool load(const char* path)
    {
        std::ifstream in{ path };
        if (!in.is_open())
            return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream fields{ line };
            Event event{ 0.0f, "", true };
            int down = 1;
            if (!(fields >> event.time >> event.action))
                continue;
            fields >> down;
            event.down = (down != 0);
            events.push_back(event);
        }
        std::stable_sort(events.begin(), events.end(),
            [](const Event& a, const Event& b) { return a.time < b.time; });
        return true;
    }

    // Applies everything due at `now`. Returns false when the script asks to quit.
    bool apply(float now, Controls& c)
    {
        while (next < events.size() && events[next].time <= now)
        {
            const Event& e = events[next++];
            const bool d = e.down;
            if (e.action == "quit") return false;
            else if (e.action == "left")    c.left = d;
            else if (e.action == "right")   c.right = d;
            else if (e.action == "jump")    { c.jumpHeld = d; c.jump |= d; }
            else if (e.action == "brew")    c.brew |= d;
            else if (e.action == "throw")   c.toss |= d;
            else if (e.action == "confirm") c.confirm |= d;
            else if (e.action == "restart") c.restart |= d;
            else if (e.action == "newgame") c.newGame |= d;
            else if (e.action == "back")    c.back |= d;
        }
        return true;
    }

    std::vector<Event> events;
    size_t next = 0;
};

void readKeyboard(glgame2d::Input& input, Controls& c)
{
    c.left = input.isHeld(Key::KEY_A) || input.isHeld(Key::KEY_LEFT);
    c.right = input.isHeld(Key::KEY_D) || input.isHeld(Key::KEY_RIGHT);
    c.jumpHeld = input.isHeld(Key::KEY_SPACE) || input.isHeld(Key::KEY_W) || input.isHeld(Key::KEY_UP);

    // Edges are OR-ed in and only cleared once a simulation step has seen them.
    c.jump |= input.isJustPressed(Key::KEY_SPACE) || input.isJustPressed(Key::KEY_W) || input.isJustPressed(Key::KEY_UP);
    c.brew |= input.isJustPressed(Key::KEY_E);
    c.toss |= input.isJustPressed(Key::KEY_F);
    c.restart |= input.isJustPressed(Key::KEY_R);
    c.confirm |= input.isJustPressed(Key::KEY_ENTER) || input.isJustPressed(Key::KEY_KP_ENTER);
    c.newGame |= input.isJustPressed(Key::KEY_N);
    c.back |= input.isJustPressed(Key::KEY_ESCAPE);
}

} // namespace

int main(int argc, char** argv)
{
    // Assets are loaded with relative paths (the engine's Tilemap and Mixer
    // resolve against the working directory), and save.txt lives beside them,
    // so run from the executable's directory no matter where we were started.
    if (argc > 0)
    {
        std::error_code ec;
        const auto exe = std::filesystem::weakly_canonical(argv[0], ec);
        if (!ec && std::filesystem::exists(exe.parent_path() / "assets"))
            std::filesystem::current_path(exe.parent_path(), ec);
    }

    Script script;
    const char* scriptPath = std::getenv("SPORE_SCRIPT");
    const bool scripted = scriptPath && script.load(scriptPath);
    const float speed = std::getenv("SPORE_SPEED") ? std::max(1.0f, std::strtof(std::getenv("SPORE_SPEED"), nullptr)) : 1.0f;

    glgame2d::Window window{ Gfx::BASE_W, Gfx::BASE_H, "Spore Lantern" };
    // The icon must be set before any Texture is created: Texture switches
    // stb_image to "flip vertically" globally and setIcon() does not undo it.
    window.setIcon("assets/gfx/icon.png");
    window.enableVSync();

    Gfx gfx{ window };
    Audio audio{ std::getenv("SPORE_MUTE") == nullptr };
    Game game{ gfx, audio, "save.txt" };
    game.setTrace(std::getenv("SPORE_TRACE") != nullptr);

    glgame2d::Input input;
    Controls controls;

    float accumulator = 0.0f;
    float simTime = 0.0f;
    bool running = true;
    glgame2d::Clock clock{ 120.0f };

    while (running && !window.shouldClose() && !game.wantsQuit())
    {
        input.begin(window);
        if (!scripted)
            readKeyboard(input, controls);
        if (input.isJustPressed(Key::KEY_F3))
            game.toggleDebug();

        while (accumulator >= Game::STEP && running)
        {
            if (scripted)
                running = script.apply(simTime, controls);
            game.step(controls);
            controls.clearEdges();
            accumulator -= Game::STEP;
            simTime += Game::STEP;
        }

        game.render();
        window.pollEvents();

        const float deltaTime = clock.tick(true);
        game.setFrameTime(deltaTime);
        audio.update(deltaTime);
        // Long hitches (window drag, breakpoint) should not fast-forward the game.
        accumulator += std::min(deltaTime, 0.1f) * speed;
    }

    return 0;
}
