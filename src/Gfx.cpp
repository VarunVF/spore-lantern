#include "Gfx.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

constexpr const char* FONT_PATH = "assets/fonts/PressStart2P-Regular.ttf";

} // namespace

Gfx::Gfx(glgame2d::Window& window)
    : m_Window{ window },
    m_Font8{ FONT_PATH, 8.0f },
    m_Font16{ FONT_PATH, 16.0f },
    m_Font32{ FONT_PATH, 32.0f },
    m_Missing{ glgame2d::Texture::makePlaceholder() }
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator{ "assets/gfx", ec })
    {
        if (entry.path().extension() == ".png")
            m_Textures.try_emplace(entry.path().stem().string(), entry.path());
    }
    if (m_Textures.empty())
        glgame2d::logging::error("[Gfx] No textures found in assets/gfx");
}

const glgame2d::Texture* Gfx::tex(const std::string& name)
{
    auto it = m_Textures.find(name);
    if (it == m_Textures.end())
    {
        glgame2d::logging::warn("[Gfx] Missing texture '{}'", name);
        return &m_Missing;
    }
    return &it->second;
}

bool Gfx::beginFrame()
{
    m_Window.getSize(&m_WinW, &m_WinH);
    if (m_WinW <= 0 || m_WinH <= 0)
        return false;

    // Whole-number zoom keeps the pixel art sharp; any extra window area
    // simply shows more of the cave.
    const float fit = std::min(m_WinW / static_cast<float>(BASE_W), m_WinH / static_cast<float>(BASE_H));
    m_Scale = std::max(1.0f, std::floor(fit));
    m_ViewW = m_WinW / m_Scale;
    m_ViewH = m_WinH / m_Scale;

    m_Renderer.clear({ 0.02f, 0.016f, 0.047f, 1.0f });
    return true;
}

void Gfx::useWorldCamera(glm::vec2 viewTopLeft)
{
    m_ViewTopLeft = viewTopLeft;

    // The engine camera can only ease towards a sprite. With an easing of 1
    // it lands exactly on its target, which puts the given sprite position in
    // the middle of the window. The camera scale is applied before that
    // translation, so the target has to be pre-multiplied by it.
    const glm::vec2 center{ viewTopLeft.x + m_ViewW / 2.0f, viewTopLeft.y - m_ViewH / 2.0f };
    m_WorldCamera.setScale(m_Scale);
    m_WorldCamera.moveEaseTowards(glgame2d::Sprite{ center * m_Scale, { 0.0f, 0.0f } }, m_Window, 1.0f);
    m_Renderer.beginScene(m_WorldCamera);
}

void Gfx::useScreenCamera()
{
    m_ViewTopLeft = { 0.0f, 0.0f };
    const glm::vec2 center{ m_ViewW / 2.0f, -m_ViewH / 2.0f };
    m_ScreenCamera.setScale(m_Scale);
    m_ScreenCamera.moveEaseTowards(glgame2d::Sprite{ center * m_Scale, { 0.0f, 0.0f } }, m_Window, 1.0f);
    m_Renderer.beginScene(m_ScreenCamera);
}

void Gfx::drawUV(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size, const glm::vec4& uv)
{
    m_Draws++;
    m_Renderer.drawSprite(m_Window, glgame2d::Sprite{ topLeft, size, texture }, uv);
}

void Gfx::draw(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size)
{
    m_Draws++;
    m_Renderer.drawSprite(m_Window, glgame2d::Sprite{ topLeft, size, texture });
}

void Gfx::drawFrame(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size,
    int frame, int frameCount, bool flipX)
{
    float u0 = static_cast<float>(frame) / frameCount;
    float u1 = static_cast<float>(frame + 1) / frameCount;
    if (flipX)
        std::swap(u0, u1);
    drawUV(texture, topLeft, size, { u0, 0.0f, u1, 1.0f });
}

void Gfx::drawCentered(const glgame2d::Texture* texture, glm::vec2 center, glm::vec2 size)
{
    draw(texture, { center.x - size.x / 2.0f, center.y + size.y / 2.0f }, size);
}

void Gfx::screen(const glgame2d::Texture* texture, float x, float y, float w, float h)
{
    draw(texture, { x, -y }, { w, h });
}

void Gfx::screenFrame(const glgame2d::Texture* texture, float x, float y, float w, float h,
    int frame, int frameCount)
{
    drawFrame(texture, { x, -y }, { w, h }, frame, frameCount);
}

glm::vec2 Gfx::worldToScreen(glm::vec2 world) const
{
    return { world.x - m_ViewTopLeft.x, m_ViewTopLeft.y - world.y };
}

void Gfx::text(std::string_view str, float x, float y, int px, glm::vec4 color, Align align, bool shadow)
{
    // The engine cannot measure text. Press Start 2P is monospaced with an
    // advance of exactly one em, so the width is simply characters * px.
    float lineY = y;
    size_t start = 0;
    while (start <= str.size())
    {
        size_t end = str.find('\n', start);
        if (end == std::string_view::npos)
            end = str.size();
        const std::string_view line = str.substr(start, end - start);

        const float width = static_cast<float>(line.size()) * px;
        float lineX = x;
        if (align == Align::Center) lineX = x - width / 2.0f;
        if (align == Align::Right)  lineX = x - width;
        lineX = std::round(lineX);

        const float baseline = std::round(lineY) + px;
        if (shadow)
        {
            const float offset = std::max(1.0f, px / 8.0f);
            m_Text.push_back({ std::string{ line }, lineX + offset, baseline + offset, px,
                { 0.02f, 0.01f, 0.05f, color.a } });
        }
        m_Text.push_back({ std::string{ line }, lineX, baseline, px, color });

        lineY += px * 1.5f;
        start = end + 1;
    }
}

glgame2d::Font& Gfx::fontFor(float screenPx, float& sizeOut)
{
    sizeOut = 1.0f;
    if (screenPx == 8.0f)  return m_Font8;
    if (screenPx == 16.0f) return m_Font16;
    if (screenPx == 32.0f) return m_Font32;
    if (screenPx < 16.0f) { sizeOut = screenPx / 16.0f; return m_Font16; }
    sizeOut = screenPx / 32.0f;
    return m_Font32;
}

void Gfx::endFrame()
{
    // FontRenderer works in a space where y spans [-1, 1] and x spans
    // [-aspect, aspect], and it batches one font at a time.
    const float halfH = m_WinH / 2.0f;
    const float aspect = static_cast<float>(m_WinW) / static_cast<float>(m_WinH);

    for (glgame2d::Font* font : { &m_Font8, &m_Font16, &m_Font32 })
    {
        bool any = false;
        for (const TextItem& item : m_Text)
        {
            float size = 1.0f;
            if (&fontFor(item.px * m_Scale, size) != font)
                continue;
            if (!any)
            {
                m_FontRenderer.drawBegin(m_Window, m_TextCamera);
                any = true;
            }
            const glm::vec3 position{
                (item.x * m_Scale) / halfH - aspect,
                1.0f - (item.y * m_Scale) / halfH,
                0.0f
            };
            m_FontRenderer.drawText(*font, item.str, position, item.color, size);
        }
        if (any)
            m_FontRenderer.renderFrame(*font);
    }
    m_Text.clear();
    m_LastDraws = m_Draws;
    m_Draws = 0;

    m_Window.swapBuffers();
}
