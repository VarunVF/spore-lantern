# Friction log — building Spore Lantern on glgame2d

Engine: `VarunVF/glgame2d`, first at commit `58312b47956e48200a6422834cc1c0a43204a057`, now at `2033fd78fe35bd2257a66fbb00b7111c0a2f8cea` (`main`; the repository has no tags).
Platform: Ubuntu 24.04, GCC 13.3, CMake 3.28, X11.

Every entry has four parts: **Wanted**, **Did instead**, **Area**, **Kind**
(missing feature or bug). No engine code was changed. Entries marked
*(read, not hit)* come from reading the engine source while working around a
neighbouring problem; everything else was run into directly.

The first five are the ones that shaped the game the most.

## Fixed upstream

Entries are kept as written, with a **Status** line added when the engine fixes them.

| Entry | Fixed in | What changed | Game |
| --- | --- | --- | --- |
| 1. Every sprite draw asks the window system for the window size | `2033fd7` | `Window` caches its window and framebuffer sizes and updates them in GLFW callbacks. | Moved to `getWindowSize()`; frame work 16–18 ms → 6 ms (measured). |
| 32. Engine classes print to the console directly | `5f06002` | Engine classes log through `glgame2d::logging`. | Nothing to change. |

---

## Top five

### 1. Every sprite draw asks the window system for the window size

- **Wanted:** Draw a few thousand small sprites per frame (tiles, particles, a grid of darkness cells).
- **Did instead:** Treated sprites as expensive and designed around a budget of roughly 400 per frame: baked the whole tile layer into one image, cut the darkness from 16 alpha levels to 8, turned crystal and slime "lights" into plain glow sprites, and added a fallback that doubles the darkness cell size after two seconds of slow frames (`F4` toggles it).
- **Detail:** `Renderer::drawSprite(window, sprite)` calls `window.getSize()` on every call to turn pixels into clip space. On X11, `glfwGetWindowSize` is a synchronous round trip to the X server. Measured in isolation under Xvfb: 17.8 µs per call. The first playable build drew about 1,460 sprites a frame (861 tiles + about 600 others) and took 35–45 ms per frame; without the tilemap and darkness it took 8 ms (frame cap). The 861 tile draws alone cost about 16 ms, which is 861 × 18 µs. This was measured with a software rasteriser, but the round trip is independent of the GPU.
- **Area:** rendering
- **Kind:** bug (performance). Caching the size once per frame, or in the framebuffer-size callback the Window already installs, would remove it.
- **Status:** fixed in `2033fd7` (window and framebuffer sizes cached, updated from callbacks; `getSize()` replaced by `getWindowSize()` / `getFramebufferSize()`). Re-measured with `SPORE_PERF=1` over the scripted playthrough, same virtual display and software renderer: baked map 16–18 ms → 5.6–6.1 ms of work per frame at 300–400 sprites (now at the 120 fps cap); engine-drawn map (`F5`) 40–42 ms at about 1,070 sprites with coarse lighting forced on → 6.7–7.2 ms at about 1,250 sprites with full lighting. A sprite now costs roughly 1 µs here, so the 400-sprite budget no longer applies. The workarounds above are still in the game.

### 2. `Entity::update()` resolves collisions but tells you nothing about them

- **Wanted:** Know whether the player is standing on the ground (to allow a jump), whether a thrown potion hit something (to break it), and whether a hit was floor, wall or ceiling.
- **Did instead:** Subclassed `Entity` (`Body` in `src/Game.hpp`). Before calling `update()` it computes where a collision-free move would end up, using the same formula as the engine, and compares afterwards. A difference in x means wall; a difference in y means floor or ceiling depending on the sign of the vertical velocity.
- **Detail:** The `collisions[TOP/BOTTOM/LEFT/RIGHT]` array is a local variable in `update()`. `m_AirTime` is the only trace it leaves, it is protected with no getter, and it is reset to zero on a *ceiling* hit as well as a floor hit, so "air time is zero" cannot be used as "grounded": it would allow a second jump after bumping your head.
- **Area:** collision
- **Kind:** missing feature (collision result), plus a bug (`m_AirTime` reset by ceiling contact).

