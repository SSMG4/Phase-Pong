// =============================================================================
//  Phase Pong — Full Edition v2.0
//  Platforms : Windows · Linux · Android · WebAssembly
//
//  Controls (Desktop)
//    MOUSE X          – move paddle
//    L-CLICK / E      – next phase
//    R-CLICK / Q      – previous phase
//    P / ESC          – pause / back to menu
//
//  Controls (Android / Touch)
//    DRAG             – move paddle
//    TAP left  30 %   – previous phase
//    TAP right 30 %   – next phase
//    TAP middle       – pause
//
//  New features over v1
//    • Mouse/touch-tracked paddle (no longer fixed at center)
//    • Balls bounce off left / right walls
//    • Game states : MENU → PLAYING → PAUSED → GAME-OVER
//    • Difficulty : Easy / Normal / Hard (speed + spawn-rate scaling)
//    • Power-ups  : Shield · TimeWarp · MultiPhase
//    • Boss ball  : large, phase-cycling, needs 3 matched hits
//                   drops a power-up on death
//    • Per-session leaderboard (top 5), persisted to file on Desktop
//    • Phase-unlock notification when BETA / GAMMA / OMEGA appear
//    • On-screen timer bars for active power-ups
//    • Animated main menu with floating background balls
//    • Pause overlay, full game-over screen with scores
// =============================================================================

#include "raylib.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(PLATFORM_WEB)
#   include <emscripten/emscripten.h>
#endif

// =============================================================================
//  CONSTANTS
// =============================================================================
static constexpr int   MAX_BALLS    = 5;
static constexpr int   MAX_PARTS    = 220;
static constexpr int   MAX_POPUPS   = 18;
static constexpr int   MAX_POWERUPS = 4;
static constexpr int   TRAIL_LEN    = 14;
static constexpr int   MAX_LIVES    = 3;
static constexpr int   MAX_PHASES   = 4;
static constexpr int   HS_SLOTS     = 5;

static constexpr float BASE_SPEED   = 270.f;   // px/s at score 0
static constexpr float SPEED_STEP   = 7.5f;    // extra px/s per point
static constexpr float SLOWMO_T     = 0.28f;   // time-warp multiplier
static constexpr float SLOWMO_DUR   = 4.5f;    // seconds
static constexpr float SHAKE_AMP    = 11.f;    // px
static constexpr float SHAKE_DUR    = 0.44f;   // seconds
static constexpr float FLASH_DUR    = 0.22f;

// phase palette
static const Color PHASE_COLOR[MAX_PHASES] = {
    { 90, 155, 255, 255 },   // 0 ALPHA  – electric blue
    { 255,  70,  70, 255 },  // 1 BETA   – hot red
    {  60, 220, 110, 255 },  // 2 GAMMA  – neon green
    { 255, 200,  35, 255 },  // 3 OMEGA  – gold (quantum / boss)
};
static const char* PHASE_NAME[MAX_PHASES] = { "ALPHA","BETA","GAMMA","OMEGA" };

static const Color BG_COLOR = { 6, 6, 16, 255 };

// =============================================================================
//  TYPES
// =============================================================================
enum GameState { S_MENU, S_PLAYING, S_PAUSED, S_GAMEOVER };
enum PUType    { PU_SHIELD, PU_TIMEWARP, PU_MULTIPHASE };
enum Diff      { D_EASY, D_NORMAL, D_HARD };

struct Ball {
    float x, y;
    float vx, vy;
    int   phase;
    int   hp;                      // boss uses 3
    bool  isQuantum;
    bool  isBoss;
    bool  active;
    float trailX[TRAIL_LEN];
    float trailY[TRAIL_LEN];
    int   trailHead;
};

struct Particle {
    float x, y, vx, vy;
    float life, radius;
    Color color;
    bool  active;
};

struct Popup {
    float x, y, life;
    int   value;
    char  text[20];
    bool  active;
};

struct PowerUp {
    float  x, y, vy;
    float  spin;        // visual rotation angle (degrees)
    PUType type;
    bool   active;
};

// =============================================================================
//  GLOBAL STATE
// =============================================================================
static GameState gState     = S_MENU;
static Diff      gDiff      = D_NORMAL;

static Ball      balls[MAX_BALLS];
static Particle  parts[MAX_PARTS];
static Popup     popups[MAX_POPUPS];
static PowerUp   pups[MAX_POWERUPS];

// gameplay
static int   score          = 0;
static int   lives          = MAX_LIVES;
static int   combo          = 0;
static int   maxCombo       = 0;
static int   numPhases      = 2;
static int   paddlePhase    = 0;

// paddle geometry (recalculated per frame for resizable window)
static float padX           = 0.f;
static float padW           = 0.f;
static float padH           = 0.f;
static float padY           = 0.f;

// power-up states
static bool  shieldActive   = false;
static float multiTimer     = 0.f;   // MultiPhase duration remaining
static float slowmoTimer    = 0.f;

// effects
static float shakeTimer     = 0.f;
static float flashTimer     = 0.f;
static Color flashColor     = {};

// spawn
static float spawnTimer     = 0.f;
static int   lastBossScore  = -99;

// boss internal
static float bossPhaseTimer = 0.f;

// high scores
static int   highScores[HS_SLOTS] = {};
static int   hsCount              = 0;

// phase-unlock notification
static int   lastNumPhases        = 2;
static float phaseUnlockTimer     = 0.f;

// hint lifetime
static float hintLife             = 9.f;

// touch
static float touchDownTime        = -1.f;
static float touchDownX           = 0.f;
static int   lastTouchCount       = 0;

// menu
static float menuTime             = 0.f;
static int   menuSel              = 0;   // 0=play, 1=diff, 2=quit

