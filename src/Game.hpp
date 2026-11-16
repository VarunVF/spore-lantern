#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glgame2d/glgame2d.hpp>
#include <glm/glm.hpp>

#include "Audio.hpp"
#include "Gfx.hpp"
#include "Level.hpp"
#include "Save.hpp"

// One frame of player intent. "Held" values are levels, the rest are edges
// that stay set until a simulation step consumes them.
struct Controls
{
    bool left = false, right = false, jumpHeld = false;
    bool jump = false, brew = false, toss = false;
    bool restart = false, confirm = false, newGame = false, back = false;

    void clearEdges()
    {
        jump = brew = toss = restart = confirm = newGame = back = false;
    }
};

// An engine Entity with the bits the game needs on top: the engine's update()
// does the tile collision, this wrapper works out what it collided with and
// allows teleporting and jumping (velocity is protected in the engine).
class Body : public glgame2d::Entity
{
public:
    struct Contact
    {
        bool wall = false;      // stopped horizontally
        bool floor = false;     // stopped while moving down
        bool ceiling = false;   // stopped while moving up
    };

    Body(glm::vec2 topLeft, glm::vec2 size, float gravity, float terminalVelocity);

    Contact step(float deltaTime, const glgame2d::Tilemap& tilemap, float velocityX);
    void teleport(glm::vec2 topLeft);

    float velocityY() const { return m_Velocity.y; }
    void setVelocityY(float vy) { m_Velocity.y = vy; }

    glm::vec2 topLeft() const { return m_Sprite.position; }
    glm::vec2 size() const { return m_Sprite.size; }
    glm::vec2 center() const { return { m_Sprite.position.x + m_Sprite.size.x / 2.0f, m_Sprite.position.y - m_Sprite.size.y / 2.0f }; }
    float bottom() const { return m_Sprite.position.y - m_Sprite.size.y; }
    glgame2d::Rect rect() const { return m_Sprite.toRect(); }
};

class Game
{
public:
    static constexpr float STEP = 1.0f / 120.0f;    // fixed simulation step

    Game(Gfx& gfx, Audio& audio, std::string savePath);

    void step(Controls& controls);
    void render();

    bool wantsQuit() const { return m_Quit; }
    void toggleDebug() { m_Debug = !m_Debug; }
    void setFrameTime(float seconds) { m_FrameTime = seconds; }
    void setTrace(bool on) { m_Trace = on; }

private:
    enum class State { Title, Playing, Paused, Dying, Lost, Won };

    struct Particle
    {
        glm::vec2 pos, vel;
        float life, maxLife;
        float size;
        float gravity;
        const glgame2d::Texture* texture;
        bool glow;      // drawn on top of the darkness
    };

    struct Light { glm::vec2 pos; float radius; float strength; };

    struct Slime
    {
        float x = 0.0f;             // centre
        float y = 0.0f;             // bottom
        float floorY = 0.0f;
        float minX = 0.0f, maxX = 0.0f;
        float vx = 0.0f, vy = 0.0f;
        float hopTimer = 1.0f;
        float squash = 0.0f;        // >0 flat and wide, <0 tall and thin
        float blink = 0.0f;
        float dying = -1.0f;        // >= 0 while the death animation plays
        int dir = -1;
        bool airborne = false;
        bool dead = false;
    };

    struct Potion
    {
        Body body;
        float vx;
        float age = 0.0f;
    };

private:
    // setup
    void resetWorld(const SaveData* data);
    void restartFromSave();
    void setupSlime();

    // simulation
    void stepPlaying(Controls& controls);
    void stepPlayer(const Controls& controls, bool alive);
    void stepSlime();
    void stepPotion();
    void stepPickups(const Controls& controls);
    void stepParticles();
    void stepCamera(bool snap);

    void hurtPlayer(float fromX);
    void burstPotion(glm::vec2 at);
    void killSlime();
    void saveAtShrine();
    void say(const std::string& message, float seconds = 3.0f);

    void emit(glm::vec2 at, int count, const char* texture, float speed, float life,
        float size, float gravity, bool glow, float upBias = 0.0f);

    int sporeCount() const;
    glgame2d::Rect slimeRect() const;
    glgame2d::Rect doorRect() const;
    glm::vec2 sporePos(size_t index) const;
    float rnd();                    // [0, 1)
    float rnd(float lo, float hi);

    // drawing
    void renderWorld();
    void renderDarkness(const std::vector<Light>& lights);
    void renderHud();
    void renderOverlay();
    void dim(int level);
    std::vector<Light> collectLights() const;

private:
    Gfx& m_Gfx;
    Audio& m_Audio;
    std::string m_SavePath;

    Level m_Level;
    glgame2d::Tilemap m_Tilemap;

    State m_State = State::Title;
    bool m_Quit = false;
    bool m_Debug = false;
    bool m_Trace = false;
    float m_FrameTime = 0.0f;

    // player
    Body m_Player;
    float m_Vx = 0.0f;
    int m_Facing = 1;
    bool m_Grounded = false;
    bool m_Jumping = false;
    float m_Coyote = 0.0f;
    float m_JumpBuffer = 0.0f;
    float m_Invulnerable = 0.0f;
    float m_KnockTimer = 0.0f;
    float m_KnockVx = 0.0f;
    float m_FallSpeed = 0.0f;
    float m_RunTime = 0.0f;
    float m_StepSound = 0.0f;
    int m_Hearts = 3;
    float m_LanternRadius = 0.0f;

    // world
    std::array<bool, 3> m_Carried{};        // spores taken out of the world
    bool m_Spent = false;                   // ...and already brewed into a potion
    float m_RegrowTimer = -1.0f;
    bool m_HasPotion = false;
    std::optional<Potion> m_Potion;
    Slime m_Slime;
    bool m_DoorOpen = false;
    float m_DoorTimer = -1.0f;
    bool m_ShrineLit = false;
    bool m_InShrine = false;
    float m_Flash = 0.0f;                   // potion burst light
    glm::vec2 m_FlashPos{};

    std::vector<Particle> m_Particles;

    // bookkeeping
    float m_Time = 0.0f;                    // total simulated seconds (animation clock)
    float m_RunSeconds = 0.0f;              // play time of this run
    int m_Deaths = 0;
    float m_StateTime = 0.0f;
    std::string m_Message;
    float m_MessageTime = 0.0f;
    bool m_Moved = false, m_Jumped = false;
    bool m_HaveSave = false;

    glm::vec2 m_Camera{};                   // centre of the view, world space
    glm::vec2 m_ViewTopLeft{};              // what the current frame is drawn with
    float m_Shake = 0.0f;
    unsigned m_Rng = 0x2545F491u;
};
