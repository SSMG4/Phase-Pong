#include "raylib.h"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

float ballY = 0.0f;
int ballPhase = 0;
int paddlePhase = 0;
int score = 0;
bool gameOver = false;

void UpdateDrawFrame() {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    if (!gameOver) {
        ballY += 7.0f + (score * 0.5f);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_SPACE)) {
            paddlePhase = 1 - paddlePhase;
        }

        if (ballY >= screenHeight - (screenHeight * 0.1f)) {
            if (ballPhase == paddlePhase) {
                score++;
                ballY = 0.0f;
                ballPhase = GetRandomValue(0, 1);
            } else {
                gameOver = true;
            }
        }
    } else {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_SPACE)) {
            gameOver = false;
            score = 0;
            ballY = 0.0f;
            ballPhase = GetRandomValue(0, 1);
        }
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    Color ballColor = (ballPhase == 0) ? BLUE : RED;
    Color paddleColor = (paddlePhase == 0) ? BLUE : RED;

    DrawCircle(screenWidth / 2, static_cast<int>(ballY), screenWidth * 0.05f, ballColor);
    DrawRectangle(screenWidth * 0.2f, screenHeight - (screenHeight * 0.1f), screenWidth * 0.6f, screenHeight * 0.03f, paddleColor);
    
    DrawText(TextFormat("SCORE: %i", score), 20, 20, screenHeight * 0.05f, DARKGRAY);

    if (gameOver) {
        int fontSize = screenHeight * 0.08f;
        int textWidth = MeasureText("SYSTEM FAILURE", fontSize);
        DrawText("SYSTEM FAILURE", (screenWidth / 2) - (textWidth / 2), screenHeight / 2, fontSize, BLACK);
    }

    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(800, 600, "Phase Pong");

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
#endif

    CloseWindow();
    return 0;
}