// =============================================================================
//  DIFFICULTY HELPERS
// =============================================================================
static float SpeedMul() {
    return gDiff == D_EASY ? 0.72f : gDiff == D_HARD ? 1.38f : 1.f;
}
static float SpawnMul() {
    return gDiff == D_EASY ? 1.45f : gDiff == D_HARD ? 0.62f : 1.f;
}
static const char* DiffName() {
    return gDiff == D_EASY ? "EASY" : gDiff == D_HARD ? "HARD" : "NORMAL";
}
static Color DiffColor() {
    return gDiff == D_EASY   ? Color{ 70,215,110,255}
         : gDiff == D_HARD   ? Color{255, 70, 70,255}
                             : Color{255,200, 50,255};
}

// =============================================================================
//  EFFECT HELPERS
// =============================================================================
static void TriggerShake(float dur) { shakeTimer = dur; }
static void TriggerFlash(Color c, float dur) { flashColor = c; flashTimer = dur; }

static void SpawnParticles(float x, float y, Color col,
                           int count, float spd, float rad = 3.5f) {
    int n = 0;
    for (int i = 0; i < MAX_PARTS && n < count; ++i) {
        if (parts[i].active) continue;
        float ang        = (float)GetRandomValue(0, 359) * DEG2RAD;
        float s          = spd * (0.4f + GetRandomValue(0,100)*0.006f);
        parts[i] = {
            x, y,
            cosf(ang)*s,
            sinf(ang)*s - GetRandomValue(0,40)*0.05f,
            1.f, rad, col, true
        };
        ++n;
    }
}

static void SpawnPopup(float x, float y, const char* txt, int val) {
    for (int i = 0; i < MAX_POPUPS; ++i) {
        if (popups[i].active) continue;
        popups[i].x = x; popups[i].y = y - 26.f;
        popups[i].life = 1.f; popups[i].value = val; popups[i].active = true;
        snprintf(popups[i].text, sizeof(popups[i].text), "%s", txt);
        return;
    }
}

// =============================================================================
//  SPAWN HELPERS
// =============================================================================
static void SpawnBall() {
    int SW = GetScreenWidth();
    for (int i = 0; i < MAX_BALLS; ++i) {
        if (balls[i].active) continue;
        Ball& b       = balls[i];
        b.active      = true;
        b.isBoss      = false;
        b.isQuantum   = (numPhases >= 3) && (GetRandomValue(0,24) == 0);
        b.phase       = b.isQuantum ? 3 : GetRandomValue(0, numPhases-1);
        float spd     = (BASE_SPEED + score * SPEED_STEP) * SpeedMul();
        b.vy          = spd;
        b.vx          = (float)GetRandomValue(-90, 90);
        b.x           = (float)GetRandomValue((int)(SW*0.1f), (int)(SW*0.9f));
        b.y           = -24.f;
        b.hp          = 1;
        b.trailHead   = 0;
        for (int t = 0; t < TRAIL_LEN; ++t) { b.trailX[t]=b.x; b.trailY[t]=b.y; }
        return;
    }
}

static void SpawnBoss() {
    int SW = GetScreenWidth();
    for (int i = 0; i < MAX_BALLS; ++i) {
        if (balls[i].active) continue;
        Ball& b       = balls[i];
        b.active      = true;
        b.isBoss      = true;
        b.isQuantum   = false;
        b.phase       = GetRandomValue(0, numPhases-1);
        b.vy          = (BASE_SPEED * 0.52f) * SpeedMul();
        b.vx          = (float)GetRandomValue(-50, 50);
        b.x           = (float)(SW / 2);
        b.y           = -52.f;
        b.hp          = 3;
        b.trailHead   = 0;
        bossPhaseTimer= 0.f;
        for (int t = 0; t < TRAIL_LEN; ++t) { b.trailX[t]=b.x; b.trailY[t]=b.y; }
        return;
    }
}

static void SpawnPowerUp(float x, float y) {
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        if (pups[i].active) continue;
        pups[i].active = true;
        pups[i].x      = x;
        pups[i].y      = y;
        pups[i].vy     = 140.f;
        pups[i].spin   = 0.f;
        pups[i].type   = (PUType)GetRandomValue(0,2);
        return;
    }
}

// =============================================================================
//  HIGH SCORES
// =============================================================================
static const char* HS_FILE = "phasepong_hs.dat";

static void LoadHighScores() {
#if !defined(PLATFORM_WEB) && !defined(PLATFORM_ANDROID)
    FILE* f = fopen(HS_FILE, "rb");
    if (!f) return;
    fread(highScores, sizeof(int), HS_SLOTS, f);
    fclose(f);
    hsCount = 0;
    for (int i = 0; i < HS_SLOTS; ++i) if (highScores[i] > 0) ++hsCount;
#endif
}

static void SaveHighScores() {
#if !defined(PLATFORM_WEB) && !defined(PLATFORM_ANDROID)
    FILE* f = fopen(HS_FILE, "wb");
    if (!f) return;
    fwrite(highScores, sizeof(int), HS_SLOTS, f);
    fclose(f);
#endif
}

static bool SubmitScore(int s) {
    // Returns true if it made the leaderboard
    bool madeIt = false;
    for (int i = 0; i < HS_SLOTS; ++i) {
        if (s > highScores[i]) {
            for (int j = HS_SLOTS-1; j > i; --j) highScores[j] = highScores[j-1];
            highScores[i] = s;
            if (hsCount < HS_SLOTS) ++hsCount;
            SaveHighScores();
            madeIt = true;
            break;
        }
    }
    return madeIt;
}

