// Phase Pong — Enhanced Edition
// Uses raylib (fetched via CMake). Compile exactly as before.
//
// NEW MECHANICS
//   • Multiple simultaneous balls (up to 4 at high scores)
//   • Expanding phase count: 2 → 3 → 4 colours as score rises
//   • Two-direction phase cycling:  LClick / Space = forward
//                                   RClick / Shift  = backward
//   • 3 lives — a missed / wrong-phase ball costs one life
//   • Combo multiplier — consecutive catches ×2 at 3+, ×4 at 6+
//   • Quantum Ball (gold, rare) — worth 5 pts + triggers slow-motion
//   • Particle bursts, ball motion trails, screen shake on miss
//   • Score popups that float above the catch point
//   • Neon CRT aesthetic (dark bg, glowing elements, scanlines)

#include "raylib.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

// ─── tunables ────────────────────────────────────────────────────────────────
static constexpr int   MAX_BALLS      = 4;
static constexpr int   MAX_PARTICLES  = 120;
static constexpr int   MAX_POPUPS     = 12;
static constexpr int   TRAIL_LEN      = 10;
static constexpr int   MAX_LIVES      = 3;
static constexpr float BASE_SPEED     = 320.f;   // px/s at score 0
static constexpr float SPEED_SCALE    = 12.f;    // extra px/s per point
static constexpr float SLOWMO_SCALE   = 0.35f;
static constexpr float SLOWMO_SECS    = 3.5f;
static constexpr float SHAKE_PEAK     = 14.f;
static constexpr float SHAKE_SECS     = 0.45f;

// ─── phase palette ───────────────────────────────────────────────────────────
static constexpr int MAX_PHASES = 4;
static const Color PHASE_COL[MAX_PHASES] = {
    {100, 160, 255, 255},   // 0  electric blue
    {255,  85,  85, 255},   // 1  hot red
    { 80, 235, 130, 255},   // 2  neon green
    {255, 210,  50, 255},   // 3  gold (quantum)
};
static const char* PHASE_NAME[MAX_PHASES] = { "ALPHA","BETA","GAMMA","QUANTUM" };

// ─── data types ──────────────────────────────────────────────────────────────
struct Ball {
    float  x, y;
    float  speed;
    int    phase;
    bool   isQuantum;
    bool   active;
    float  trailX[TRAIL_LEN];
    float  trailY[TRAIL_LEN];
    int    trailHead;
};

struct Particle {
    float  x, y, vx, vy;
    float  life;
    Color  color;
    float  radius;
    bool   active;
};

struct Popup {
    float  x, y;
    float  life;
    int    value;
    bool   active;
};

// ─── global state ────────────────────────────────────────────────────────────
static Ball      balls[MAX_BALLS];
static Particle  parts[MAX_PARTICLES];
static Popup     popups[MAX_POPUPS];

static int   score       = 0;
static int   lives       = MAX_LIVES;
static int   combo       = 0;
static int   maxCombo    = 0;
static int   paddlePhase = 0;
static int   numPhases   = 2;
static bool  gameOver    = false;

static float shakeTimer  = 0.f;
static float flashTimer  = 0.f;
static Color flashColor  = {};
static float slowmoTimer = 0.f;
static float spawnTimer  = 0.f;
static float spawnDelay  = 1.6f;

// ─── helpers ─────────────────────────────────────────────────────────────────
static void SpawnParticles(float x, float y, Color col,
                           int count, float speed, float radius = 3.5f)
{
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; ++i) {
        if (parts[i].active) continue;
        parts[i].active  = true;
        parts[i].x       = x;
        parts[i].y       = y;
        float ang        = (float)GetRandomValue(0, 359) * DEG2RAD;
        float spd        = speed * (0.4f + (float)GetRandomValue(0, 100) * 0.006f);
        parts[i].vx      = cosf(ang) * spd;
        parts[i].vy      = sinf(ang) * spd - (float)GetRandomValue(0, 40) * 0.05f;
        parts[i].life    = 1.f;
        parts[i].color   = col;
        parts[i].radius  = radius;
        ++spawned;
    }
}