### 3. No way to jump, push or teleport an `Entity` from outside

- **Wanted:** `player.jump()`, knock-back when hurt, and placing the player at a saved position when `save.txt` is loaded.
- **Did instead:** The same `Body` subclass writes `m_Velocity.y` directly for jumps and throws, and its `teleport()` sets both `m_Displacement` and `m_Sprite.position`. Horizontal motion (running, knock-back) goes through the `inputVelocity` argument.
- **Detail:** Velocity and acceleration are protected with no accessors. `Entity::move(dx, dy)` looks like the teleport API but only changes `m_Sprite.position`; the next `update()` overwrites that with `m_Displacement`, so a moved entity snaps back. Gravity and terminal velocity can only be set in the constructor (the default terminal velocity of -200 px/s is a slow fall).
- **Area:** collision
- **Kind:** bug (`move()` is undone by `update()`), plus missing feature (velocity access / impulse).

### 4. The tilemap can be drawn and collided with, but not queried

- **Wanted:** Author the level in one place: solid ground, background decoration that does not collide, spikes, and markers for the player start, spores, shrine, slime and door.
- **Did instead:** The level is an ASCII file (`assets/level/level.txt`). `tools/build_level.py` turns it into a Tiled JSON map: the `#` cells become a solid tile layer and the decoration (stalactites, mushrooms, crystals) becomes an object layer of non-solid tile objects. The game parses the same ASCII file itself for everything it needs a position for (spawn, spores, spikes, shrine, slime, door, crystal glows) and draws spikes and props as its own sprites.
- **Detail:** Non-solid tiles *are* supported, through object layers: a tile object (an object with a `gid`) is drawn by `Tilemap::render()` and gets no collision rectangle, which is how the engine's own example map does it. I missed this at first and wrote here that every tile is solid; that was wrong and is corrected. Checked afterwards with a test map: three tile objects in open air were rendered, the collision rectangle count was unchanged (865 with and without the layer), and a dropped `Entity` fell through them to the floor. What is true: every tile in a *tile layer* is solid, in every tile layer, so non-solid tiles cannot be painted on a grid layer and must be placed as objects.
  The part that still forced the ASCII file: there is no "what tile is at (x, y)", and no access to tile ids, layer names, Tiled custom properties or the objects themselves, so the game cannot read where a spawn point, a spore or a spike is from the map. The loader also reads `object["gid"]` unconditionally, so point or rectangle objects (the usual way to place markers) are not supported *(read, not hit)*. A failed load only logs; there is no way to ask the Tilemap whether it loaded.
  Decoration now lives in an object layer of the map. The game still does not call `Tilemap::render()` in normal play (entries 1 and 9); it draws a picture baked from the same two layers. `F5` switches to the engine's rendering: a paused frame looked the same both ways (29 objects and all tiles in place; pixel differences of at most 9/255, confined to the flickering lantern light), at 1,066 sprites and 30 ms instead of 336 sprites and 13.5 ms (before entry 1 was fixed; now about 7 ms against 6 ms).
- **Area:** collision / assets
- **Kind:** missing feature (map queries, markers). Non-solid tiles: not a gap, only undocumented.

### 5. Sprites have a texture and a rectangle, and nothing else

- **Wanted:** Darkness around the lantern with several light sources; fade to black; a white flash when hit; fading particles; a spinning potion.
- **Did instead:**
  - *Darkness:* a world-aligned grid of 16 px squares, each drawn with one of eight pre-made 4×4 PNGs of the same colour at increasing alpha, chosen from the brightest light reaching the cell. Runs of equal cells in a row are merged into one stretched sprite. This is what gives the game its blocky light.
  - *Fades and dimming:* the same alpha swatches stretched over the screen, stepping through the levels.
  - *Hit flash:* a second, all-white copy of the player sprite sheet.
  - *Particles:* shrink instead of fading.
  - *Rotation:* dropped. The potion does not spin.
