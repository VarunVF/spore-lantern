#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <format>

using glgame2d::Rect;
namespace logging = glgame2d::logging;

namespace {

constexpr float DT = Game::STEP;
constexpr float TILE = Level::TILE;

// player
constexpr glm::vec2 PLAYER_SIZE{ 20.0f, 28.0f };
constexpr float RUN_SPEED = 175.0f;
constexpr float JUMP_SPEED = 470.0f;
constexpr float GRAVITY = -1200.0f;
constexpr float TERMINAL = -640.0f;
constexpr float COYOTE_TIME = 0.09f;
constexpr float JUMP_BUFFER = 0.12f;
constexpr float HURT_TIME = 1.3f;
constexpr int MAX_HEARTS = 3;

// potion
constexpr glm::vec2 POTION_SIZE{ 10.0f, 10.0f };
constexpr float POTION_GRAVITY = -700.0f;
constexpr float THROW_VX = 230.0f;      // standing lob; running adds to it
constexpr float THROW_VY = 250.0f;
constexpr float POTION_LIFETIME = 2.0f;
constexpr float BURST_RADIUS = 84.0f;

// light
constexpr int DARK_LEVELS = 8;         // must match tools/make_art.py
constexpr float AMBIENT = 0.09f;

float approach(float value, float target, float maxStep)
{
    if (value < target) return std::min(value + maxStep, target);
    return std::max(value - maxStep, target);
}

bool overlaps(const Rect& a, const Rect& b)
{
    return a.collides(b);
}

Rect rectAround(glm::vec2 center, float w, float h)
{
    return Rect{ center.x - w / 2.0f, center.y + h / 2.0f, w, h };
}

std::string clockText(float seconds)
{
    const int total = static_cast<int>(seconds);
    return std::format("{}:{:02}", total / 60, total % 60);
}

} // namespace


// ---------------------------------------------------------------------------
// Body
// ---------------------------------------------------------------------------

Body::Body(glm::vec2 topLeft, glm::vec2 size, float gravity, float terminalVelocity)
    : glgame2d::Entity{ glgame2d::Sprite{ topLeft, size }, gravity, terminalVelocity }
{
}

Body::Contact Body::step(float deltaTime, const glgame2d::Tilemap& tilemap, float velocityX)
{
    // Entity::update() resolves collisions but does not report them, so
    // predict where a free move would end up and compare afterwards.
    const float vy = std::fmax(m_TerminalVelocity, m_Velocity.y + m_Acceleration.y * deltaTime);
    const glm::vec2 expected = m_Displacement + glm::vec2{ velocityX, vy } * deltaTime;

    update(deltaTime, tilemap, { velocityX, 0.0f });

    constexpr float EPSILON = 2.0e-3f;
    const bool stoppedY = std::fabs(m_Displacement.y - expected.y) > EPSILON;

    Contact contact;
    contact.wall = std::fabs(m_Displacement.x - expected.x) > EPSILON;
    contact.floor = stoppedY && vy <= 0.0f;
    contact.ceiling = stoppedY && vy > 0.0f;
    return contact;
}

void Body::teleport(glm::vec2 topLeft)
{
    // Entity::move() only touches the sprite and is overwritten by the next
    // update(), so set the internal position as well.
    m_Displacement = topLeft;
    m_Sprite.position = topLeft;
    m_Velocity = { 0.0f, 0.0f };
}


// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

Game::Game(Gfx& gfx, Audio& audio, std::string savePath)
    : m_Gfx{ gfx }, m_Audio{ audio }, m_SavePath{ std::move(savePath) },
    m_Tilemap{ "assets/level/cave.tmj" },
    m_Player{ { 0.0f, 0.0f }, PLAYER_SIZE, GRAVITY, TERMINAL }
{
    if (!m_Level.load("assets/level/level.txt"))
        logging::error("[Game] Could not load assets/level/level.txt");

    SaveData data;
    if (save::read(m_SavePath, data))
    {
        logging::info("[Game] Loaded {}", m_SavePath);
        m_HaveSave = true;
        resetWorld(&data);
    }
    else
    {
        if (save::exists(m_SavePath))
            logging::warn("[Game] {} is not a valid save, starting fresh", m_SavePath);
        resetWorld(nullptr);
    }
}

void Game::resetWorld(const SaveData* data)
{
    m_Particles.clear();
    m_Potion.reset();
    m_Flash = 0.0f;
    m_Message.clear();
    m_MessageTime = 0.0f;
    m_RegrowTimer = -1.0f;
    m_DoorTimer = -1.0f;
    m_Shake = 0.0f;

    m_Vx = 0.0f;
    m_Facing = 1;
    m_Grounded = false;
    m_Jumping = false;
    m_Coyote = m_JumpBuffer = 0.0f;
    m_Invulnerable = 0.0f;
    m_KnockTimer = 0.0f;
    m_FallSpeed = 0.0f;

    const glm::vec2 startFloor = Level::cellFloorCenter(m_Level.playerStart);
    glm::vec2 start{ startFloor.x - PLAYER_SIZE.x / 2.0f, startFloor.y + PLAYER_SIZE.y };

    m_Carried = {};
    m_Spent = false;
    m_HasPotion = false;
    m_Hearts = MAX_HEARTS;
    m_RunSeconds = 0.0f;
    m_Deaths = 0;
    m_ShrineLit = false;
    bool slimeDead = false;

    if (data)
    {
        // Only trust a saved position that is inside the cave and not in rock.
        const int c0 = static_cast<int>(std::floor(data->x / TILE));
        const int c1 = static_cast<int>(std::floor((data->x + PLAYER_SIZE.x - 0.01f) / TILE));
        const int r0 = static_cast<int>(std::floor(-data->y / TILE));
        const int r1 = static_cast<int>(std::floor((-data->y + PLAYER_SIZE.y - 0.01f) / TILE));
        bool free = true;
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++)
                free = free && !m_Level.solid(c, r);
        if (free)
            start = { data->x, data->y };
        else
            logging::warn("[Game] Saved position is inside rock, using the level start");

        m_Carried = data->spores;
        m_HasPotion = data->potion;
        slimeDead = data->slimeDead;
        m_Spent = m_HasPotion || slimeDead;
        if (m_Spent)
            m_Carried = { true, true, true };
        m_Hearts = data->hearts;
        m_RunSeconds = data->seconds;
        m_Deaths = data->deaths;
        m_ShrineLit = true;
    }

    m_Player.teleport(start);
    setupSlime();
    m_Slime.dead = slimeDead;
    m_DoorOpen = slimeDead;

    const glm::vec2 shrineFloor = Level::cellFloorCenter(m_Level.shrine);
    m_InShrine = overlaps(m_Player.rect(), Rect{ shrineFloor.x - 16.0f, shrineFloor.y + 48.0f, 32.0f, 48.0f });

    m_LanternRadius = 0.0f;
    stepCamera(true);
}