// =============================================================================
//  RESET
// =============================================================================
static void ResetGame() {
    score = 0; lives = MAX_LIVES; combo = maxCombo = 0;
    paddlePhase = 0; numPhases = 2; lastNumPhases = 2; phaseUnlockTimer = 0.f;
    shieldActive = false; multiTimer = 0.f;
    slowmoTimer = shakeTimer = flashTimer = spawnTimer = 0.f;
    lastBossScore = -99; bossPhaseTimer = 0.f;
    hintLife = 9.f;
    lastTouchCount = 0;
    for (auto& b : balls)  b.active = false;
    for (auto& p : parts)  p.active = false;
    for (auto& p : popups) p.active = false;
    for (auto& p : pups)   p.active = false;
    SpawnBall();
    gState = S_PLAYING;
}

// =============================================================================
//  HELPER: centered text
// =============================================================================
static void DrawTextCenter(const char* txt, int y, int fs, Color col) {
    DrawText(txt, (GetScreenWidth() - MeasureText(txt, fs))/2, y, fs, col);
}

// =============================================================================
//  MENU
// =============================================================================
static void UpdateDrawMenu(float dt) {
    int SW = GetScreenWidth(), SH = GetScreenHeight();
    menuTime += dt;

    // --- Input ---
    bool goUp  = IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W);
    bool goDn  = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    bool doSel = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)
              || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
              || (GetTouchPointCount() > 0 && lastTouchCount == 0);
    lastTouchCount = GetTouchPointCount();

    if (goUp)  menuSel = (menuSel + 2) % 3;
    if (goDn)  menuSel = (menuSel + 1) % 3;

    if (doSel) {
        if (menuSel == 0) { ResetGame(); return; }
        if (menuSel == 1) { gDiff = (Diff)((gDiff + 1) % 3); }
        if (menuSel == 2) { CloseWindow(); }
    }

    // --- Draw ---
    BeginDrawing();
    ClearBackground(BG_COLOR);

    // Scanlines
    for (int y = 0; y < SH; y += 5) DrawRectangle(0, y, SW, 1, {0,0,0,28});

    // Animated background phase balls
    for (int i = 0; i < MAX_PHASES; ++i) {
        float t  = menuTime * (0.28f + i * 0.07f);
        float bx = SW * (0.15f + 0.7f * ((sinf(t + i*1.4f)+1.f)*0.5f));
        float by = SH * (0.12f + 0.72f * ((cosf(t*0.85f + i*1.1f)+1.f)*0.5f));
        Color c  = PHASE_COLOR[i];
        c.a      = 50; DrawCircleV({bx,by}, SW*0.075f, c);
        c.a      = 20; DrawCircleV({bx,by}, SW*0.13f,  c);
    }

    // Title
    int tFs = std::max(28, SH/7);
    {
        float pulse  = 1.f + sinf(menuTime*2.2f)*0.04f;
        int   tFsPul = (int)(tFs * pulse);
        Color glowC  = PHASE_COLOR[0]; glowC.a = 55;
        DrawTextCenter("PHASE  PONG", SH/6, tFsPul, glowC);
        DrawTextCenter("PHASE  PONG", SH/6, tFsPul, WHITE);
    }

    // Sub-title
    {
        int sf = std::max(9, tFs/4);
        DrawTextCenter("catch the right phase * don't miss a single one",
                       SH/6 + tFs + 8, sf, {130,130,175,165});
    }

    // Menu rows: PLAY | DIFFICULTY | QUIT
    const char* LABELS[3] = { "PLAY", nullptr, "QUIT" };
    int mFs     = std::max(16, SH/11);
    float startY = SH * 0.48f;
    float rowH   = mFs * 1.85f;

    for (int i = 0; i < 3; ++i) {
        float iy  = startY + i * rowH;
        bool  sel = (i == menuSel);
        float pul = sel ? (1.f + sinf(menuTime*4.2f)*0.055f) : 1.f;
        int   ifs = (int)(mFs * pul);

        char  buf[48];
        const char* lbl;
        Color lc;

        if (i == 1) {
            snprintf(buf, sizeof(buf), "DIFFICULTY :  %s", DiffName());
            lbl = buf;
            lc  = sel ? DiffColor() : Color{90,90,125,175};
        } else {
            lbl = LABELS[i];
            lc  = sel ? WHITE : Color{90,90,125,175};
        }

        DrawTextCenter(lbl, (int)iy, ifs, lc);

        // Selection arrow
        if (sel) {
            int aw = MeasureText(lbl, ifs);
            int ax = (SW - aw)/2 - ifs - 10;
            DrawText(">", ax, (int)iy, ifs, {255,195,40,200});
        }
    }

    // Leaderboard
    if (hsCount > 0) {
        int hfs = std::max(9, SH/22);
        int hx  = 22;
        int hy  = SH - hfs*(hsCount+3);
        DrawText("BEST SCORES", hx, hy, hfs, {95,95,140,155});
        for (int i = 0; i < hsCount; ++i) {
            char buf[24]; snprintf(buf,sizeof(buf),"#%d  %d", i+1, highScores[i]);
            DrawText(buf, hx, hy + (i+1)*hfs*14/10, hfs, {120,120,165,195});
        }
    }

    // Nav hint
    {
        int hf = std::max(8, SH/30);
        DrawTextCenter("W/S  UP/DN  to navigate     SPACE/ENTER  to select",
                       SH - hf*2, hf, {70,70,105,140});
    }

    EndDrawing();
}