- **Detail:** No tint or alpha per sprite, no rotation, no blend mode (additive light is impossible), no render target, no custom shader hook on `Renderer`, no rectangle or line primitive, and a `Texture` can only be made from a file (no solid colour, no pixel data), hence the swatch PNGs.
- **Area:** rendering
- **Kind:** missing feature.

---

## Collision

### 6. Collision only looks at the 3×3 tiles around the entity's top-left corner

- **Wanted:** A slime bigger than one tile (60×44 px on 32 px tiles) that collides with the cave.
- **Did instead:** Kept every `Entity` at or under one tile (player 20×28, potion 10×10). The slime does not use `Entity` at all: it has its own hop physics on a flat floor whose extent is read from the ASCII level.
- **Detail:** `Tilemap::tilesAround` takes the tile under `rect.x, rect.y` and its eight neighbours. Anything wider or taller than a tile can pass through tiles further from that corner. It also uses `int(x) / tileWidth`, which truncates towards zero, so the cell is wrong for negative x and positive y; the level therefore lives entirely at x ≥ 0, y ≤ 0 behind a solid border *(sign issue read, not hit)*.
- **Area:** collision
- **Kind:** bug.

### 7. No one-way platforms, slopes, triggers or hazards

- **Wanted:** Spikes that hurt, a shrine and a door you can touch, platforms you can jump up through (the hub stairs bump your head from below).
- **Did instead:** Hand-written rectangle overlap tests in `Game::stepPickups` using `Rect::collides`. The stairs stayed solid; the level is laid out so each step is approached from the open side.
- **Area:** collision
- **Kind:** missing feature.

### 8. `Rect` uses a top-left origin with +y up, and asserts on empty rectangles