static void SpawnPopup(float x, float y, int value)
{
    for (int i = 0; i < MAX_POPUPS; ++i) {
        if (popups[i].active) continue;
        popups[i] = { x, y - 20.f, 1.f, value, true };
        return;
    }
}

static void SpawnBall()
{
    int w = GetScreenWidth();
    for (int i = 0; i < MAX_BALLS; ++i) {
        if (balls[i].active) continue;
        balls[i].active    = true;
        balls[i].isQuantum = (numPhases >= 3) && (GetRandomValue(0, 22) == 0);
        balls[i].phase     = balls[i].isQuantum ? 3 : GetRandomValue(0, numPhases - 1);
        balls[i].speed     = BASE_SPEED + score * SPEED_SCALE;
        balls[i].x         = (float)(w / 2 + GetRandomValue(-w * 3 / 10, w * 3 / 10));
        balls[i].y         = -20.f;
        balls[i].trailHead = 0;
        for (int t = 0; t < TRAIL_LEN; ++t) {
            balls[i].trailX[t] = balls[i].x;
            balls[i].trailY[t] = balls[i].y;
        }
        return;
    }
}

static void ResetGame()
{
    score = 0; lives = MAX_LIVES; combo = maxCombo = 0;
    paddlePhase = 0; numPhases = 2; gameOver = false;
    shakeTimer = flashTimer = slowmoTimer = spawnTimer = 0.f;
    spawnDelay = 1.6f;
    for (auto& b : balls)  b.active  = false;
    for (auto& p : parts)  p.active  = false;
    for (auto& p : popups) p.active  = false;
    SpawnBall();
}