// =============================================================================
//  GAME UPDATE
// =============================================================================
static void UpdateGame(float dt) {
    int SW = GetScreenWidth(), SH = GetScreenHeight();

    float gamedt = (slowmoTimer > 0.f) ? dt * SLOWMO_T : dt;

    // Layout
    float ballR = SW * 0.034f;
    padW  = SW * 0.24f;
    padH  = SH * 0.021f;
    padY  = SH * 0.872f;
    float margin = SW * 0.018f;

    // Paddle follows mouse X (or touch X)
    {
        float target = (float)GetMouseX() - padW * 0.5f;
        int tc = GetTouchPointCount();
        if (tc > 0) {
            Vector2 tp = GetTouchPosition(0);
            target = tp.x - padW * 0.5f;
        }
        target = std::max(margin, std::min((float)SW - padW - margin, target));
        // Smooth follow — tight enough to feel immediate, smooth enough to not jitter
        padX += (target - padX) * std::min(1.f, dt * 30.f);
    }

    float catchTop = padY - ballR * 0.45f;

    // Phase progression
    numPhases = (score >= 20) ? 4 : (score >= 10) ? 3 : 2;
    if (numPhases > lastNumPhases) {
        phaseUnlockTimer = 3.5f;
        lastNumPhases    = numPhases;
    }
    if (paddlePhase >= numPhases) paddlePhase = numPhases - 1;

    // Phase input — keyboard / mouse
    bool fwd = IsKeyPressed(KEY_E) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
            || IsKeyPressed(KEY_SPACE);
    bool bwd = IsKeyPressed(KEY_Q) || IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)
            || IsKeyPressed(KEY_LEFT_SHIFT);

    // Touch tap detection (tap left 30% = prev, tap right 30% = next, mid = pause)
    {
        int curTC = GetTouchPointCount();
        if (curTC > 0 && lastTouchCount == 0) {
            touchDownTime = (float)GetTime();
            touchDownX    = GetTouchPosition(0).x;
        }
        if (curTC == 0 && lastTouchCount > 0) {
            float dur = (float)GetTime() - touchDownTime;
            if (dur < 0.18f) {
                float rx = touchDownX / (float)SW;
                if      (rx < 0.30f) bwd = true;
                else if (rx > 0.70f) fwd = true;
                else  { gState = S_PAUSED; lastTouchCount = curTC; return; }
            }
        }
        lastTouchCount = curTC;
    }

    if (fwd) paddlePhase = (paddlePhase + 1)            % numPhases;
    if (bwd) paddlePhase = (paddlePhase + numPhases - 1)% numPhases;

    // Pause
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
        gState = S_PAUSED;
        return;
    }

    // Tick timers
    slowmoTimer      -= dt; if (slowmoTimer      < 0.f) slowmoTimer      = 0.f;
    shakeTimer       -= dt; if (shakeTimer        < 0.f) shakeTimer       = 0.f;
    flashTimer       -= dt; if (flashTimer        < 0.f) flashTimer       = 0.f;
    multiTimer       -= dt; if (multiTimer        < 0.f) multiTimer       = 0.f;
    phaseUnlockTimer -= dt; if (phaseUnlockTimer  < 0.f) phaseUnlockTimer = 0.f;
    hintLife         -= dt; if (hintLife          < 0.f) hintLife         = 0.f;

    // Ball spawn
    spawnTimer -= gamedt;
    if (spawnTimer <= 0.f) {
        int active = 0;
        for (auto& b : balls) if (b.active) ++active;
        int cap = std::min(1 + score / 7, MAX_BALLS);
        if (active < cap) SpawnBall();
        spawnTimer = std::max(0.38f, 1.85f - score * 0.034f) * SpawnMul();
    }

    // Boss spawn (every 20 pts after score > 15, no cooldown overlap)
    bool bossAlive = false;
    for (auto& b : balls) if (b.active && b.isBoss) { bossAlive = true; break; }
    if (!bossAlive && score > 15 && score - lastBossScore >= 20) {
        SpawnBoss();
        lastBossScore = score;
    }

    // Boss phase cycling
    bossPhaseTimer += gamedt;
    if (bossPhaseTimer >= 2.0f) {
        bossPhaseTimer = 0.f;
        for (auto& b : balls)
            if (b.active && b.isBoss) b.phase = (b.phase + 1) % numPhases;
    }

    // ── Update balls ──────────────────────────────────────────────────────
    for (auto& b : balls) {
        if (!b.active) continue;

        // Trail
        b.trailX[b.trailHead] = b.x;
        b.trailY[b.trailHead] = b.y;
        b.trailHead = (b.trailHead + 1) % TRAIL_LEN;

        float rad = b.isBoss ? ballR * 2.3f : ballR;

        b.x += b.vx * gamedt;
        b.y += b.vy * gamedt;

        // Wall bounce
        if (b.x - rad < 0.f)        { b.x = rad;        b.vx =  fabsf(b.vx); }
        if (b.x + rad > (float)SW)  { b.x = SW - rad;   b.vx = -fabsf(b.vx); }

        // Paddle collision window
        bool onPad = (b.y + rad >= catchTop)
                  && (b.y - rad <= padY + padH + 6.f)
                  && (b.x + rad >= padX)
                  && (b.x - rad <= padX + padW);

        if (onPad) {
            bool matched = b.isQuantum || (b.phase == paddlePhase) || (multiTimer > 0.f);

            if (matched) {
                if (b.isBoss) {
                    // Damage boss
                    --b.hp;
                    SpawnParticles(b.x, b.y, PHASE_COLOR[b.phase], 16, 200.f, 5.f);
                    if (b.hp > 0) {
                        // Bounce back up, speed up a bit
                        b.vy = -fabsf(b.vy) * 0.85f;
                        b.vx = (float)GetRandomValue(-130, 130);
                        TriggerFlash({255,200,40,60}, 0.12f);
                        continue;   // don't kill yet
                    }
                    // Boss dead
                    ++combo; if (combo > maxCombo) maxCombo = combo;
                    int pts = 10 + (combo >= 6 ? 30 : combo >= 3 ? 15 : 0);
                    score += pts;
                    char ptxt[20]; snprintf(ptxt, sizeof(ptxt), "BOSS! +%d", pts);
                    SpawnPopup(b.x, padY, ptxt, pts);
                    SpawnParticles(b.x, padY, PHASE_COLOR[3], 55, 310.f, 6.f);
                    SpawnPowerUp(b.x, b.y);
                    TriggerShake(SHAKE_DUR * 0.7f);
                    TriggerFlash(PHASE_COLOR[3], 0.28f);
                } else {
                    // Normal catch
                    ++combo; if (combo > maxCombo) maxCombo = combo;
                    int pts = b.isQuantum ? 5 : 1;
                    if      (combo >= 6) pts *= 4;
                    else if (combo >= 3) pts *= 2;
                    score += pts;

                    char ptxt[20];
                    if      (pts >= 20) snprintf(ptxt,sizeof(ptxt),"INSANE! +%d",pts);
                    else if (pts >= 10) snprintf(ptxt,sizeof(ptxt),"MEGA +%d",pts);
                    else if (pts >= 4)  snprintf(ptxt,sizeof(ptxt),"x%d +%d", combo>=6?4:2, pts);
                    else if (pts == 5)  snprintf(ptxt,sizeof(ptxt),"QUANTUM +%d",pts);
                    else                snprintf(ptxt,sizeof(ptxt),"+%d",pts);
                    SpawnPopup(b.x, padY, ptxt, pts);

                    SpawnParticles(b.x, padY, PHASE_COLOR[b.phase], 22, 230.f, 4.f);

                    if (b.isQuantum) {
                        slowmoTimer = SLOWMO_DUR;
                        TriggerFlash({PHASE_COLOR[3].r,PHASE_COLOR[3].g,PHASE_COLOR[3].b,85}, FLASH_DUR);
                    }
                }
            } else {
                // Wrong phase — lose a life (or shield)
                combo = 0;
                if (shieldActive) {
                    shieldActive = false;
                    SpawnParticles(b.x, padY, {70,195,255,255}, 22, 220.f, 5.f);
                    SpawnPopup(b.x, padY, "SHIELDED!", 0);
                    TriggerFlash({70,195,255,75}, FLASH_DUR);
                } else {
                    --lives;
                    SpawnParticles(b.x, padY, PHASE_COLOR[b.phase], 32, 330.f, 5.f);
                    TriggerShake(SHAKE_DUR);
                    TriggerFlash({210,20,20,85}, FLASH_DUR);
                    if (lives <= 0) {
                        SubmitScore(score);
                        gState = S_GAMEOVER;
                        return;
                    }
                }
            }
            b.active = false;
            continue;
        }

        // Ball fell past the bottom
        if (b.y > (float)SH + rad * 2.f) {
            combo = 0;
            if (shieldActive) {
                shieldActive = false;
                SpawnParticles(b.x, (float)SH - 28.f, {70,195,255,255}, 18, 190.f);
                SpawnPopup(b.x, (float)SH*0.85f, "SHIELDED!", 0);
                TriggerFlash({70,195,255,65}, FLASH_DUR);
            } else {
                --lives;
                SpawnParticles(b.x, (float)SH - 28.f, {90,90,110,200}, 14, 190.f);
                TriggerShake(SHAKE_DUR * 0.55f);
                if (lives <= 0) {
                    SubmitScore(score);
                    gState = S_GAMEOVER;
                    return;
                }
            }
            b.active = false;
        }
    }

    // ── Update power-ups ──────────────────────────────────────────────────
    for (auto& pu : pups) {
        if (!pu.active) continue;
        pu.y    += pu.vy * gamedt;
        pu.spin += gamedt * 95.f;

        float puR = ballR * 0.88f;
        bool  onPad = (pu.y + puR >= padY) && (pu.y - puR <= padY + padH)
                   && (pu.x + puR >= padX) && (pu.x - puR <= padX + padW);
        if (onPad) {
            pu.active = false;
            Color puCol;
            const char* puTxt;
            if (pu.type == PU_SHIELD) {
                shieldActive = true;
                puCol = {70,195,255,255}; puTxt = "SHIELD!";
            } else if (pu.type == PU_TIMEWARP) {
                slowmoTimer  = SLOWMO_DUR * 1.6f;
                puCol = {175,75,255,255}; puTxt = "TIMEWARP!";
            } else {
                multiTimer   = 6.5f;
                puCol = {255,255,165,255}; puTxt = "MULTIPHASE!";
            }
            SpawnParticles(pu.x, padY, puCol, 30, 265.f, 5.f);
            SpawnPopup(pu.x, padY - 22.f, puTxt, 0);
            TriggerFlash(puCol, 0.26f);
        }
        if (pu.y > (float)SH + puR * 2.f) pu.active = false;
    }

    // ── Update particles ──────────────────────────────────────────────────
    for (auto& p : parts) {
        if (!p.active) continue;
        p.x    += p.vx * gamedt;
        p.y    += p.vy * gamedt;
        p.vy   += 390.f * gamedt;
        p.life -= gamedt * 1.75f;
        if (p.life <= 0.f) p.active = false;
    }

    // ── Update popups ─────────────────────────────────────────────────────
    for (auto& p : popups) {
        if (!p.active) continue;
        p.y    -= 78.f * dt;
        p.life -= dt * 1.15f;
        if (p.life <= 0.f) p.active = false;
    }
}

