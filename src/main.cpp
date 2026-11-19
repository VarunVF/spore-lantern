#include <algorithm>
#include <chrono>
#include <cstdio>
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
//   SPORE_SCRIPT=<file>   one step per line: "<condition> <action> [0|1]"
//   SPORE_SPEED=<n>       run the simulation n times faster
//   SPORE_TRACE=1         log the player position a few times a second
//   SPORE_MUTE=1          do not open an audio device (same as --mute)
//   SPORE_ENGINE_TILEMAP=1  start with the map drawn by Tilemap::render() (F5)
//   SPORE_PERF=1          log frame work time and sprite count every 5 s
// Steps run in order; each waits for its condition, then applies its action.
// Conditions: "12.5" (simulation time), "+0.3" (seconds after the previous
// step), "x>100" / "x<100" / "y>-500" / "y<-500" (player position),
// "sx>100" / "sx<100" (slime position), "ground" (player standing).
// Actions: left right jump (held, 0|1), brew throw confirm restart newgame
// back (presses), quit.
struct Script
{
    struct Step { std::string condition, action; bool down; };

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
            Step step{ "", "", true };
            int down = 1;
            if (!(fields >> step.condition >> step.action))
                continue;
            fields >> down;
            step.down = (down != 0);
            steps.push_back(step);
        }
        return true;
    }

    bool ready(const std::string& c, float now, const Game& game) const
    {
        const glm::vec2 p = game.playerPosition();
        auto number = [&](size_t from) { return std::strtof(c.c_str() + from, nullptr); };
        if (c == "ground")          return game.playerGrounded();
        if (c.starts_with("+"))     return now >= lastTime + number(1);
        if (c.starts_with("x>"))    return p.x > number(2);
        if (c.starts_with("x<"))    return p.x < number(2);
        if (c.starts_with("y>"))    return p.y > number(2);
        if (c.starts_with("y<"))    return p.y < number(2);
        if (c.starts_with("sx>"))   return game.slimeX() > number(3);
        if (c.starts_with("sx<"))   return game.slimeX() < number(3);
        return now >= number(0);
    }

    // Runs every step that is due. Returns false when the script asks to quit.
    bool apply(float now, const Game& game, Controls& c)
    {
        while (next < steps.size() && ready(steps[next].condition, now, game))
        {
            const Step& e = steps[next++];
            lastTime = now;
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
            glgame2d::logging::info("[Script] {:.2f}s: {} {} {}", now, e.condition, e.action, d ? 1 : 0);
        }
        return true;
    }

    std::vector<Step> steps;
    size_t next = 0;
    float lastTime = 0.0f;
};

// Frame statistics for SPORE_PERF: the time each frame spends on its own
// work (input, simulation, drawing, swapping, events), without the sleep that
// caps the frame rate.
struct PerfLog
{
    using SteadyClock = std::chrono::steady_clock;

    void frameStart() { start = SteadyClock::now(); }

    void frameEnd(int sprites)
    {
        const auto now = SteadyClock::now();
        work.push_back(std::chrono::duration<float, std::milli>(now - start).count());
        spriteSum += sprites;
        if (now - windowStart < std::chrono::seconds(5))
            return;
        std::vector<float> sorted = work;
        std::sort(sorted.begin(), sorted.end());
        auto pct = [&](float p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * sorted.size()))]; };
        const float seconds = std::chrono::duration<float>(now - windowStart).count();
        glgame2d::logging::info("[Perf] fps {:.1f}  work p50 {:.2f} ms  p95 {:.2f} ms  max {:.2f} ms  sprites {}",
            work.size() / seconds, pct(0.5f), pct(0.95f), sorted.back(), spriteSum / static_cast<long>(work.size()));
        work.clear();
        spriteSum = 0;
        windowStart = now;
    }

    SteadyClock::time_point start{}, windowStart = SteadyClock::now();
    std::vector<float> work;
    long spriteSum = 0;
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

    // Command line: --fullscreen, --size WIDTHxHEIGHT, --mute
    int width = Gfx::BASE_W, height = Gfx::BASE_H;
    bool fullscreen = false;
    bool mute = std::getenv("SPORE_MUTE") != nullptr;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--fullscreen")
            fullscreen = true;
        else if (arg == "--mute")
            mute = true;
        else if (arg == "--size" && i + 1 < argc)
        {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240)
            {
                width = w;
                height = h;
            }
        }
        else
        {
            std::printf("usage: %s [--fullscreen] [--size WIDTHxHEIGHT] [--mute]\n", argv[0]);
            return arg == "--help" ? 0 : 1;
        }
    }

    Script script;
    const char* scriptPath = std::getenv("SPORE_SCRIPT");
    const bool scripted = scriptPath && script.load(scriptPath);
    const float speed = std::getenv("SPORE_SPEED") ? std::max(1.0f, std::strtof(std::getenv("SPORE_SPEED"), nullptr)) : 1.0f;

    glgame2d::Window window{ width, height, "Spore Lantern",
        fullscreen ? glgame2d::Window::WINDOWED_FULLSCREEN : glgame2d::Window::WINDOWED };
    // The icon must be set before any Texture is created: Texture switches
    // stb_image to "flip vertically" globally and setIcon() does not undo it.
    window.setIcon("assets/gfx/icon.png");
    window.enableVSync();

    Gfx gfx{ window };
    Audio audio{ !mute };
    Game game{ gfx, audio, "save.txt" };
    game.setTrace(std::getenv("SPORE_TRACE") != nullptr);
    if (std::getenv("SPORE_ENGINE_TILEMAP"))
        game.toggleEngineTilemapRender();

    glgame2d::Input input;
    Controls controls;

    float accumulator = 0.0f;
    float simTime = 0.0f;
    bool running = true;
    glgame2d::Clock clock{ 120.0f };
    const bool perf = std::getenv("SPORE_PERF") != nullptr;
    PerfLog perfLog;

    while (running && !window.shouldClose() && !game.wantsQuit())
    {
        if (perf)
            perfLog.frameStart();
        input.begin(window);
        if (!scripted)
            readKeyboard(input, controls);
        if (input.isJustPressed(Key::KEY_F3))
            game.toggleDebug();
        if (input.isJustPressed(Key::KEY_F4))
            game.toggleCoarseLight();
        if (input.isJustPressed(Key::KEY_F5))
            game.toggleEngineTilemapRender();

        while (accumulator >= Game::STEP && running)
        {
            if (scripted)
                running = script.apply(simTime, game, controls);
            game.step(controls);
            controls.clearEdges();
            accumulator -= Game::STEP;
            simTime += Game::STEP;
        }

        game.render();
        window.pollEvents();
        if (perf)
            perfLog.frameEnd(gfx.lastDrawCount());

        const float deltaTime = clock.tick(true);
        game.setFrameTime(deltaTime);
        audio.update(deltaTime);
        // Long hitches (window drag, breakpoint) should not fast-forward the game.
        accumulator += std::min(deltaTime, 0.1f) * speed;
    }

    return 0;
}
