#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glgame2d/glgame2d.hpp>
#include <glm/glm.hpp>

// Thin drawing layer over the engine's Renderer / FontRenderer / Camera.
//
// It adds the things the game needs that the engine does not offer directly:
// a camera that can be placed at an exact position, sprite-sheet frames and
// horizontal flipping (through the UV rectangle), screen-space drawing in
// pixels for the HUD, and text positioned in pixels with alignment.
class Gfx
{
public:
    // Size of the view in world pixels at scale 1 (also the window size).
    static constexpr int BASE_W = 960;
    static constexpr int BASE_H = 540;

    enum class Align { Left, Center, Right };

    explicit Gfx(glgame2d::Window& window);

    // Returns false when the window has no area (minimised): skip the frame.
    bool beginFrame();
    void endFrame();    // flushes queued text and presents

    // Visible area in world pixels (changes when the window is resized).
    float viewW() const { return m_ViewW; }
    float viewH() const { return m_ViewH; }

    // --- world space (engine convention: +y up, position = top-left) ------
    void useWorldCamera(glm::vec2 viewTopLeft);
    void draw(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size);
    void drawUV(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size, const glm::vec4& uv);
    void drawFrame(const glgame2d::Texture* texture, glm::vec2 topLeft, glm::vec2 size,
        int frame, int frameCount, bool flipX = false);
    void drawCentered(const glgame2d::Texture* texture, glm::vec2 center, glm::vec2 size);

    // --- screen space (pixels from the top-left of the view, +y down) -----
    void useScreenCamera();
    void screen(const glgame2d::Texture* texture, float x, float y, float w, float h);
    void screenFrame(const glgame2d::Texture* texture, float x, float y, float w, float h,
        int frame, int frameCount);

    // Text is queued and drawn on top of everything in endFrame().
    // (x, y) is the top of the first line; px is the glyph height in view pixels.
    void text(std::string_view str, float x, float y, int px, glm::vec4 color,
        Align align = Align::Left, bool shadow = true);

    glm::vec2 worldToScreen(glm::vec2 world) const;

    const glgame2d::Texture* tex(const std::string& name);
    glgame2d::Renderer& renderer() { return m_Renderer; }
    glgame2d::Window& window() { return m_Window; }

    // Sprites drawn through this class during the previous frame (debug overlay).
    int lastDrawCount() const { return m_LastDraws; }
    void countExternalDraws(int n) { m_Draws += n; }

private:
    struct TextItem
    {
        std::string str;
        float x, y;     // baseline-left, view pixels
        int px;
        glm::vec4 color;
    };

    glgame2d::Font& fontFor(float screenPx, float& sizeOut);

private:
    glgame2d::Window& m_Window;
    glgame2d::Renderer m_Renderer;
    glgame2d::FontRenderer m_FontRenderer;
    glgame2d::Font m_Font8, m_Font16, m_Font32;
    glgame2d::Camera m_WorldCamera, m_ScreenCamera, m_TextCamera;

    std::unordered_map<std::string, glgame2d::Texture> m_Textures;
    glgame2d::Texture m_Missing;

    std::vector<TextItem> m_Text;
    glm::vec2 m_ViewTopLeft{ 0.0f, 0.0f };
    int m_WinW = BASE_W, m_WinH = BASE_H;
    float m_Scale = 1.0f;
    int m_Draws = 0, m_LastDraws = 0;
    float m_ViewW = BASE_W, m_ViewH = BASE_H;
};