// =============================================================================
//  GAME DRAW
// =============================================================================
static void DrawGame(float dt) {
    int SW  = GetScreenWidth();
    int SH  = GetScreenHeight();
    float ballR = SW * 0.034f;
    float T     = (float)GetTime();

    // Screen shake offset
    float sx = 0.f, sy = 0.f;
    if (shakeTimer > 0.f) {
        float f = shakeTimer / SHAKE_DUR;
        sx = (float)GetRandomValue(-1,1) * SHAKE_AMP * f;
        sy = (float)GetRandomValue(-1,1) * SHAKE_AMP * f;
    }

    BeginDrawing();
    ClearBackground(BG_COLOR);

    // Scanlines
    for (int y = 0; y < SH; y += 5) DrawRectangle(0, y, SW, 1, {0,0,0,26});

    // Subtle grid
    for (int x = 0; x < SW; x += SW/10)
        DrawLine(x, 0, x, SH, {255,255,255,5});
    for (int y = 0; y < SH; y += SH/8)
        DrawLine(0, y, SW, y, {255,255,255,5});

    // Flash overlay
    if (flashTimer > 0.f) {
        Color fc   = flashColor;
        float frac = std::min(1.f, flashTimer / FLASH_DUR);
        fc.a       = (unsigned char)(fc.a * frac);
        DrawRectangle(0, 0, SW, SH, fc);
    }

    // Quantum / time-warp vignette border
    if (slowmoTimer > 0.f) {
        float a = std::min(1.f, slowmoTimer / 0.7f) * 55.f;
        DrawRectangleLinesEx({0,0,(float)SW,(float)SH}, 18.f,
            {PHASE_COLOR[3].r,PHASE_COLOR[3].g,PHASE_COLOR[3].b,(unsigned char)a});
    }

    // MultiPhase vignette border
    if (multiTimer > 0.f) {
        float a = std::min(1.f, multiTimer / 0.7f) * 38.f;
        DrawRectangleLinesEx({0,0,(float)SW,(float)SH}, 10.f, {255,255,160,(unsigned char)a});
    }

    // ── Particles ─────────────────────────────────────────────────────────
    for (auto& p : parts) {
        if (!p.active) continue;
        Color c = p.color; c.a = (unsigned char)(p.life * 205.f);
        DrawCircleV({p.x + sx, p.y + sy}, p.radius * p.life, c);
    }

    // ── Power-ups (spinning diamond) ───────────────────────────────────────
    for (auto& pu : pups) {
        if (!pu.active) continue;
        float r = ballR * 0.88f;
        Color puC;
        const char* puL;
        if      (pu.type == PU_SHIELD)   { puC = {70,195,255,255};  puL = "S"; }
        else if (pu.type == PU_TIMEWARP) { puC = {175,75,255,255};  puL = "T"; }
        else                             { puC = {255,255,160,255};  puL = "M"; }

        // Glow
        Color gc = puC; gc.a = 38;
        DrawCircleV({pu.x+sx, pu.y+sy}, r * 1.8f, gc);

        // Spinning quad (4-point star outline using 4 triangles)
        float a = pu.spin * DEG2RAD;
        float outer = r, inner = r * 0.45f;
        for (int k = 0; k < 4; ++k) {
            float a0 = a + k * (PI / 2.f);
            float a1 = a0 + (PI / 4.f);
            float a2 = a0 + (PI / 2.f);
            Vector2 p0 = {pu.x + sx + cosf(a0)*outer, pu.y + sy + sinf(a0)*outer};
            Vector2 p1 = {pu.x + sx + cosf(a1)*inner, pu.y + sy + sinf(a1)*inner};
            Vector2 p2 = {pu.x + sx + cosf(a2)*outer, pu.y + sy + sinf(a2)*outer};
            Vector2 cn = {pu.x + sx, pu.y + sy};
            DrawTriangle(p0, cn, p1, puC);
            DrawTriangle(p1, cn, p2, puC);
        }

        // Letter
        int lfs = std::max(7, (int)(r * 0.9f));
        int lw  = MeasureText(puL, lfs);
        DrawText(puL, (int)(pu.x+sx - lw*0.5f), (int)(pu.y+sy - lfs*0.5f), lfs, {0,0,0,210});
    }

    // ── Balls + trails ─────────────────────────────────────────────────────
    for (auto& b : balls) {
        if (!b.active) continue;
        Color bc  = PHASE_COLOR[b.phase];
        float rad = b.isBoss ? ballR * 2.3f : ballR;

        // Trail
        for (int k = 1; k < TRAIL_LEN; ++k) {
            int   idx  = (b.trailHead - k - 1 + TRAIL_LEN) % TRAIL_LEN;
            float frac = (float)(TRAIL_LEN - k) / TRAIL_LEN;
            float tr   = rad * frac * 0.68f;
            Color tc   = bc; tc.a = (unsigned char)(frac*frac * 115.f);
            DrawCircleV({b.trailX[idx]+sx, b.trailY[idx]+sy}, tr, tc);
        }

        // Glow rings
        Color glow = bc;
        glow.a = 42; DrawCircleV({b.x+sx,b.y+sy}, rad*2.05f, glow);
        glow.a = 78; DrawCircleV({b.x+sx,b.y+sy}, rad*1.42f, glow);

        // Body with pulse
        float pulse = b.isQuantum ? (1.f + sinf(T*9.5f)*0.14f)
                    : b.isBoss    ? (1.f + sinf(T*4.2f)*0.07f)
                    :               1.f;
        DrawCircleV({b.x+sx, b.y+sy}, rad * pulse, bc);

        // Boss HP pips
        if (b.isBoss) {
            float dotR = rad * 0.17f;
            float dx   = b.x + sx - dotR * 2.5f;
            float dy   = b.y + sy - rad - dotR * 2.6f;
            for (int h = 0; h < 3; ++h) {
                Color dc = (h < b.hp) ? WHITE : Color{50,50,70,170};
                DrawCircleV({dx + h * dotR * 2.5f, dy}, dotR, dc);
            }
        }

        // Label
        int lfs = std::max(8, (int)(rad * 0.82f));
        const char* lbl = b.isQuantum ? "Q"
                        : b.isBoss    ? "B"
                        : (b.phase==0?"A":b.phase==1?"B":b.phase==2?"G":"O");
        int tw = MeasureText(lbl, lfs);
        DrawText(lbl, (int)(b.x+sx - tw*0.5f), (int)(b.y+sy - lfs*0.5f), lfs, {0,0,0,195});
    }

    // ── Paddle ─────────────────────────────────────────────────────────────
    {
        Color pc = PHASE_COLOR[paddlePhase];
        if (multiTimer > 0.f) {
            // Cycle through phase colours
            pc = PHASE_COLOR[(int)(T * 6.5f) % numPhases];
        }

        // Shield aura
        if (shieldActive) {
            Color sg = {70,195,255,55};
            DrawRectangle((int)(padX-14+sx),(int)(padY-8+sy),(int)(padW+28),(int)(padH+16), sg);
            DrawRectangleLinesEx({padX-11.f+sx,padY-5.f+sy,padW+22,padH+10}, 2.5f,{70,195,255,145});
        }

        // Outer glow
        Color g = pc; g.a = 30;
        DrawRectangle((int)(padX-8+sx),(int)(padY-5+sy),(int)(padW+16),(int)(padH+10), g);

        // Body
        DrawRectangleRounded({padX+sx, padY+sy, padW, padH}, 0.55f, 8, pc);

        // Highlight streak
        DrawRectangle((int)(padX + padW*0.22f + sx), (int)(padY + padH*0.14f + sy),
                      (int)(padW * 0.56f), (int)(padH * 0.34f), {255,255,255,52});
    }

    // ── HUD — Score (top-left) ─────────────────────────────────────────────
    {
        int fs = std::max(16, (int)(SH * 0.078f));
        DrawText(TextFormat("%d", score), 20, 16, fs, WHITE);
    }

    // ── HUD — Lives (top-right, coloured squares) ──────────────────────────
    {
        float sz  = SH * 0.023f;
        float gap = sz * 1.35f;
        for (int l = 0; l < MAX_LIVES; ++l) {
            bool  alive = (l < lives);
            Color lc   = alive ? Color{245,70,70,215} : Color{45,45,65,80};
            DrawRectangleRounded({ (float)SW - 20.f - l*(sz+gap), 18.f, sz, sz },
                                 0.42f, 4, lc);
        }
    }

    // ── HUD — Combo badge ──────────────────────────────────────────────────
    if (combo >= 3) {
        int fs  = std::max(12, (int)(SH*0.038f));
        Color cc = (combo >= 6) ? PHASE_COLOR[3] : Color{255,195,45,255};
        DrawText(TextFormat("x%d COMBO", combo), 20, 16 + (int)(SH*0.085f), fs, cc);
    }

    // ── HUD — Power-up timer bars (top-right, below lives) ─────────────────
    {
        float barW = SW * 0.13f;
        float barH = SH * 0.013f;
        float barX = (float)SW - barW - 22.f;
        float barY = 18.f + SH * 0.048f;
        float lineH = barH + 8.f;
        int   bfs   = std::max(8, (int)(barH * 1.4f));

        if (slowmoTimer > 0.f) {
            float frac = std::min(1.f, slowmoTimer / SLOWMO_DUR);
            DrawRectangle((int)barX,(int)barY,(int)barW,(int)barH,{25,25,45,185});
            DrawRectangle((int)barX,(int)barY,(int)(barW*frac),(int)barH,PHASE_COLOR[3]);
            int tw = MeasureText("TIMEWARP", bfs);
            DrawText("TIMEWARP", (int)(barX - tw - 5), (int)barY, bfs, PHASE_COLOR[3]);
            barY += lineH;
        }
        if (multiTimer > 0.f) {
            float frac = std::min(1.f, multiTimer / 6.5f);
            DrawRectangle((int)barX,(int)barY,(int)barW,(int)barH,{25,25,45,185});
            Color mc = {255,255,155,255};
            DrawRectangle((int)barX,(int)barY,(int)(barW*frac),(int)barH,mc);
            int tw = MeasureText("MULTI", bfs);
            DrawText("MULTI", (int)(barX - tw - 5), (int)barY, bfs, mc);
        }
    }

    // ── HUD — Phase selector (bottom-left) ─────────────────────────────────
    {
        float dotR   = ballR * 0.50f;
        float rowY   = SH * 0.932f;
        float startX = SW * 0.055f;
        float step   = std::max(SW * 0.088f, dotR * 2.9f);
        for (int p = 0; p < numPhases; ++p) {
            float cx  = startX + p * step;
            bool  sel = (p == paddlePhase);
            Color col = PHASE_COLOR[p]; col.a = sel ? 255 : 60;
            DrawCircleV({cx, rowY}, dotR, col);
            if (sel) DrawCircleLinesV({cx, rowY}, dotR * 1.55f, {255,255,255,155});
            int fz = std::max(7, (int)(dotR * 0.82f));
            DrawText(PHASE_NAME[p],
                     (int)(cx - MeasureText(PHASE_NAME[p],fz)*0.5f),
                     (int)(rowY + dotR + 3.f),
                     fz, sel ? WHITE : Color{95,95,130,175});
        }
    }

    // ── HUD — Quantum Flux label ───────────────────────────────────────────
    if (slowmoTimer > 0.f) {
        int fs = std::max(10, (int)(SH*0.032f));
        float p = 0.78f + sinf(T*6.5f)*0.22f;
        Color qc = PHASE_COLOR[3]; qc.a = (unsigned char)(p * 225.f);
        DrawTextCenter("** QUANTUM FLUX **", (int)(SH*0.078f), fs, qc);
    }

    // ── HUD — Phase-unlock notification ───────────────────────────────────
    if (phaseUnlockTimer > 0.f && lastNumPhases > 0) {
        int fs = std::max(11, (int)(SH*0.036f));
        float a  = std::min(1.f, phaseUnlockTimer / 0.5f);
        Color uc = PHASE_COLOR[lastNumPhases-1]; uc.a = (unsigned char)(a * 235.f);
        char buf[48];
        snprintf(buf, sizeof(buf), "PHASE  %s  UNLOCKED!", PHASE_NAME[lastNumPhases-1]);
        DrawTextCenter(buf, (int)(SH*0.148f), fs, uc);
    }

    // ── HUD — Score popups ─────────────────────────────────────────────────
    for (auto& p : popups) {
        if (!p.active) continue;
        int fs = std::max(10, (int)(SH*0.034f));
        Color col = (p.value >= 10) ? PHASE_COLOR[3]
                  : (p.value >= 4)  ? Color{255,195,45,255}
                  : (p.value == 0)  ? Color{70,195,255,255}
                                    : WHITE;
        col.a = (unsigned char)(p.life * 240.f);
        DrawText(p.text, (int)p.x, (int)p.y, fs, col);
    }

    // ── HUD — Controls hint (fades out) ───────────────────────────────────
    if (hintLife > 0.f && score == 0) {
        int   fs = std::max(9, (int)(SH*0.024f));
        unsigned char a = (unsigned char)(std::min(1.f, hintLife * 0.5f) * 145.f);
        Color hc = {145,145,188,a};
        DrawTextCenter("MOUSE / TOUCH  to move paddle",
                       (int)(SH*0.62f),         fs, hc);
        DrawTextCenter("L-CLICK / E  next phase   |   R-CLICK / Q  prev phase",
                       (int)(SH*0.62f)+fs+4,    fs, hc);
        DrawTextCenter("P  pause",
                       (int)(SH*0.62f)+fs*2+8,  fs, hc);
    }

    EndDrawing();
}