- **Wanted:** Build overlap rectangles without thinking about it.
- **Did instead:** Wrote a `rectAround(center, w, h)` helper and kept every rectangle strictly positive in size (the slime's squashes with its animation, so it is clamped by construction).
- **Detail:** A sprite's position is its top-left corner but y grows upwards, so the rectangle extends *down* from `y` to `y - h`. This is consistent across the engine but appears only in a comment inside `Rect::collides`.
- **Area:** collision
- **Kind:** missing feature (documentation).

## Rendering

### 9. `Tilemap::render()` draws every tile of the map every frame

- **Wanted:** Draw the visible part of a 70×22 map.
- **Did instead:** `tools/build_level.py` composes the tile layer into one 2240×704 PNG and the game draws it as a single sprite. The `Tilemap` object is still loaded and used for all collision; its `render()` is not called.
- **Detail:** No culling against the camera, one draw call and three uniform uploads per tile, no batching. With entry 1 that was 861 X round trips per frame. Since entry 1 was fixed it costs about 1 ms per frame more than the baked image, so this is now minor.
- **Area:** rendering
- **Kind:** missing feature (culling / batching).

### 10. The camera cannot be put at a position

- **Wanted:** A camera that follows the player with look-ahead, stops at the level edges, shakes, and sits on whole pixels.
- **Did instead:** The game keeps its own camera position and, every frame, calls `camera.moveEaseTowards(fakeSprite, window, 1.0f)`. With an easing of 1 the engine camera lands exactly on its target, so a fake zero-size sprite at the wanted centre works as a "set position".
- **Detail:** `Camera` has no `setPosition` or `getPosition`. `move(x, y)` edits the view matrix directly and is thrown away by the next `moveEaseTowards` (which rebuilds the matrix from `m_Position`, which `move` never updates). `moveEaseTowards` divides by a fixed easing per call, so its speed depends on the frame rate, and it cannot be clamped.
- **Area:** rendering
- **Kind:** missing feature (`setPosition`), bug (`move()` and `moveEaseTowards()` do not compose; easing is frame-rate dependent).

### 11. Camera scale is applied before the translation and ignored by the follow code

- **Wanted:** Zoom 2× when the window is large enough.
- **Did instead:** Multiply the follow target by the scale before passing it to `moveEaseTowards` (see `Gfx::useWorldCamera`).
- **Detail:** `setScale()` only stores the factor; nothing changes until the next `moveEaseTowards`. The view matrix is translate × scale, while `moveEaseTowards` computes its target as if the scale were 1, so with any other scale the followed sprite is no longer centred.
- **Area:** rendering
- **Kind:** bug.

### 12. One world unit is one window pixel; there is no virtual resolution

- **Wanted:** The game to look the same in a 960×540 window and full screen.
- **Did instead:** `Gfx::beginFrame` picks the largest whole-number zoom that fits 960×540 into the window and applies it through the camera scale; any leftover window area shows more of the cave. `--size` and `--fullscreen` command-line options were added to make this usable.
- **Detail:** `drawSprite` divides positions by the current window size, so resizing the window changes how much of the world is visible. `Window` cannot be made non-resizable and has no minimum size, no letterboxing and no resize callback for the game.
- **Area:** rendering / UI
- **Kind:** missing feature.

### 13. Texture filtering is fixed to linear, and textures do not know their size

- **Wanted:** Crisp pixel art drawn at 2× from small source images; sprite-sheet frames addressed in pixels.
- **Did instead:** `tools/make_art.py` upscales all pixel art 2× with nearest-neighbour so textures are drawn 1:1, and the game rounds draw positions and the camera to whole pixels. Frames are addressed as "frame i of n" because the game cannot ask a `Texture` for its width or height; sprite sizes are hard-coded next to each draw.
- **Detail:** `GL_LINEAR` and `GL_REPEAT` are hard-coded in `Texture`. Mipmaps are generated but the minification filter does not use them.
- **Area:** rendering / assets
- **Kind:** missing feature.

### 14. Sprite sheets and flipping work only through an undocumented UV rectangle

- **Wanted:** `drawFrame(sheet, index)` and `flipX`.
- **Did instead:** `Gfx::drawFrame` builds the UV rectangle and swaps `u0`/`u1` to mirror the sprite.
- **Detail:** The vertex shader computes `mix(uv.xy, uv.zw, a_Pos)` with `a_Pos.y` in [-1, 0], so the bottom edge samples `2·v0 − v1`, not `v0`. The default rectangle (0,0,1,1) therefore samples v from −1 to 0 and only shows the right picture because the wrap mode is `GL_REPEAT`. Working out how to address a row of a sheet meant reading the shader and `Tilemap::computeTileUV`.
- **Area:** rendering
- **Kind:** missing feature (API and documentation); the reliance on `GL_REPEAT` is fragile.

### 15. No draw order control beyond call order, no layers

- **Wanted:** "Glowing things above the darkness, HUD above that, text above everything."
- **Did instead:** Ordered the draw calls by hand in `Game::render`, and queued all text to flush at the end of the frame.
- **Area:** rendering
- **Kind:** missing feature (minor).

## Text

### 16. Text cannot be measured

- **Wanted:** Centred titles and right-aligned HUD numbers.
- **Did instead:** Chose a monospaced font whose advance is exactly one em (Press Start 2P), so width = characters × pixel size. A proportional font would have been impossible to centre.
- **Detail:** Glyph metrics are private to `Font` with `FontRenderer` as a friend. There is no `measure()`, no alignment option and no wrapping.
- **Area:** text
- **Kind:** missing feature.

### 17. Text lives in a different coordinate space from sprites

- **Wanted:** Put text at pixel (x, y) next to a HUD sprite.
- **Did instead:** `Gfx::text` takes view pixels and converts them in `Gfx::endFrame`.
- **Detail:** `FontRenderer` uses an orthographic space where y spans [−1, 1] and x spans [−aspect, aspect], the position is the baseline, and the size is a multiplier on the size the `Font` was created with. Sprites use window pixels from the top-left. Nothing converts between them.
- **Area:** text
- **Kind:** missing feature.

### 18. The font atlas is a fixed 512×512, one size per `Font`, ASCII only

- **Wanted:** A big 64 px title, and crisp text at 8, 16 and 32 px.
- **Did instead:** Created three `Font` objects (8, 16, 32 px) and render each size with the matching one. The title is 32 px.
- **Detail:** At 64 px the 95 glyphs do not fit the atlas and the font fails to load. Scaling a smaller font up through the `size` argument blurs it (linear filtering). Each `Font` needs its own `drawBegin`/`renderFrame` batch, enforced by asserts. Code points outside 32–126 are skipped.
- **Area:** text
- **Kind:** missing feature.

## Audio

### 19. The mixer can start a sound and nothing else

- **Wanted:** Looping ambience, per-sound volume, stopping sounds, slight pitch variation on footsteps.
- **Did instead:**
  - *Looping:* `ambience.wav` is 27 s long: 24 s of material that repeats exactly, with a 3 s linear fade-in at the head and fade-out at the tail. The game starts a new copy every 24 s and the overlapping fades sum to a seamless loop (`src/Audio.hpp`, `tools/make_audio.py`).
  - *Volume:* baked into each file by the generator script.
  - *Stop / pitch:* dropped. Three separate spore chimes stand in for pitch variation.
- **Area:** audio
- **Kind:** missing feature.

### 20. `Mixer::load()` returns a miniaudio sound by value

- **Wanted:** Load each effect once and replay it.
- **Did instead:** Used only `Mixer::play(const char* file)`, which lets miniaudio load and recycle the sound.
- **Detail:** `Mixer::Sound` holds an `ma_sound` and `load()` returns it by value. An initialised `ma_sound` is registered with the engine's node graph by address, so copying or moving the struct leaves the engine pointing at a dead object. I did not risk it; the engine's own example has its music lines commented out. *(read, not hit)*
- **Area:** audio
- **Kind:** bug.

### 21. No audio device means the process exits

- **Wanted:** Run the game, and the automated play-test, on a machine without sound.
- **Did instead:** `Audio` holds a `std::optional<Mixer>` and does not construct it when `--mute` or `SPORE_MUTE` is given.
- **Detail:** `Mixer::Mixer()` calls `std::exit(result)` when `ma_engine_init` fails. A game cannot catch that and continue silently. *(read, not hit: this machine has audio)*
- **Area:** audio
- **Kind:** bug.

## Input

### 22. Key edges are per rendered frame

- **Wanted:** A fixed 120 Hz simulation step that never misses a jump press and never sees one twice.
- **Did instead:** `main.cpp` ORs `isJustPressed` results into a `Controls` struct and clears them only after a simulation step has consumed them.
- **Area:** input
- **Kind:** missing feature (minor).

### 23. No way to ask the window to close

- **Wanted:** "Esc on the title screen quits."
- **Did instead:** The game sets its own quit flag and the main loop checks it next to `window.shouldClose()`.
- **Detail:** `Window` has `shouldClose()` but no `close()`. `getGLFWWindow()` would allow calling GLFW directly; I avoided reaching around the engine.
- **Area:** input / UI
- **Kind:** missing feature (minor).

## UI

### 24. No screen-space drawing

- **Wanted:** Hearts and spore icons pinned to the corner of the screen, in pixels.
- **Did instead:** A second `Camera` kept at the origin (through the same `moveEaseTowards` trick as entry 10) and helper functions that flip y, in `Gfx::screen`.
- **Area:** UI
- **Kind:** missing feature.

## Save

### 25. No persistence helpers

- **Wanted:** Save and load a handful of values.
- **Did instead:** A small key/value text format written with `<fstream>` (`src/Save.cpp`), validated on load (bad files and positions inside rock are rejected). nlohmann_json is available transitively through the engine, but depending on that by accident seemed worse than forty lines of parsing.
- **Area:** save
- **Kind:** missing feature (expected for an engine this size; listed for completeness).

## Assets

### 26. All paths are relative to the working directory

- **Wanted:** Start the game from any directory, including by double-clicking it.
- **Did instead:** `main()` changes the working directory to the executable's directory, where CMake copies `assets/`. `save.txt` lives there too.
- **Area:** assets
- **Kind:** missing feature.

### 27. `Window::setIcon()` comes out upside down after any texture has loaded

- **Wanted:** Set the window icon.
- **Did instead:** Call `setIcon()` before creating the first `Texture`, with a comment saying why.
- **Detail:** `Texture`'s constructor calls `stbi_set_flip_vertically_on_load(true)`, which is global state in stb_image. `setIcon()` loads with `stbi_load` and never sets it back, so the icon is flipped or not depending on call order. *(Found by reading; avoided rather than reproduced, since Xvfb shows no icons.)*
- **Area:** assets
- **Kind:** bug.

### 28. Missing files degrade silently

- **Wanted:** Know at start-up that an asset is missing.
- **Did instead:** `Gfx::tex()` warns once per lookup of an unknown name; file-level failures rely on the engine's console output.
- **Detail:** A `Texture` that fails to load becomes a magenta placeholder (good), but there is no `isValid()` on `Texture`, `Font` or `Tilemap` for the game to check.
- **Area:** assets
- **Kind:** missing feature (minor).

## Build

### 29. No tags or releases to pin

- **Wanted:** `GIT_TAG v0.x` for a reproducible build.
- **Did instead:** `GIT_TAG main`, and the build prints the fetched commit (`glgame2d commit: …`), which is recorded in the README.
- **Area:** build
- **Kind:** missing feature.

### 30. The first configure takes minutes and about 500 MB

- **Wanted:** A quick `cmake -S . -B build`.
- **Did instead:** Waited. Documented the time in the README.
- **Detail:** The engine fetches GLFW, GLM and nlohmann_json as full git clones (no `GIT_SHALLOW`). On this machine the first configure took 163 s and `_deps` holds 348 MB for nlohmann_json, 100 MB for GLM, 28 MB for the engine and 24 MB for GLFW. The game only uses nlohmann_json indirectly, through `Tilemap`.
- **Area:** build
- **Kind:** missing feature (shallow clones or release archives).

### 31. Build settings do not carry to the consumer, and vendored headers do

- **Wanted:** `target_link_libraries(game PRIVATE glgame2d)` and nothing else.
- **Did instead:** Set C++23 and a default `Release` build type in the game's own `CMakeLists.txt`.
- **Detail:** The engine sets `CMAKE_CXX_STANDARD` as a directory variable rather than `target_compile_features(glgame2d PUBLIC cxx_std_23)`, so a consumer that forgets it compiles engine headers that include `<format>` with the compiler's default standard *(read, not hit: I set it from the start, as the task said)*. With no build type the engine compiles unoptimised with `GLCall` checking `glGetError` around every GL call. The public include directory also exposes the vendored `glad`, `stb_*` and `miniaudio.h` headers to the game, and there is no namespaced alias target (`glgame2d::glgame2d`) or install/export. In the engine's CMake, `${OPENGL_INCLUDE_DIR}` is used before `find_package(OpenGL)` runs *(read, not hit: harmless on Linux)*.
- **Area:** build
- **Kind:** missing feature, plus a small ordering bug.

## Other

### 32. Engine classes print to the console directly

- **Wanted:** One log stream the game controls (the engine has a sink-based `logging` API).
- **Did instead:** Used `glgame2d::logging` for the game's own messages and lived with the rest.
- **Detail:** `Texture`, `Tilemap`, `Font`, `Shader`, `Window` and `Mixer` write to `std::cout` / `std::clog` / `std::cerr` and bypass the sink, so they cannot be silenced or redirected. Loading the game's 46 textures prints 46 lines.
- **Area:** other
- **Kind:** missing feature.
- **Status:** fixed in `5f06002` (engine classes now log through `glgame2d::logging`; no direct `std::cout` / `std::clog` / `std::cerr` left).

### 33. No licence file for the engine itself

- **Wanted:** State the engine's licence in this repository's README.
- **Did instead:** Linked to the engine and noted that only `THIRD_PARTY_LICENSES.md` exists upstream. The game does not vendor or redistribute engine source; it is fetched at configure time.
- **Area:** other
- **Kind:** missing feature.

### 34. No frame-rate-independent game loop helper

- **Wanted:** A fixed timestep.
- **Did instead:** A standard accumulator loop in `main.cpp` around `Clock::tick()`.
- **Detail:** `Clock::tick(true)` sleeps to cap the frame rate, which works; there is just nothing above it. The engine example passes the raw frame delta straight into `Entity::update`, which makes jump height depend on frame rate.
- **Area:** other
- **Kind:** missing feature (minor).
