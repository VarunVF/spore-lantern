#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <glgame2d/glgame2d.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

struct Options
{
    int width = Gfx::BASE_W, height = Gfx::BASE_H;
    bool fullscreen = false;
    bool mute = false;
};

// Everything the game needs between frames. It lives on the heap: in the
// browser main() returns to the event loop and its stack frame is reused.
struct App
{
    explicit App(const Options& opt, const std::string& savePath)
    {
        const char* scriptPath = std::getenv("SPORE_SCRIPT");
        scripted = scriptPath && script.load(scriptPath);
        speed = std::getenv("SPORE_SPEED") ? std::max(1.0f, std::strtof(std::getenv("SPORE_SPEED"), nullptr)) : 1.0f;

        window = std::make_unique<glgame2d::Window>(opt.width, opt.height, "Spore Lantern",
            opt.fullscreen ? glgame2d::Window::WINDOWED_FULLSCREEN : glgame2d::Window::WINDOWED);
        // The icon must be set before any Texture is created: Texture switches
        // stb_image to "flip vertically" globally and setIcon() does not undo it.
        window->setIcon("assets/gfx/icon.png");
        window->enableVSync();

        gfx = std::make_unique<Gfx>(*window);
        audio = std::make_unique<Audio>(!opt.mute);
        game = std::make_unique<Game>(*gfx, *audio, savePath);
        game->setTrace(std::getenv("SPORE_TRACE") != nullptr);
        if (std::getenv("SPORE_ENGINE_TILEMAP"))
            game->toggleEngineTilemapRender();
    }

    // One displayed frame. Returns false when the game should stop.
    bool frame()
    {
        if (!running || window->shouldClose() || game->wantsQuit())
            return false;

        input.begin(*window);
        if (!scripted)
            readKeyboard(input, controls);
        if (input.isJustPressed(Key::KEY_F3))
            game->toggleDebug();
        if (input.isJustPressed(Key::KEY_F4))
            game->toggleCoarseLight();
        if (input.isJustPressed(Key::KEY_F5))
            game->toggleEngineTilemapRender();

        while (accumulator >= Game::STEP && running)
        {
            if (scripted)
                running = script.apply(simTime, *game, controls);
            game->step(controls);
            controls.clearEdges();
            accumulator -= Game::STEP;
            simTime += Game::STEP;
        }

        game->render();
        window->pollEvents();

#ifdef __EMSCRIPTEN__
        // The browser paces frames (requestAnimationFrame); sleeping here
        // would block the page.
        const float deltaTime = clock.tick(false);
#else
        const float deltaTime = clock.tick(true);
#endif
        game->setFrameTime(deltaTime);
        audio->update(deltaTime);
        // Long hitches (window drag, breakpoint, hidden tab) should not
        // fast-forward the game.
        accumulator += std::min(deltaTime, 0.1f) * speed;
        return running;
    }

    std::unique_ptr<glgame2d::Window> window;
    std::unique_ptr<Gfx> gfx;
    std::unique_ptr<Audio> audio;
    std::unique_ptr<Game> game;

    Script script;
    bool scripted = false;
    float speed = 1.0f;

    glgame2d::Input input;
    Controls controls;
    float accumulator = 0.0f;
    float simTime = 0.0f;
    bool running = true;
    glgame2d::Clock clock{ 120.0f };
};

#ifdef __EMSCRIPTEN__
// Saves live in IndexedDB so they survive a page reload. The browser storage
// has to be read (asynchronously) before the game starts and looks for a save.
constexpr const char* WEB_SAVE_DIR = "/persist";
bool g_StorageReady = false;
App* g_App = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void spore_storage_ready() { g_StorageReady = true; }

void webFrame()
{
    if (!g_StorageReady)
        return;
    if (!g_App)
        g_App = new App{ Options{}, std::string{ WEB_SAVE_DIR } + "/save.txt" };
    // There is nowhere to quit to in a browser tab, so a "quit" just keeps going.
    g_App->frame();
}
#endif

} // namespace

int main(int argc, char** argv)
{
#ifdef __EMSCRIPTEN__
    (void)argc; (void)argv;
    EM_ASM({
        FS.mkdir(UTF8ToString($0));
        FS.mount(IDBFS, {}, UTF8ToString($0));
        FS.syncfs(true, function(err) {
            if (err) console.warn("Spore Lantern: could not read saved data", err);
            Module._spore_storage_ready();
        });
    }, WEB_SAVE_DIR);
    emscripten_set_main_loop(webFrame, 0, true);
    return 0;
#else
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
    Options opt;
    opt.mute = std::getenv("SPORE_MUTE") != nullptr;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--fullscreen")
            opt.fullscreen = true;
        else if (arg == "--mute")
            opt.mute = true;
        else if (arg == "--size" && i + 1 < argc)
        {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240)
            {
                opt.width = w;
                opt.height = h;
            }
        }
        else
        {
            std::printf("usage: %s [--fullscreen] [--size WIDTHxHEIGHT] [--mute]\n", argv[0]);
            return arg == "--help" ? 0 : 1;
        }
    }

    App app{ opt, "save.txt" };
    while (app.frame()) {}
    return 0;
#endif
}