// =============================================================================
//  PAUSE OVERLAY
// =============================================================================
static void DrawPauseOverlay() {
    int SW = GetScreenWidth(), SH = GetScreenHeight();
    DrawRectangle(0, 0, SW, SH, {0,0,0,145});
    int fs = std::max(20, SH/8);
    DrawTextCenter("PAUSED", SH/2 - fs, fs, WHITE);
    int sf = fs/3;
    DrawTextCenter("P  or  ESC  to resume   |   MENU  key for main menu",
                   SH/2 + sf/2, sf, {150,150,195,195});
}

// =============================================================================
//  GAME OVER
// =============================================================================
static void UpdateDrawGameOver(float dt) {
    int SW = GetScreenWidth(), SH = GetScreenHeight();
    float T = (float)GetTime();

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_SPACE)
     || IsKeyPressed(KEY_ENTER)
     || (GetTouchPointCount() > 0 && lastTouchCount == 0)) {
        lastTouchCount = GetTouchPointCount();
        ResetGame(); return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) { gState = S_MENU; return; }
    lastTouchCount = GetTouchPointCount();

    BeginDrawing();
    ClearBackground(BG_COLOR);
    for (int y = 0; y < SH; y += 5) DrawRectangle(0, y, SW, 1, {0,0,0,26});
    DrawRectangle(0, 0, SW, SH, {0,0,0,155});

    // Title
    {
        int fs   = std::max(20, SH/8);
        float p  = 1.f + sinf(T*3.6f)*0.065f;
        Color tc = { (unsigned char)(255*p), 55, 55, 255 };
        DrawTextCenter("PHASE  COLLAPSE", (int)(SH*0.20f), (int)(fs*p), tc);
    }

    // Score + best combo
    {
        int sf = std::max(12, SH/14);
        char buf[32]; snprintf(buf,sizeof(buf),"SCORE:  %d", score);
        DrawTextCenter(buf, (int)(SH*0.37f), sf, WHITE);

        int sf2 = sf * 3/4;
        char cb[32]; snprintf(cb,sizeof(cb),"BEST COMBO:  x%d", maxCombo);
        DrawTextCenter(cb, (int)(SH*0.47f), sf2, {185,185,215,195});

        char db[32]; snprintf(db,sizeof(db),"DIFFICULTY:  %s", DiffName());
        DrawTextCenter(db, (int)(SH*0.47f) + sf2*14/10, sf2, DiffColor());
    }

    // Leaderboard
    if (hsCount > 0) {
        int hfs = std::max(10, SH/20);
        int hy  = (int)(SH * 0.595f);
        DrawTextCenter("- LEADERBOARD -", hy, hfs, {130,130,175,175});
        for (int i = 0; i < hsCount; ++i) {
            bool isThis = (highScores[i] == score && score > 0);
            Color hc = isThis ? PHASE_COLOR[3] : Color{170,170,205,195};
            char buf[32];
            snprintf(buf,sizeof(buf),"#%d    %d%s", i+1, highScores[i], isThis?" <":"");
            DrawTextCenter(buf, hy + (int)((i+1.5f)*hfs*1.38f), hfs, hc);
        }
    }

    // Restart prompt (blink)
    if ((int)(T*2.1f) % 2 == 0) {
        int tf = std::max(9, SH/28);
        DrawTextCenter("SPACE / CLICK  to restart   |   ESC  for menu",
                       (int)(SH*0.89f), tf, {125,125,175,195});
    }

    EndDrawing();
}

// =============================================================================
//  MASTER LOOP
// =============================================================================
void UpdateDrawFrame() {
    float dt = GetFrameTime();
    if (dt > 0.06f) dt = 0.06f;   // clamp for physics stability

    switch (gState) {
        case S_MENU:
            UpdateDrawMenu(dt);
            break;

        case S_PLAYING:
            UpdateGame(dt);
            if (gState == S_PLAYING)   // state might have changed inside Update
                DrawGame(dt);
            break;

        case S_PAUSED:
            // Re-draw the game behind the overlay (no update)
            DrawGame(dt);
            DrawPauseOverlay();
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE))
                gState = S_PLAYING;
            if (IsKeyPressed(KEY_M))
                gState = S_MENU;
            break;

        case S_GAMEOVER:
            UpdateDrawGameOver(dt);
            break;
    }
}

// =============================================================================
//  ENTRY POINT
// =============================================================================
int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(800, 600, "Phase Pong");
    SetWindowMinSize(360, 480);

    LoadHighScores();

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
