#include <stdio.h>
#include "raylib.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif // PLATFORM_WEB

const int screenWidth = 800;
const int screenHeight = 600;

Vector2* curvePts = nullptr;
Vector2 focus = {400, 300};

float GetArcYForXCoord(Vector2 focus, float x, float directrixY)
{
    // NOTE: In the interest of keeping the formula simple when moving away from the origin,
    //       we'll use the substitution from (x,y) -> (w,y) = (x-focusX,y).
    //       In particular this substitution means that the formula always has the form:
    //       y = aw^2 + c, the linear term's coefficient is always 0.
    float a = 1.0f/(4.0f*(focus.y - directrixY));
    float c = (focus.y + directrixY)*0.5f;

    float w = x - focus.x;
    return a*w*w + c;
}

void DrawParabola(Vector2 focus, float directrixY, Color color)
{
    float x = 0.0f;
    for(int i=0; i<screenWidth; i++)
    {
        // Change of variables from (x,y) to (w,y) for simplicity of expression
        curvePts[i] = {x, GetArcYForXCoord(focus, x, directrixY)};

        // Y increases downwards in screencoords, so flip each point around the x-axis.
        // This is just so that it is closer to what we usually get/expect in mathematics.
        curvePts[i].y = screenHeight - curvePts[i].y;
        x += 1.0f;
    }

    for(int i=1; i<screenWidth; i++)
    {
        DrawLineV(curvePts[i-1], curvePts[i], color);
    }
}

#if PLATFORM_WEB
int UpdatesTillInitComplete = 2;
#endif // PLATFORM_WEB
void update()
{
#if PLATFORM_WEB
#define TO_STR(x) #x
#define EXPAND_DEFINE_TO_STRING(x) TO_STR(x)
    if(UpdatesTillInitComplete > 0)
    {
        UpdatesTillInitComplete--;
        if(UpdatesTillInitComplete == 0)
        {
            const char* initializedModuleName = EXPAND_DEFINE_TO_STRING(MODULE_NAME);
            EM_ASM_({emscriptenInitializationComplete(Pointer_stringify($0));}, initializedModuleName);
        }
    }
#undef TO_STR
#undef EXPAND_DEFINE_TO_STRING
#endif // PLATFORM_WEB

    float directrixY = screenHeight - (float)GetMouseY();
    if(IsMouseButtonDown(0))
    {
        focus.x = (float)GetMouseX();
        focus.y = directrixY;
    }

    BeginDrawing();
    DrawText("Left click to move the focus", 0, 0, 24, WHITE);
    DrawLine(0, screenHeight-(int)directrixY, screenWidth, screenHeight-(int)directrixY, WHITE);

    Vector2 size = {8, 8};
    Vector2 position = {focus.x-size.x/2.0f, screenHeight-focus.y-size.y/2.0f};
    DrawRectangleV(position, size, RED);

    DrawParabola(focus, directrixY, GREEN);
    EndDrawing();
}

int main()
{
    char windowTitle[] = "Focus-directrix parabola demo";
    curvePts = new Vector2[screenWidth];
    InitWindow(screenWidth, screenHeight, windowTitle);
    ClearBackground(BLACK);

#ifdef PLATFORM_WEB
    emscripten_set_main_loop(update, 0, 1);
#else // PLATFORM_WEB
    SetTargetFPS(60);
    while(!WindowShouldClose())
    {
        update();
    }
#endif // PLATFORM_WEB

    delete[] curvePts;
    CloseWindow();
    return 0;
}