// ─── main loop ───────────────────────────────────────────────────────────────
void UpdateDrawFrame()
{
    const int   SW  = GetScreenWidth();
    const int   SH  = GetScreenHeight();
    float       dt  = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    float gamedt = (slowmoTimer > 0.f) ? dt * SLOWMO_SCALE : dt;

    // Layout
    float ballR    = SW * 0.038f;
    float padW     = SW * 0.28f;
    float padH     = SH * 0.022f;
    float padX     = (SW - padW) * 0.5f;
    float padY     = SH * 0.87f;
    float catchTop = padY - ballR * 0.5f;

    // Phase count progression
    numPhases = (score >= 20) ? 4 : (score >= 10) ? 3 : 2;
    if (paddlePhase >= numPhases) paddlePhase = numPhases - 1;

    // Input
    if (!gameOver) {
        bool fwd = IsMouseButtonPressed(MOUSE_LEFT_BUTTON)  || IsKeyPressed(KEY_SPACE);
        bool bwd = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_LEFT_SHIFT);
        if (fwd) paddlePhase = (paddlePhase + 1)             % numPhases;
        if (bwd) paddlePhase = (paddlePhase + numPhases - 1) % numPhases;
    } else {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_SPACE)) ResetGame();
    }

    // Game update
    if (!gameOver) {

        // Spawn
        spawnTimer -= gamedt;
        if (spawnTimer <= 0.f) {
            int active = 0;
            for (auto& b : balls) if (b.active) ++active;
            int maxActive = std::min(1 + score / 6, MAX_BALLS);
            if (active < maxActive) SpawnBall();
            spawnDelay = std::max(0.45f, 1.8f - score * 0.04f);
            spawnTimer = spawnDelay;
        }

        // Balls
        for (auto& b : balls) {
            if (!b.active) continue;

            b.trailX[b.trailHead] = b.x;
            b.trailY[b.trailHead] = b.y;
            b.trailHead = (b.trailHead + 1) % TRAIL_LEN;

            b.y += b.speed * gamedt;

            if (b.y >= catchTop && b.y < catchTop + padH + ballR * 1.5f) {
                if (b.x >= padX && b.x <= padX + padW) {
                    bool hit = b.isQuantum || (b.phase == paddlePhase);
                    if (hit) {
                        ++combo;
                        if (combo > maxCombo) maxCombo = combo;
                        int pts = b.isQuantum ? 5 : 1;
                        if      (combo >= 6) pts *= 4;
                        else if (combo >= 3) pts *= 2;
                        score += pts;
                        SpawnParticles(b.x, padY, PHASE_COL[b.phase], 22, 220.f, 4.f);
                        SpawnPopup(b.x, padY, pts);
                        if (b.isQuantum) {
                            slowmoTimer = SLOWMO_SECS;
                            flashTimer  = 0.25f;
                            flashColor  = { PHASE_COL[3].r, PHASE_COL[3].g, PHASE_COL[3].b, 90 };
                        }
                    } else {
                        combo = 0; --lives;
                        SpawnParticles(b.x, padY, PHASE_COL[b.phase], 30, 300.f, 5.f);
                        shakeTimer = SHAKE_SECS;
                        flashTimer = 0.18f;
                        flashColor = { 200, 20, 20, 80 };
                        if (lives <= 0) gameOver = true;
                    }
                    b.active = false;
                    continue;
                }
            }

            if (b.y > SH + ballR * 2.f) {
                combo = 0; --lives;
                SpawnParticles(b.x, (float)SH - 30.f, { 120, 120, 140, 255 }, 16, 200.f);
                shakeTimer = SHAKE_SECS * 0.6f;
                b.active   = false;
                if (lives <= 0) gameOver = true;
            }
        }

        for (auto& p : parts) {
            if (!p.active) continue;
            p.x    += p.vx * gamedt;
            p.y    += p.vy * gamedt;
            p.vy   += 400.f * gamedt;
            p.life -= gamedt * 1.8f;
            if (p.life <= 0.f) p.active = false;
        }

        for (auto& p : popups) {
            if (!p.active) continue;
            p.y    -= 80.f * dt;
            p.life -= dt * 1.3f;
            if (p.life <= 0.f) p.active = false;
        }

        shakeTimer  -= dt; if (shakeTimer  < 0.f) shakeTimer  = 0.f;
        flashTimer  -= dt; if (flashTimer  < 0.f) flashTimer  = 0.f;
        slowmoTimer -= dt; if (slowmoTimer < 0.f) slowmoTimer = 0.f;
    }

    // Draw
    float sx = 0.f, sy = 0.f;
    if (shakeTimer > 0.f) {
        float t = shakeTimer / SHAKE_SECS;
        sx = (float)GetRandomValue(-1, 1) * SHAKE_PEAK * t;
        sy = (float)GetRandomValue(-1, 1) * SHAKE_PEAK * t;
    }

    BeginDrawing();
    ClearBackground({ 7, 7, 18, 255 });

    // Scanlines
    for (int y = 0; y < SH; y += 5)
        DrawRectangle(0, y, SW, 1, { 0, 0, 0, 35 });

    // Flash
    if (flashTimer > 0.f) {
        Color fc = flashColor;
        fc.a = (unsigned char)(fc.a * (flashTimer / 0.25f));
        DrawRectangle(0, 0, SW, SH, fc);
    }

    // Quantum vignette
    if (slowmoTimer > 0.f) {
        float alpha = std::min(1.f, slowmoTimer / 0.5f) * 40.f;
        Color vc = { PHASE_COL[3].r, PHASE_COL[3].g, PHASE_COL[3].b, (unsigned char)alpha };
        DrawRectangleLinesEx({ 0, 0, (float)SW, (float)SH }, 14.f, vc);
    }

    // Particles
    for (auto& p : parts) {
        if (!p.active) continue;
        Color c = p.color;
        c.a = (unsigned char)(p.life * 210.f);
        DrawCircleV({ p.x + sx, p.y + sy }, p.radius * p.life, c);
    }

    // Balls + trails
    for (auto& b : balls) {
        if (!b.active) continue;
        Color bc = PHASE_COL[b.phase];

        for (int t = 1; t < TRAIL_LEN; ++t) {
            int   idx  = (b.trailHead - t - 1 + TRAIL_LEN) % TRAIL_LEN;
            float frac = (float)(TRAIL_LEN - t) / TRAIL_LEN;
            float r    = ballR * frac * 0.75f;
            Color tc   = bc;
            tc.a       = (unsigned char)(frac * frac * 140.f);
            DrawCircleV({ b.trailX[idx] + sx, b.trailY[idx] + sy }, r, tc);
        }

        Color glow = bc; glow.a = 55;
        DrawCircleV({ b.x + sx, b.y + sy }, ballR * 1.9f, glow);
        glow.a = 90;
        DrawCircleV({ b.x + sx, b.y + sy }, ballR * 1.35f, glow);

        float pulse = b.isQuantum ? (1.f + sinf((float)GetTime() * 9.f) * 0.18f) : 1.f;
        DrawCircleV({ b.x + sx, b.y + sy }, ballR * pulse, bc);

        int fntSz = (int)(ballR * 0.85f);
        if (fntSz < 8) fntSz = 8;
        const char* lbl = b.isQuantum ? "Q" : (b.phase == 0 ? "A" : b.phase == 1 ? "B" : "G");
        int tw = MeasureText(lbl, fntSz);
        DrawText(lbl,
                 (int)(b.x + sx - tw * 0.5f),
                 (int)(b.y + sy - fntSz * 0.5f),
                 fntSz, { 0, 0, 0, 180 });
    }

    // Paddle
    Color pc = PHASE_COL[paddlePhase];
    {
        Color g = pc; g.a = 30;
        DrawRectangle((int)(padX - 8.f + sx), (int)(padY - 6.f + sy),
                      (int)(padW + 16.f), (int)(padH + 12.f), g);
        g.a = 55;
        DrawRectangle((int)(padX - 4.f + sx), (int)(padY - 3.f + sy),
                      (int)(padW + 8.f), (int)(padH + 6.f), g);
    }
    DrawRectangleRounded({ padX + sx, padY + sy, padW, padH }, 0.55f, 8, pc);
    DrawRectangle((int)(padX + padW * 0.3f + sx), (int)(padY + padH * 0.2f + sy),
                  (int)(padW * 0.4f), (int)(padH * 0.35f),
                  { 255, 255, 255, 60 });

    // Phase selector (bottom-left)
    {
        float dotR   = ballR * 0.55f;
        float rowY   = SH * 0.94f;
        float startX = SW * 0.05f;
        float step   = SW * 0.08f;
        for (int p = 0; p < numPhases; ++p) {
            float cx   = startX + p * step;
            Color col  = PHASE_COL[p];
            bool  sel  = (p == paddlePhase);
            col.a      = sel ? 255 : 70;
            DrawCircleV({ cx, rowY }, dotR, col);
            if (sel) DrawCircleLinesV({ cx, rowY }, dotR * 1.4f, { 255, 255, 255, 180 });
            int fz = (int)(dotR * 0.9f); if (fz < 7) fz = 7;
            const char* pn = PHASE_NAME[p];
            int tw = MeasureText(pn, fz);
            DrawText(pn, (int)(cx - tw * 0.5f), (int)(rowY + dotR + 2.f),
                     fz, sel ? WHITE : Color{ 120, 120, 140, 255 });
        }
    }

    // Lives (top-right)
    {
        float sz  = SH * 0.025f;
        float gap = sz * 1.5f;
        float ox  = SW - 20.f;
        float oy  = 18.f;
        for (int l = 0; l < MAX_LIVES; ++l) {
            Color lc = (l < lives) ? Color{ 255, 255, 255, 210 } : Color{ 50, 50, 70, 100 };
            DrawRectangleRounded({ ox - l * (sz + gap), oy, sz, sz }, 0.35f, 4, lc);
        }
    }

    // Score
    {
        int fs = (int)(SH * 0.08f); if (fs < 16) fs = 16;
        DrawText(TextFormat("%i", score), 20, 16, fs, WHITE);
    }

    // Combo badge
    if (combo >= 3) {
        int fs   = (int)(SH * 0.042f); if (fs < 12) fs = 12;
        Color cc = (combo >= 6) ? PHASE_COL[3] : Color{ 255, 200, 55, 255 };
        DrawText(TextFormat("x%i COMBO", combo), 20, 20 + (int)(SH * 0.09f), fs, cc);
    }

    // Quantum flux label
    if (slowmoTimer > 0.f) {
        int fs = (int)(SH * 0.035f); if (fs < 10) fs = 10;
        float pls = 0.8f + sinf((float)GetTime() * 6.f) * 0.2f;
        Color qc  = PHASE_COL[3]; qc.a = (unsigned char)(pls * 230.f);
        const char* msg = "QUANTUM FLUX";
        int tw = MeasureText(msg, fs);
        DrawText(msg, (SW - tw) / 2, (int)(SH * 0.08f), fs, qc);
    }

    // Score popups
    for (auto& p : popups) {
        if (!p.active) continue;
        int fs = (int)(SH * 0.038f); if (fs < 10) fs = 10;
        Color col = (p.value >= 5) ? PHASE_COL[3] :
                    (p.value >= 3) ? Color{ 255, 200, 55, 255 } : WHITE;
        col.a = (unsigned char)(p.life * 245.f);
        DrawText(TextFormat("+%i", p.value), (int)p.x, (int)p.y, fs, col);
    }

    // Controls hint (fades after a few seconds)
    {
        static float hintLife = 7.f;
        if (!gameOver && score == 0 && hintLife > 0.f) {
            hintLife -= dt;
            int fs = (int)(SH * 0.028f); if (fs < 9) fs = 9;
            unsigned char a = (unsigned char)(std::min(1.f, hintLife) * 160.f);
            Color hc = { 160, 160, 190, a };
            const char* h1 = "LEFT CLICK / SPACE  — next phase";
            const char* h2 = "RIGHT CLICK / SHIFT — prev phase";
            DrawText(h1, (SW - MeasureText(h1, fs)) / 2, (int)(SH * 0.65f), fs, hc);
            DrawText(h2, (SW - MeasureText(h2, fs)) / 2, (int)(SH * 0.65f) + fs + 4, fs, hc);
        }
    }

    // Game Over
    if (gameOver) {
        DrawRectangle(0, 0, SW, SH, { 0, 0, 0, 170 });

        int titleFs = (int)(SH * 0.10f); if (titleFs < 20) titleFs = 20;
        int subFs   = (int)(SH * 0.05f); if (subFs   < 12) subFs   = 12;
        int tinyFs  = (int)(SH * 0.032f);if (tinyFs  < 9)  tinyFs  = 9;

        float pulse = 1.f + sinf((float)GetTime() * 3.5f) * 0.08f;
        Color titleC = { (unsigned char)(255 * pulse), 70, 70, 255 };
        const char* title = "PHASE COLLAPSE";
        DrawText(title, (SW - MeasureText(title, titleFs)) / 2,
                 (int)(SH * 0.28f), titleFs, titleC);

        char scoreBuf[32]; snprintf(scoreBuf, sizeof(scoreBuf), "SCORE:  %i", score);
        DrawText(scoreBuf, (SW - MeasureText(scoreBuf, subFs)) / 2,
                 (int)(SH * 0.46f), subFs, WHITE);

        char comboBuf[32]; snprintf(comboBuf, sizeof(comboBuf), "BEST COMBO:  x%i", maxCombo);
        int smallFs = (int)(subFs * 0.8f);
        DrawText(comboBuf, (SW - MeasureText(comboBuf, smallFs)) / 2,
                 (int)(SH * 0.56f), smallFs, { 200, 200, 220, 200 });

        if ((int)(GetTime() * 2.f) % 2 == 0) {
            const char* restart = "LEFT CLICK  /  SPACE  to restart";
            DrawText(restart, (SW - MeasureText(restart, tinyFs)) / 2,
                     (int)(SH * 0.72f), tinyFs, { 140, 140, 180, 210 });
        }
    }

    EndDrawing();
}

// ─── entry point ─────────────────────────────────────────────────────────────
int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(800, 600, "Phase Pong");
    ResetGame();

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose())
        UpdateDrawFrame();
#endif

    CloseWindow();
    return 0;
}