void Game::restartFromSave()
{
    const int deaths = m_Deaths;
    const float seconds = m_RunSeconds;

    SaveData data;
    if (save::read(m_SavePath, data))
        resetWorld(&data);
    else
        resetWorld(nullptr);

    m_Deaths = deaths;
    m_RunSeconds = seconds;
    m_State = State::Playing;
    m_StateTime = 0.0f;
}

void Game::setupSlime()
{
    m_Slime = Slime{};
    const Level::Cell cell = m_Level.slime;
    const glm::vec2 floor = Level::cellFloorCenter(cell);

    // The slime patrols the flat floor around its marker.
    auto walkable = [&](int c) {
        return !m_Level.solid(c, cell.r) && m_Level.solid(c, cell.r + 1) && m_Level.rows[cell.r][c] != 'D';
    };
    int cmin = cell.c, cmax = cell.c;
    while (walkable(cmin - 1)) cmin--;
    while (walkable(cmax + 1)) cmax++;

    m_Slime.x = floor.x;
    m_Slime.y = m_Slime.floorY = floor.y;
    m_Slime.minX = cmin * TILE + 26.0f;
    m_Slime.maxX = (cmax + 1) * TILE - 26.0f;
}


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

float Game::rnd()
{
    // xorshift32: deterministic, so scripted runs replay exactly
    m_Rng ^= m_Rng << 13;
    m_Rng ^= m_Rng >> 17;
    m_Rng ^= m_Rng << 5;
    return (m_Rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float Game::rnd(float lo, float hi)
{
    return lo + (hi - lo) * rnd();
}

int Game::sporeCount() const
{
    if (m_Spent)
        return 0;
    return static_cast<int>(std::count(m_Carried.begin(), m_Carried.end(), true));
}

Rect Game::slimeRect() const
{
    const float h = 30.0f * (1.0f - m_Slime.squash * 0.5f);
    return Rect{ m_Slime.x - 22.0f, m_Slime.y + h, 44.0f, h };
}

Rect Game::doorRect() const
{
    const glm::vec2 floor = Level::cellFloorCenter(m_Level.door);
    return Rect{ floor.x - 10.0f, floor.y + 44.0f, 20.0f, 44.0f };
}

glm::vec2 Game::sporePos(size_t index) const
{
    const glm::vec2 floor = Level::cellFloorCenter(m_Level.spores[index]);
    const float bob = std::sin(m_Time * 2.2f + index * 1.7f) * 3.0f;
    return { floor.x, floor.y + 16.0f + bob };
}

void Game::say(const std::string& message, float seconds)
{
    m_Message = message;
    m_MessageTime = seconds;
}

void Game::emit(glm::vec2 at, int count, const char* texture, float speed, float life,
    float size, float gravity, bool glow, float upBias)
{
    const glgame2d::Texture* tex = m_Gfx.tex(texture);
    for (int i = 0; i < count; i++)
    {
        const float angle = rnd(0.0f, 6.2832f);
        const float v = speed * rnd(0.3f, 1.0f);
        const float l = life * rnd(0.6f, 1.0f);
        m_Particles.push_back(Particle{
            at, { std::cos(angle) * v, std::sin(angle) * v + upBias },
            l, l, size * rnd(0.6f, 1.0f), gravity, tex, glow
        });
    }
}


// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void Game::step(Controls& controls)
{
    m_Time += DT;
    m_StateTime += DT;

    switch (m_State)
    {
        case State::Title:
            if (controls.back)
                m_Quit = true;
            else if (controls.newGame)
            {
                save::erase(m_SavePath);
                m_HaveSave = false;
                resetWorld(nullptr);
                m_State = State::Playing;
                m_StateTime = 0.0f;
                logging::info("[Game] New game");
            }
            else if (controls.confirm)
            {
                m_State = State::Playing;
                m_StateTime = 0.0f;
                logging::info("[Game] Start");
            }
            stepParticles();
            break;

        case State::Playing:
            if (controls.back)
            {
                m_State = State::Paused;
                m_StateTime = 0.0f;
                break;
            }
            if (controls.restart)
            {
                restartFromSave();
                say(m_HaveSave ? "Back at the shrine." : "Back at the start.", 2.0f);
                break;
            }
            m_RunSeconds += DT;
            stepPlaying(controls);
            break;

        case State::Paused:
            if (controls.back || controls.confirm)
                m_State = State::Playing;
            else if (controls.restart)
                restartFromSave();
            else if (controls.newGame)
            {
                restartFromSave();
                m_State = State::Title;
            }
            break;

        case State::Dying:
        {
            Controls none;
            stepPlaying(none);
            if (m_StateTime > 1.8f)
            {
                m_State = State::Lost;
                m_StateTime = 0.0f;
                logging::info("[Game] LOST");
            }
            break;
        }

        case State::Lost:
            stepSlime();
            stepParticles();
            if (m_StateTime > 0.5f && (controls.confirm || controls.restart))
            {
                m_Deaths++;
                restartFromSave();
                logging::info("[Game] Restart after loss");
            }
            break;

        case State::Won:
            stepParticles();
            if (rnd() < 0.1f)
            {
                const glm::vec2 door = Level::cellFloorCenter(m_Level.door);
                emit({ door.x + rnd(-10.0f, 10.0f), door.y + rnd(8.0f, 44.0f) }, 1, "pix_amber", 40.0f, 1.4f, 4.0f, 30.0f, true, 20.0f);
            }
            if (m_StateTime > 0.8f && controls.confirm)
            {
                save::erase(m_SavePath);
                m_HaveSave = false;
                resetWorld(nullptr);
                m_State = State::Playing;
                m_StateTime = 0.0f;
                say("The cave resets. Good luck!", 2.5f);
                logging::info("[Game] Level reset after win");
            }
            break;
    }

    if (m_MessageTime > 0.0f)
    {
        m_MessageTime -= DT;
        if (m_MessageTime <= 0.0f)
            m_Message.clear();
    }
    m_Shake = std::max(0.0f, m_Shake - 22.0f * DT);

    // Darkness is the most sprite-hungry thing on screen. If frames have been
    // slow for a while, fall back to cells of twice the size (a quarter of
    // the sprites) and stay there. F4 switches by hand.
    if (!m_CoarseLight && !m_LightManual && m_FrameTime > 1.0f / 50.0f)
    {
        m_SlowTime += DT;
        if (m_SlowTime > 2.0f)
        {
            m_CoarseLight = true;
            logging::info("[Game] Slow frames: switching to coarse lighting");
        }
    }
    else
    {
        m_SlowTime = std::max(0.0f, m_SlowTime - DT);
    }

    if (m_Trace && static_cast<int>(m_Time / DT) % 30 == 0)
        logging::info("[Trace] t={:.2f} x={:.1f} y={:.1f} ground={} hearts={} spores={} slime={:.0f} ms={:.1f} draws={}",
            m_Time, m_Player.topLeft().x, m_Player.topLeft().y, m_Grounded, m_Hearts, sporeCount(), m_Slime.x, m_FrameTime * 1000.0f, m_Gfx.lastDrawCount());
}

void Game::stepPlaying(Controls& controls)
{
    const bool alive = (m_State == State::Playing);

    stepPlayer(controls, alive);
    stepSlime();
    stepPotion();
    if (alive)
        stepPickups(controls);
    stepParticles();
    stepCamera(false);

    // Lantern: brighter with more hearts, out when dead.
    static constexpr float RADIUS[MAX_HEARTS + 1] = { 0.0f, 150.0f, 195.0f, 240.0f };
    const float target = RADIUS[std::clamp(m_Hearts, 0, MAX_HEARTS)];
    m_LanternRadius = approach(m_LanternRadius, target, (alive ? 260.0f : 130.0f) * DT);

    if (m_Flash > 0.0f)
        m_Flash = std::max(0.0f, m_Flash - DT / 0.6f);

    if (m_DoorTimer > 0.0f)
    {
        m_DoorTimer -= DT;
        if (m_DoorTimer <= 0.0f)
        {
            m_DoorOpen = true;
            m_Audio.play("door");
            say("The seal is gone. The exit is open!");
            const glm::vec2 door = Level::cellFloorCenter(m_Level.door);
            emit({ door.x, door.y + 30.0f }, 26, "pix_purple", 120.0f, 0.8f, 5.0f, -300.0f, false, 60.0f);
            emit({ door.x, door.y + 30.0f }, 16, "pix_amber", 80.0f, 1.0f, 4.0f, 0.0f, true);
            logging::info("[Game] Door open");
        }
    }

    // ambient dust drifting through the lantern light
    if (alive && rnd() < 0.06f)
    {
        const glm::vec2 c = m_Player.center();
        m_Particles.push_back(Particle{
            { c.x + rnd(-170.0f, 170.0f), c.y + rnd(-110.0f, 130.0f) },
            { rnd(-8.0f, 8.0f), rnd(4.0f, 14.0f) },
            3.0f, 3.0f, 2.0f, 0.0f, m_Gfx.tex("pix_amber"), false
        });
    }
}

void Game::stepPlayer(const Controls& controls, bool alive)
{
    float move = 0.0f;
    if (alive)
    {
        if (controls.right) move += 1.0f;
        if (controls.left)  move -= 1.0f;
    }
    if (move != 0.0f)
    {
        m_Facing = move > 0.0f ? 1 : -1;
        m_Moved = true;
    }

    m_Vx = approach(m_Vx, move * RUN_SPEED, (m_Grounded ? 2400.0f : 1500.0f) * DT);
    float vx = m_Vx;
    if (m_KnockTimer > 0.0f)
    {
        m_KnockTimer -= DT;
        vx = m_KnockVx;
        m_Vx = m_KnockVx * 0.4f;
    }

    // jump with a little forgiveness on both sides of the edge
    m_JumpBuffer -= DT;
    m_Coyote -= DT;
    if (m_Grounded)
        m_Coyote = COYOTE_TIME;
    if (alive && controls.jump)
        m_JumpBuffer = JUMP_BUFFER;
    if (m_JumpBuffer > 0.0f && m_Coyote > 0.0f)
    {
        m_Player.setVelocityY(JUMP_SPEED);
        m_JumpBuffer = m_Coyote = 0.0f;
        m_Jumping = true;
        m_Jumped = true;
        m_Audio.play("jump");
        emit({ m_Player.center().x, m_Player.bottom() + 2.0f }, 5, "pix_rock", 50.0f, 0.3f, 4.0f, -200.0f, false, 20.0f);
    }
    if (m_Jumping && !controls.jumpHeld && m_Player.velocityY() > 0.0f)
    {
        // released early: cut the jump short
        m_Player.setVelocityY(m_Player.velocityY() * 0.45f);
        m_Jumping = false;
    }

    const Body::Contact contact = m_Player.step(DT, m_Tilemap, vx);

    if (contact.wall)
        m_Vx = 0.0f;
    if (contact.floor)
    {
        if (!m_Grounded && m_FallSpeed < -300.0f)
        {
            m_Audio.play("land");
            emit({ m_Player.center().x, m_Player.bottom() + 2.0f }, 6, "pix_rock", 60.0f, 0.35f, 4.0f, -200.0f, false, 30.0f);
        }
        m_Grounded = true;
        m_Jumping = false;
        m_FallSpeed = 0.0f;
    }
    else
    {
        m_Grounded = false;
        m_FallSpeed = m_Player.velocityY();
    }
    if (m_Player.velocityY() <= 0.0f)
        m_Jumping = false;

    if (m_Grounded && std::fabs(vx) > 30.0f)
    {
        m_RunTime += DT;
        m_StepSound -= DT;
        if (m_StepSound <= 0.0f)
        {
            m_Audio.play("step");
            m_StepSound = 0.3f;
        }
    }
    else
    {
        m_StepSound = 0.08f;
    }

    m_Invulnerable = std::max(0.0f, m_Invulnerable - DT);

    // Safety net: the cave is closed, but never leave the player in the void.
    if (m_Player.topLeft().y < -m_Level.pixelHeight() - 200.0f)
        restartFromSave();
}

void Game::hurtPlayer(float fromX)
{
    if (m_Invulnerable > 0.0f || m_State != State::Playing)
        return;

    m_Hearts--;
    m_Invulnerable = HURT_TIME;
    m_KnockVx = (m_Player.center().x < fromX ? -1.0f : 1.0f) * 230.0f;
    m_KnockTimer = 0.22f;
    m_Player.setVelocityY(340.0f);
    m_Jumping = false;
    m_Shake = 7.0f;
    emit(m_Player.center(), 12, "pix_red", 130.0f, 0.5f, 5.0f, -300.0f, false, 40.0f);
    logging::info("[Game] Hurt, hearts={}", m_Hearts);

    if (m_Hearts <= 0)
    {
        m_State = State::Dying;
        m_StateTime = 0.0f;
        m_Audio.play("lose");
        emit(m_Player.center(), 24, "pix_amber", 150.0f, 1.0f, 5.0f, -150.0f, true, 60.0f);
    }
    else
    {
        m_Audio.play("hurt");
    }
}

void Game::stepSlime()
{
    Slime& s = m_Slime;
    if (s.dead)
        return;

    if (s.dying >= 0.0f)
    {
        s.dying += DT;
        if (rnd() < 0.5f)
            emit({ s.x + rnd(-20.0f, 20.0f), s.y + rnd(4.0f, 30.0f) }, 1, "pix_purple", 90.0f, 0.6f, 6.0f, -200.0f, false, 50.0f);
        if (s.dying > 0.5f)
        {
            s.dead = true;
            s.dying = -1.0f;
            m_DoorTimer = 0.7f;
            emit({ s.x, s.y + 18.0f }, 40, "pix_purple", 190.0f, 0.9f, 6.0f, -400.0f, false, 90.0f);
            emit({ s.x, s.y + 18.0f }, 20, "pix_cyan", 120.0f, 1.0f, 4.0f, 0.0f, true);
            logging::info("[Game] Slime defeated");
        }
        return;
    }

    s.blink -= DT;
    if (s.blink < -0.15f)
        s.blink = rnd(1.5f, 4.0f);

    const glm::vec2 player = m_Player.center();
    const bool alert = m_State == State::Playing
        && std::fabs(player.x - s.x) < 330.0f
        && player.y < s.floorY + 56.0f && player.y > s.floorY - 20.0f;   // only once you are down on its floor

    if (!s.airborne)
    {
        s.hopTimer -= DT;
        // settle, then crouch just before the next hop
        const float target = s.hopTimer < 0.2f ? 0.3f : 0.05f * std::sin(m_Time * 3.0f);
        s.squash += (target - s.squash) * std::min(1.0f, 12.0f * DT);

        if (s.hopTimer <= 0.0f)
        {
            if (alert)
                s.dir = player.x > s.x ? 1 : -1;
            else if (s.x <= s.minX + 6.0f)
                s.dir = 1;
            else if (s.x >= s.maxX - 6.0f)
                s.dir = -1;

            s.vx = s.dir * (alert ? 135.0f : 105.0f);
            s.vy = alert ? 300.0f : 240.0f;
            s.airborne = true;
            s.squash = -0.25f;
            if (alert)
                m_Audio.play("hop");
        }
    }
    else
    {
        s.vy -= 900.0f * DT;
        s.x = std::clamp(s.x + s.vx * DT, s.minX, s.maxX);
        s.y += s.vy * DT;
        s.squash += (-0.15f - s.squash) * std::min(1.0f, 8.0f * DT);
        if (s.y <= s.floorY)
        {
            s.y = s.floorY;
            s.airborne = false;
            s.squash = 0.35f;
            s.hopTimer = alert ? 0.5f : rnd(0.55f, 0.95f);
            emit({ s.x, s.y + 2.0f }, 4, "pix_purple", 60.0f, 0.3f, 4.0f, -200.0f, false, 20.0f);
        }
    }

    if (overlaps(m_Player.rect(), slimeRect()))
        hurtPlayer(s.x);
}

void Game::stepPotion()
{
    if (!m_Potion)
        return;

    Potion& p = *m_Potion;
    p.age += DT;
    const Body::Contact contact = p.body.step(DT, m_Tilemap, p.vx);
    const glm::vec2 at = p.body.center();

    if (rnd() < 0.5f)
        emit(at, 1, "pix_cyan", 20.0f, 0.35f, 4.0f, 0.0f, true);

    const bool hitSlime = !m_Slime.dead && m_Slime.dying < 0.0f && overlaps(p.body.rect(), slimeRect());
    if (contact.wall || contact.floor || contact.ceiling || hitSlime || p.age >= POTION_LIFETIME)
        burstPotion(at);
}

void Game::burstPotion(glm::vec2 at)
{
    m_Potion.reset();
    m_Flash = 1.0f;
    m_FlashPos = at;
    m_Shake = 6.0f;
    m_Audio.play("shatter");
    emit(at, 30, "pix_cyan", 220.0f, 0.7f, 5.0f, -250.0f, true, 40.0f);
    emit(at, 14, "pix_white", 140.0f, 0.5f, 4.0f, 0.0f, true);
    logging::info("[Game] Potion burst at {:.0f},{:.0f}", at.x, at.y);

    if (m_Slime.dead || m_Slime.dying >= 0.0f)
        return;

    const glm::vec2 slimeCenter{ m_Slime.x, m_Slime.y + 16.0f };
    if (glm::distance(at, slimeCenter) <= BURST_RADIUS)
    {
        killSlime();
    }
    else
    {
        say("The light fades before it reaches the slime.\nThe spores are regrowing...", 4.0f);
        m_RegrowTimer = 1.6f;
        logging::info("[Game] Potion missed");
    }
}

void Game::killSlime()
{
    m_Slime.dying = 0.0f;
    m_Shake = 9.0f;
    m_Audio.play("slime_die");
    say("The slime dissolves in the light!");
}

void Game::saveAtShrine()
{
    m_Hearts = MAX_HEARTS;
    m_ShrineLit = true;

    SaveData data;
    data.x = m_Player.topLeft().x;
    data.y = m_Player.topLeft().y;
    // A potion in the air (or a pending regrowth) is saved as "spores are back".
    const bool regrowing = m_Potion.has_value() || m_RegrowTimer >= 0.0f;
    data.spores = regrowing ? std::array<bool, 3>{} : m_Carried;
    data.potion = m_HasPotion;
    data.slimeDead = m_Slime.dead || m_Slime.dying >= 0.0f;
    data.hearts = m_Hearts;
    data.seconds = m_RunSeconds;
    data.deaths = m_Deaths;

    const glm::vec2 shrine = Level::cellFloorCenter(m_Level.shrine);
    if (save::write(m_SavePath, data))
    {
        m_HaveSave = true;
        say("The shrine keeps your light. Game saved.");
        logging::info("[Game] Saved to {}", m_SavePath);
    }
    else
    {
        say("The shrine flickers: could not write save.txt");
        logging::error("[Game] Could not write {}", m_SavePath);
    }
    m_Audio.play("save");
    emit({ shrine.x, shrine.y + 40.0f }, 24, "pix_blue", 110.0f, 1.0f, 4.0f, 60.0f, true, 60.0f);
}

void Game::stepPickups(const Controls& controls)
{
    const Rect player = m_Player.rect();

    // spores
    for (size_t i = 0; i < m_Level.spores.size() && i < m_Carried.size(); i++)
    {
        if (m_Carried[i])
            continue;
        const glm::vec2 pos = sporePos(i);
        if (overlaps(player, rectAround(pos, 22.0f, 22.0f)))
        {
            m_Carried[i] = true;
            const int count = sporeCount();
            m_Audio.play(std::format("spore{}", std::clamp(count, 1, 3)));
            emit(pos, 18, "pix_teal", 120.0f, 0.7f, 4.0f, 0.0f, true);
            if (count == 1)      say("A glowing spore! Find two more.");
            else if (count == 2) say("Two spores. One more to find.");
            else                 say("Three spores! Press E to brew a light potion.", 5.0f);
            logging::info("[Game] Spore collected ({}/3)", count);
        }
    }

    if (m_RegrowTimer >= 0.0f)
    {
        m_RegrowTimer -= DT;
        if (m_RegrowTimer < 0.0f)
        {
            m_Carried = {};
            m_Spent = false;
            m_Audio.play("regrow");
            for (size_t i = 0; i < m_Level.spores.size(); i++)
                emit(sporePos(i), 12, "pix_teal", 70.0f, 0.8f, 4.0f, 0.0f, true);
            logging::info("[Game] Spores regrown");
        }
    }

    // brew
    if (controls.brew)
    {
        if (m_HasPotion)
        {
            say("You already carry a potion. Press F to throw it.");
        }
        else if (sporeCount() == 3)
        {
            m_Spent = true;
            m_HasPotion = true;
            m_Audio.play("brew");
            emit(m_Player.center(), 30, "pix_teal", 150.0f, 0.8f, 4.0f, 0.0f, true);
            emit(m_Player.center(), 14, "pix_cyan", 90.0f, 1.0f, 5.0f, 80.0f, true, 40.0f);
            say("Brewed a light potion! Press F to throw it.", 4.0f);
            logging::info("[Game] Potion brewed");
        }
        else
        {
            m_Audio.play("deny");
            say(std::format("You need 3 spores to brew. You have {}.", sporeCount()), 2.0f);
        }
    }

    // throw
    if (controls.toss)
    {
        if (m_HasPotion && !m_Potion)
        {
            m_HasPotion = false;
            // Spawn inside the player's own box so it can never start in a wall.
            const glm::vec2 c = m_Player.center();
            const glm::vec2 topLeft{ c.x - POTION_SIZE.x / 2.0f + m_Facing * 4.0f, c.y + POTION_SIZE.y / 2.0f + 6.0f };
            m_Potion.emplace(Potion{ Body{ topLeft, POTION_SIZE, POTION_GRAVITY, -900.0f }, m_Facing * THROW_VX + m_Vx * 0.6f });
            m_Potion->body.setVelocityY(THROW_VY);
            m_Audio.play("throw");
            logging::info("[Game] Potion thrown");
        }
        else if (!m_HasPotion && !m_Potion)
        {
            m_Audio.play("deny");
            say("No potion. Brew one from 3 spores with E.", 2.0f);
        }
    }

    // shrine: saves once each time you step into it
    const glm::vec2 shrine = Level::cellFloorCenter(m_Level.shrine);
    const bool inShrine = overlaps(player, Rect{ shrine.x - 16.0f, shrine.y + 48.0f, 32.0f, 48.0f });
    if (inShrine && !m_InShrine)
        saveAtShrine();
    m_InShrine = inShrine;

    // spikes
    for (const Level::Cell& cell : m_Level.spikes)
    {
        const glm::vec2 floor = Level::cellFloorCenter(cell);
        if (overlaps(player, Rect{ floor.x - 11.0f, floor.y + 16.0f, 22.0f, 16.0f }))
        {
            hurtPlayer(floor.x);
            break;
        }
    }

    // exit door
    if (m_State == State::Playing && overlaps(player, doorRect()))
    {
        if (m_DoorOpen)
        {
            m_State = State::Won;
            m_StateTime = 0.0f;
            m_Audio.play("win");
            logging::info("[Game] WON in {:.1f}s", m_RunSeconds);
        }
        else if (m_Message.empty())
        {
            say("The door is sealed with slime.", 2.0f);
        }
    }
}

void Game::stepParticles()
{
    for (Particle& p : m_Particles)
    {
        p.life -= DT;
        p.vel.y += p.gravity * DT;
        p.vel *= (1.0f - 1.5f * DT);
        p.pos += p.vel * DT;
    }
    std::erase_if(m_Particles, [](const Particle& p) { return p.life <= 0.0f; });
}

void Game::stepCamera(bool snap)
{
    const glm::vec2 half{ m_Gfx.viewW() / 2.0f, m_Gfx.viewH() / 2.0f };
    glm::vec2 target = m_Player.center() + glm::vec2{ m_Facing * 40.0f, 24.0f };

    const float w = m_Level.pixelWidth(), h = m_Level.pixelHeight();
    target.x = (w <= half.x * 2.0f) ? w / 2.0f : std::clamp(target.x, half.x, w - half.x);
    target.y = (h <= half.y * 2.0f) ? -h / 2.0f : std::clamp(target.y, -h + half.y, -half.y);

    if (snap)
        m_Camera = target;
    else
        m_Camera += (target - m_Camera) * (1.0f - std::exp(-5.0f * DT));
}


// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

std::vector<Game::Light> Game::collectLights() const
{
    std::vector<Light> lights;

    if (m_LanternRadius > 1.0f)
    {
        const float flicker = 1.0f + 0.025f * std::sin(m_Time * 13.0f) + 0.02f * std::sin(m_Time * 7.3f);
        lights.push_back({ m_Player.center(), m_LanternRadius * flicker, 1.0f });
    }
    for (size_t i = 0; i < m_Level.spores.size() && i < m_Carried.size(); i++)
        if (!m_Carried[i])
            lights.push_back({ sporePos(i), 70.0f + 6.0f * std::sin(m_Time * 3.0f + i), 0.85f });

    // Crystals and the slime only get a glow sprite (see render()): every real
    // light adds dozens of darkness sprites, and sprites are not cheap here.

    const glm::vec2 shrine = Level::cellFloorCenter(m_Level.shrine);
    lights.push_back({ { shrine.x, shrine.y + 40.0f }, m_ShrineLit ? 120.0f : 50.0f, m_ShrineLit ? 0.9f : 0.4f });

    if (m_DoorOpen)
        lights.push_back({ Level::cellFloorCenter(m_Level.door) + glm::vec2{ 0.0f, 28.0f }, 150.0f, 1.0f });

    if (m_Potion)
        lights.push_back({ m_Potion->body.center(), 100.0f, 0.95f });
    if (m_Flash > 0.0f)
        lights.push_back({ m_FlashPos, 120.0f + 260.0f * m_Flash, std::min(1.0f, m_Flash * 1.6f) });

    return lights;
}

void Game::renderDarkness(const std::vector<Light>& lights)
{
    // The renderer has no blend modes, tint or render targets, so darkness is
    // a world-aligned grid of translucent squares. Each cell picks one of 8
    // alpha levels from the brightest light reaching it, and runs of equal
    // cells in a row are merged into one stretched sprite.
    static const glgame2d::Texture* dark[DARK_LEVELS] = {};
    if (!dark[0])
        for (int i = 0; i < DARK_LEVELS; i++)
            dark[i] = m_Gfx.tex(std::format("dark_{:02}", i));

    const float CELL = m_CoarseLight ? 32.0f : 16.0f;
    const glm::vec2 topLeft = m_ViewTopLeft;
    const int c0 = static_cast<int>(std::floor(topLeft.x / CELL));
    const int c1 = static_cast<int>(std::floor((topLeft.x + m_Gfx.viewW()) / CELL));
    const int r0 = static_cast<int>(std::floor(-topLeft.y / CELL));
    const int r1 = static_cast<int>(std::floor((-topLeft.y + m_Gfx.viewH()) / CELL));

    for (int r = r0; r <= r1; r++)
    {
        const float cy = -(r + 0.5f) * CELL;
        int runStart = c0;
        int runLevel = -1;
        for (int c = c0; c <= c1 + 1; c++)
        {
            int level = -1;
            if (c <= c1)
            {
                const float cx = (c + 0.5f) * CELL;
                float light = AMBIENT;
                for (const Light& l : lights)
                {
                    const float dx = cx - l.pos.x, dy = cy - l.pos.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= l.radius * l.radius)
                        continue;
                    // fully lit core, soft edge
                    float t = std::min(1.0f, (1.0f - std::sqrt(d2) / l.radius) * 1.7f);
                    t = t * t * (3.0f - 2.0f * t);
                    light = std::max(light, AMBIENT + (1.0f - AMBIENT) * t * l.strength);
                }
                level = static_cast<int>(std::lround((1.0f - light) * (DARK_LEVELS - 1)));
            }
            if (level != runLevel)
            {
                if (runLevel > 0)
                    m_Gfx.draw(dark[runLevel], { runStart * CELL, -r * CELL }, { (c - runStart) * CELL, CELL });
                runStart = c;
                runLevel = level;
            }
        }
    }
}

void Game::renderWorld()
{
    Gfx& g = m_Gfx;
    const glm::vec2 topLeft = m_ViewTopLeft;

    // background wall, scrolling at half speed (texture wraps, so one sprite)
    {
        constexpr float SIZE = 128.0f, PARALLAX = 0.5f;
        const float u0 = topLeft.x * PARALLAX / SIZE;
        const float v0 = topLeft.y * PARALLAX / SIZE;
        g.drawUV(g.tex("background"), topLeft, { g.viewW(), g.viewH() },
            { u0, v0, u0 + g.viewW() / SIZE, v0 + g.viewH() / SIZE });
    }

    // The cave: solid tiles plus the decoration object layer of the Tiled map.
    // The engine draws every tile and object of the map, visible or not
    // (about 890 draw calls); since glgame2d 2033fd7 that costs about 1 ms.
    m_Tilemap.render(g.renderer(), g.window());
    g.countExternalDraws(static_cast<int>(m_Tilemap.getRects().size() + m_Level.decor.size()));

    for (const Level::Cell& cell : m_Level.spikes)
        g.draw(g.tex("spikes"), Level::cellTopLeft(cell), { TILE, TILE });

    // shrine and its flame
    {
        const glm::vec2 floor = Level::cellFloorCenter(m_Level.shrine);
        g.draw(g.tex("shrine"), { floor.x - 16.0f, floor.y + 48.0f }, { 32.0f, 48.0f });
        const int frame = m_ShrineLit ? static_cast<int>(m_Time * 9.0f) % 3 : 3;
        g.drawFrame(g.tex("flame"), { floor.x - 10.0f, floor.y + 50.0f }, { 20.0f, 20.0f }, frame, 4);
    }

    // door
    {
        const glm::vec2 floor = Level::cellFloorCenter(m_Level.door);
        g.drawFrame(g.tex("door"), { floor.x - 24.0f, floor.y + 64.0f }, { 48.0f, 64.0f }, m_DoorOpen ? 1 : 0, 2);
    }

    // spores
    for (size_t i = 0; i < m_Level.spores.size() && i < m_Carried.size(); i++)
    {
        if (m_Carried[i])
            continue;
        const glm::vec2 pos = glm::round(sporePos(i));
        const int frame = static_cast<int>(m_Time * 3.0f + i) % 2;
        g.drawFrame(g.tex("spore"), { pos.x - 12.0f, pos.y + 12.0f }, { 24.0f, 24.0f }, frame, 2);
    }

    // slime
    if (!m_Slime.dead)
    {
        const Slime& s = m_Slime;
        float shrink = 1.0f;
        int frame = s.blink < 0.0f ? 1 : 0;
        if (s.dying >= 0.0f)
        {
            frame = (static_cast<int>(s.dying * 30.0f) % 2 == 0) ? 2 : 0;
            shrink = 1.0f - s.dying / 0.5f * 0.6f;
        }
        const float w = std::round(60.0f * (1.0f + s.squash * 0.6f) * shrink);
        const float h = std::round(44.0f * (1.0f - s.squash * 0.6f) * shrink);
        g.drawFrame(g.tex("slime"), { std::round(s.x - w / 2.0f), std::round(s.y + h) }, { w, h }, frame, 3, s.dir > 0);
    }

    // player and lantern
    const bool dyingGone = m_State == State::Lost || (m_State == State::Dying && m_StateTime > 1.0f);
    const bool blink = m_Invulnerable > 0.0f && static_cast<int>(m_Time * 20.0f) % 2 == 0 && m_State == State::Playing;
    if (!dyingGone && !blink)
    {
        int frame;
        if (!m_Grounded)
            frame = m_Player.velocityY() > 0.0f ? 6 : 7;
        else if (std::fabs(m_Vx) > 30.0f)
            frame = 2 + static_cast<int>(m_RunTime * 12.0f) % 4;
        else
            frame = static_cast<int>(m_Time * 2.0f) % 2;

        const bool flash = m_Invulnerable > HURT_TIME - 0.12f || m_State == State::Dying;
        const glm::vec2 p = glm::round(m_Player.topLeft());
        g.drawFrame(g.tex(flash ? "player_flash" : "player"), { p.x - 6.0f, p.y + 4.0f }, { 32.0f, 32.0f },
            frame, 8, m_Facing < 0);

        if (m_State != State::Dying)
        {
            const float sway = m_Grounded && std::fabs(m_Vx) > 30.0f ? std::sin(m_RunTime * 24.0f) * 1.5f : std::sin(m_Time * 2.0f);
            const glm::vec2 lantern{ p.x + 10.0f + m_Facing * 17.0f, p.y - 15.0f + std::round(sway) };
            g.drawCentered(g.tex("lantern"), lantern, { 20.0f, 22.0f });
        }
    }

    if (m_Potion)
        g.drawCentered(g.tex("potion"), glm::round(m_Potion->body.center()), { 20.0f, 22.0f });

    for (const Particle& p : m_Particles)
        if (!p.glow)
        {
            const float size = std::max(1.0f, std::round(p.size * (p.life / p.maxLife)));
            g.drawCentered(p.texture, glm::round(p.pos), { size, size });
        }
}

void Game::dim(int level)
{
    level = std::clamp(level, 0, DARK_LEVELS - 1);
    if (level > 0)
        m_Gfx.screen(m_Gfx.tex(std::format("dark_{:02}", level)), 0.0f, 0.0f, m_Gfx.viewW(), m_Gfx.viewH());
}

void Game::renderHud()
{
    Gfx& g = m_Gfx;
    const glm::vec4 white{ 1.0f, 1.0f, 1.0f, 1.0f };

    for (int i = 0; i < MAX_HEARTS; i++)
        g.screen(g.tex(i < m_Hearts ? "heart" : "heart_empty"), 14.0f + i * 28.0f, 12.0f, 22.0f, 22.0f);

    const int spores = sporeCount();
    for (int i = 0; i < 3; i++)
    {
        const float x = 13.0f + i * 28.0f, y = 42.0f;
        if (i < spores)
            g.screenFrame(g.tex("spore"), x, y, 24.0f, 24.0f, static_cast<int>(m_Time * 3.0f + i) % 2, 2);
        else
            g.screen(g.tex("spore_slot"), x, y, 24.0f, 24.0f);
    }
    g.screen(g.tex(m_HasPotion ? "potion" : "potion_slot"), 102.0f, 43.0f, 20.0f, 22.0f);

    g.text(clockText(m_RunSeconds), g.viewW() - 14.0f, 14.0f, 16, { 0.8f, 0.85f, 1.0f, 0.8f }, Gfx::Align::Right);

    if (m_Debug)
    {
        const float fps = m_FrameTime > 0.0f ? 1.0f / m_FrameTime : 0.0f;
        g.text(std::format("{:.0f} fps  {} sprites  {} particles", fps, g.lastDrawCount(), m_Particles.size()),
            g.viewW() - 14.0f, 40.0f, 8, { 1.0f, 1.0f, 0.4f, 1.0f }, Gfx::Align::Right);
    }

    if (m_State != State::Playing)
        return;

    // one line of guidance: a timed message wins over hints
    std::string hint = m_Message;
    if (hint.empty())
    {
        if (m_HasPotion)                hint = "F: throw the potion (run to throw further)";
        else if (spores == 3)           hint = "E: brew a light potion";
        else if (!m_Moved)              hint = "A / D or arrow keys: move";
        else if (!m_Jumped)             hint = "SPACE: jump (hold to jump higher)";
        else if (m_RunSeconds < 14.0f && !m_Spent) hint = "Find three glowing spores";
    }
    if (!hint.empty())
    {
        // Near the top: the camera stops at the cave floor, so the bottom of
        // the screen is where the player and the slime are in the last room.
        g.text(hint, g.viewW() / 2.0f, 84.0f, 16, white, Gfx::Align::Center);
    }
}

void Game::renderOverlay()
{
    Gfx& g = m_Gfx;
    const float cx = g.viewW() / 2.0f;
    const float cy = g.viewH() / 2.0f;
    const glm::vec4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const glm::vec4 soft{ 0.72f, 0.78f, 0.95f, 1.0f };
    const glm::vec4 teal{ 0.55f, 1.0f, 0.84f, 1.0f };
    const glm::vec4 amber{ 1.0f, 0.82f, 0.4f, 1.0f };
    const bool pulse = static_cast<int>(m_Time * 2.0f) % 2 == 0;
    using Align = Gfx::Align;

    switch (m_State)
    {
        case State::Title:
            dim(5);
            g.text("SPORE LANTERN", cx, cy - 150.0f, 32, teal, Align::Center);
            g.text("Gather three spores. Brew the light.\nBanish the slime that blocks the way out.",
                cx, cy - 92.0f, 16, soft, Align::Center);
            g.text(m_HaveSave ? "ENTER  continue from the shrine" : "ENTER  begin",
                cx, cy - 8.0f, 16, pulse ? white : amber, Align::Center);
            if (m_HaveSave)
                g.text("N  new game (erases the save)", cx, cy + 22.0f, 16, soft, Align::Center);
            g.text("ESC  quit", cx, cy + (m_HaveSave ? 52.0f : 22.0f), 16, soft, Align::Center);
            g.text("A/D or arrows  move      SPACE  jump\nE  brew potion           F  throw potion\nR  back to last shrine   ESC  pause",
                cx, cy + 120.0f, 16, { 0.55f, 0.6f, 0.75f, 1.0f }, Align::Center);
            break;

        case State::Paused:
            dim(5);
            g.text("PAUSED", cx, cy - 70.0f, 32, white, Align::Center);
            g.text("ENTER  resume\nR  back to the last shrine\nN  title screen", cx, cy - 6.0f, 16, soft, Align::Center);
            break;

        case State::Dying:
            break;

        case State::Lost:
            dim(std::min(6, static_cast<int>(m_StateTime * 12.0f)));
            g.text("YOUR LANTERN WENT DARK", cx, cy - 60.0f, 32, { 1.0f, 0.45f, 0.4f, 1.0f }, Align::Center);
            g.text(m_HaveSave ? "The shrine still remembers you." : "The cave waits at the entrance.",
                cx, cy - 4.0f, 16, soft, Align::Center);
            g.text("ENTER  try again", cx, cy + 40.0f, 16, pulse ? white : amber, Align::Center);
            break;

        case State::Won:
            dim(std::min(4, static_cast<int>(m_StateTime * 8.0f)));
            g.text("YOU CARRIED THE LIGHT OUT", cx, cy - 80.0f, 32, amber, Align::Center);
            g.text(std::format("Time {}    Lanterns lost {}", clockText(m_RunSeconds), m_Deaths),
                cx, cy - 22.0f, 16, white, Align::Center);
            g.text("ENTER  play again (the cave resets)", cx, cy + 30.0f, 16, pulse ? white : teal, Align::Center);
            break;

        case State::Playing:
            break;
    }
}

void Game::render()
{
    Gfx& g = m_Gfx;
    if (!g.beginFrame())
        return;

    glm::vec2 shake{ 0.0f, 0.0f };
    if (m_Shake > 0.2f)
        shake = { std::sin(m_Time * 91.0f) * m_Shake, std::cos(m_Time * 77.0f) * m_Shake * 0.7f };

    m_ViewTopLeft = glm::round(glm::vec2{
        m_Camera.x - g.viewW() / 2.0f + shake.x,
        m_Camera.y + g.viewH() / 2.0f + shake.y
    });
    g.useWorldCamera(m_ViewTopLeft);

    renderWorld();

    const std::vector<Light> lights = collectLights();
    renderDarkness(lights);

    // coloured glows and bright particles sit on top of the darkness
    if (m_LanternRadius > 1.0f && m_State != State::Lost)
    {
        const glm::vec2 p = m_Player.center();
        const float size = m_LanternRadius * 1.5f;
        g.drawCentered(g.tex("glow_warm"), { p.x + m_Facing * 17.0f, p.y - 1.0f }, { size, size });
    }
    for (size_t i = 0; i < m_Level.spores.size() && i < m_Carried.size(); i++)
        if (!m_Carried[i])
            g.drawCentered(g.tex("glow_teal"), sporePos(i), { 64.0f, 64.0f });
    for (const Level::Decor& d : m_Level.decor)
        if (d.kind == 'c')
        {
            // self-lit: drawn again over the darkness, with a halo
            g.draw(g.tex("crystal"), Level::cellTopLeft(d.cell), { TILE, TILE });
            g.drawCentered(g.tex("glow_blue"), Level::cellFloorCenter(d.cell) + glm::vec2{ 0.0f, 12.0f }, { 84.0f, 84.0f });
        }
    if (m_ShrineLit)
    {
        const glm::vec2 shrine = Level::cellFloorCenter(m_Level.shrine);
        g.drawCentered(g.tex("glow_blue"), { shrine.x, shrine.y + 42.0f }, { 110.0f, 110.0f });
    }
    if (m_DoorOpen)
        g.drawCentered(g.tex("glow_warm"), Level::cellFloorCenter(m_Level.door) + glm::vec2{ 0.0f, 28.0f }, { 200.0f, 200.0f });
    if (!m_Slime.dead && m_Slime.dying < 0.0f)
    {
        // the slime lurks in the dark: its eyes and a faint halo give it away
        const Slime& s = m_Slime;
        const float h = 44.0f * (1.0f - s.squash * 0.6f);
        g.drawCentered(g.tex("glow_purple"), { s.x, s.y + h * 0.5f }, { 96.0f, 96.0f });
        if (s.blink >= 0.0f)
            for (float side : { -1.0f, 1.0f })
                g.drawCentered(g.tex("pix_amber"), glm::round(glm::vec2{ s.x + side * 9.0f, s.y + h * 0.42f }), { 4.0f, 4.0f });
    }
    if (m_Potion)
        g.drawCentered(g.tex("glow_cyan"), m_Potion->body.center(), { 80.0f, 80.0f });
    if (m_Flash > 0.0f)
    {
        const int stage = std::clamp(static_cast<int>((1.0f - m_Flash) * 4.0f), 0, 3);
        const float size = 110.0f + (1.0f - m_Flash) * 330.0f;
        g.drawCentered(g.tex(std::format("burst_{}", stage)), m_FlashPos, { size, size });
    }
    for (const Particle& p : m_Particles)
        if (p.glow)
        {
            const float size = std::max(1.0f, std::round(p.size * (p.life / p.maxLife)));
            g.drawCentered(p.texture, glm::round(p.pos), { size, size });
        }

    g.useScreenCamera();
    renderHud();
    renderOverlay();

    g.endFrame();
}
