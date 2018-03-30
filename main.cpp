#include <assert.h>
#include <float.h>
#include <list>
#include <math.h>
#include <queue>
#include <stdio.h>
#include <random>
#include <vector>

#include "raylib.h"

#include "mathutil.cpp"
#include "voronoi.cpp"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif // PLATFORM_WEB

using namespace std;

const int screenWidth = 800;
const int screenHeight = 600;

struct MovingPoint
{
    Vector2 position;
    Vector2 velocity;
};
vector<MovingPoint> inputPoints;

void DrawCompleteEdge(Vector2 start, Vector2 end)
{
    Vector2 screenSpaceStart = {start.x, screenHeight-start.y};
    Vector2 screenSpaceEnd = {end.x, screenHeight-end.y};
    DrawLineV(screenSpaceStart, screenSpaceEnd, VIOLET);
}

void DrawEdge(Vector2 start, Vector2 direction, Vector2 minCorner, Vector2 maxCorner)
{
    float distance = 1000.0f;
    if(direction.x != 0)
    {
        float endX = start.x + distance * direction.x;
        endX = max(endX, minCorner.x);
        endX = min(endX, maxCorner.x);
        distance = (endX - start.x)/direction.x;
    }
    else // direction.x == 0.0f
    {
        assert(abs(direction.y) == 1.0f);
        float endY = start.y + distance * direction.y;
        endY = max(endY, minCorner.y);
        endY = min(endY, maxCorner.y);
        distance = abs(endY - start.y);
    }

    Vector2 end = {start.x + distance*direction.x, start.y + distance*direction.y};

    Vector2 screenSpaceStart = {start.x, screenHeight-start.y};
    Vector2 screenSpaceEnd = {end.x, screenHeight-end.y};
    DrawLineV(screenSpaceStart, screenSpaceEnd, WHITE);
}

void DrawHorizontalLine(float worldY, Color color)
{
    int screenY = (int)((float)screenHeight - worldY);
    DrawLine(0, screenY, screenWidth, screenY, color);
}

void DrawParabola(Vector2 focus, float directrixY, float minX, float maxX, float maxY, Color color)
{
    Arc arc = {};
    arc.focus = focus;

    int pointCount = 50;
    Vector2* curvePts = new Vector2[pointCount];

    if(!isfinite(GetArcYForXCoord(arc, 0.0f, directrixY)))
    {
        Vector2 min = {focus.x - 1.0f, focus.y};
        Vector2 max = {focus.x + 1.0f, maxY};
        DrawEdge({focus.x, directrixY}, {0.0f, 1.0f}, min, max);
        return;
    }

    if(maxX < minX) return;
    assert(maxX >= minX);
    float x = minX;
    float xInterval = (maxX - minX)/(pointCount-1);

    for(int i=0; i<pointCount; i++)
    {
        // Change of variables from (x,y) to (w,y) for simplicity of expression
        curvePts[i] = {x, GetArcYForXCoord(arc, x, directrixY)};

        // Y increases downwards in screencoords, so flip each point around the x-axis.
        // This is just so that it is closer to what we usually get/expect in mathematics.
        curvePts[i].y = screenHeight - curvePts[i].y;
        x += xInterval;
    }

    for(int i=1; i<pointCount; i++)
    {
        DrawLineV(curvePts[i-1], curvePts[i], color);
    }
    DrawLine(0, screenHeight-(int)directrixY, screenWidth, screenHeight-(int)directrixY, WHITE);

    delete[] curvePts;
}

void DrawBeachlineItem(BeachlineItem* item, float directrixY)
{
    if(item == nullptr) return;

    float minX = 0.0f;
    float maxX = (float)screenWidth;
    if(item->type == BeachlineItemType::Arc)
    {
        Color lineColor = WHITE;

        BeachlineItem* prevItem = GetFirstParentOnTheLeft(item);
        BeachlineItem* nextItem = GetFirstParentOnTheRight(item);
        assert(!prevItem || (prevItem->type == BeachlineItemType::Edge));
        assert(!nextItem || (nextItem->type == BeachlineItemType::Edge));
        float maxY = (item->arc.focus.y + directrixY)*0.5f;
        if(prevItem)
        {
            Vector2 intersection;
            bool doesIntersect = GetEdgeArcIntersectionPoint(prevItem->edge, item->arc, directrixY, intersection);
            if(doesIntersect)
            {
                minX = clampf(intersection.x, 0.0f, (float)screenWidth);
            }
        }
        if(nextItem)
        {
            Vector2 intersection;
            bool doesIntersect = GetEdgeArcIntersectionPoint(nextItem->edge, item->arc, directrixY, intersection);
            if(doesIntersect)
            {
                maxX = clampf(intersection.x, 0.0f, (float)screenWidth);
                maxY = max(maxY, intersection.y);
            }
        }
        DrawParabola(item->arc.focus, directrixY, minX, maxX, maxY, lineColor);
    }
    else if(item->type == BeachlineItemType::Edge)
    {
        BeachlineItem* prevItem = GetFirstLeafOnTheLeft(item);
        BeachlineItem* nextItem = GetFirstLeafOnTheRight(item);
        assert(!prevItem || (prevItem->type == BeachlineItemType::Arc));
        assert(!nextItem || (nextItem->type == BeachlineItemType::Arc));
        float minY = item->edge.start.y;
        float maxY = minY;
        if(prevItem)
        {
            Vector2 intersection;
            bool doesIntersect = GetEdgeArcIntersectionPoint(item->edge, prevItem->arc, directrixY, intersection);
            if(doesIntersect)
            {
                minX = intersection.x;
                minY = min(minY, intersection.y);
            }
        }
        if(nextItem)
        {
            Vector2 intersection;
            bool doesIntersect = GetEdgeArcIntersectionPoint(item->edge, nextItem->arc, directrixY, intersection);
            if(doesIntersect)
            {
                maxX = intersection.x;
                maxY = max(maxY, intersection.y);
            }
        }
        DrawEdge(item->edge.start, item->edge.direction, {minX, minY}, {maxX, maxY});
    }

    DrawBeachlineItem(item->left, directrixY);
    DrawBeachlineItem(item->right, directrixY);
}

bool isInteractive = true;
bool isMoving = false;
bool shouldDrawFps = true;
#if PLATFORM_WEB
int UpdatesTillInitComplete = 2;
#endif // PLATFORM_WEB
void UpdateAndRender()
{
    bool shouldLog = false;
#if PLATFORM_WEB

#define TO_STR(x) #x
#define EXPAND_DEFINE_TO_STRING(x) TO_STR(x)
    if(UpdatesTillInitComplete > 0)
    {
        TraceLog(LOG_INFO, "Updates till init complete: %d", UpdatesTillInitComplete);
        UpdatesTillInitComplete--;
        shouldLog = true;
        if(UpdatesTillInitComplete == 0)
        {
            TraceLog(LOG_INFO, "Call init complete!");
            const char* initializedModuleName = EXPAND_DEFINE_TO_STRING(MODULE_NAME);
            EM_ASM_({emscriptenInitializationComplete(Pointer_stringify($0));}, initializedModuleName);
            TraceLog(LOG_INFO, "Init complete called");
        }
    }
#endif // PLATFORM_WEB

    int screenSpaceMouseY = GetMouseY();
    float worldSpaceMouseY = (float)(screenHeight - screenSpaceMouseY);
    if(!isInteractive)
    {
        worldSpaceMouseY = -FLT_MAX;
    }

    if(IsKeyPressed(KEY_T))
    {
        isMoving = !isMoving;
    }
    bool moveThisFrame = isMoving;
    if(IsKeyDown(KEY_Q))
    {
        moveThisFrame = true;
    }
    if(IsKeyPressed(KEY_W))
    {
        moveThisFrame = true;
    }
    if(IsKeyPressed(KEY_F))
    {
        shouldDrawFps = !shouldDrawFps;
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Collect input point data set");
    }
    vector<Vector2> fortunePoints;
    float dt = 1.0f/60.0f;
    for(MovingPoint& mp : inputPoints)
    {
        if(moveThisFrame)
        {
            mp.position.x += dt*mp.velocity.x;
            mp.position.y += dt*mp.velocity.y;
            if(mp.position.x <= 0.0f) mp.velocity.x = abs(mp.velocity.x);
            if(mp.position.y <= 0.0f) mp.velocity.y = abs(mp.velocity.y);
            if(mp.position.x >= (float)screenWidth) mp.velocity.x = -abs(mp.velocity.x);
            if(mp.position.y >= (float)screenHeight) mp.velocity.y = -abs(mp.velocity.y);
        }
        fortunePoints.emplace_back(mp.position);
    }
    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Run Fortune");
    }
    FortuneState fortune = FortunesAlgorithm(fortunePoints, worldSpaceMouseY);

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Begin draw");
    }
    BeginDrawing();
    if(shouldDrawFps)
    {
        DrawFPS(0, 0);
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Draw points");
    }
    DrawHorizontalLine(worldSpaceMouseY, WHITE);
    Vector2 pointSize = {8, 8};
    if(inputPoints.size() > 500)
    {
        pointSize = {4,4};
    }

    bool drawLabels = (inputPoints.size() < 10);
    for(MovingPoint mp : inputPoints)
    {
        Vector2 pt = mp.position;
        Vector2 position = {pt.x-pointSize.x/2.0f, screenHeight-pt.y-pointSize.y/2.0f};
        DrawRectangleV(position, pointSize, RED);

        if(drawLabels)
        {
            int fontSize = 8;
            char label[256];
            sprintf(label, "(%.0f,%.0f)", pt.x, pt.y);
            int textWidth = MeasureText(label, fontSize);
            int labelX = (int)position.x - textWidth/2;
            int labelY = (int)position.y - fontSize;
            if(labelY < 2*fontSize)
            {
                labelX = (int)position.x + 10;
                labelY = (int)position.y;
            }
            DrawText(label, labelX, labelY, fontSize, WHITE);
        }
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Draw beachline");
    }
    float directrixY = worldSpaceMouseY;
    if(isInteractive && fortune.beachlineRoot != nullptr)
    {
        DrawBeachlineItem(fortune.beachlineRoot, directrixY);
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Draw completed edges");
    }
    for(CompleteEdge* edge : fortune.edges)
    {
        DrawCompleteEdge(edge->endpointA, edge->endpointB);
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Draw events");
    }
    for(SweepEvent* evt : fortune.unencounteredEvents)
    {
        Color color = WHITE;
        if(evt->type == SweepEventType::NewPoint)
        {
            color = RED;
        }
        else if(evt->type == SweepEventType::EdgeIntersection)
        {
            if(!evt->edgeIntersect.isValid)
                color = GRAY;
            else
                color = BLUE;
        }
        DrawHorizontalLine(evt->yCoord, color);

    }

    int fontSize = 24;
    if(IsKeyDown(KEY_H))
    {
        int textY = 100;
        DrawRectangle(0, 0, screenWidth, screenHeight, {0, 0, 0, 175});
        DrawText("Hold Q to let the sites move around", 0, textY, fontSize, WHITE); textY += fontSize;
        DrawText("Press W to move for a single frame", 0, textY, fontSize, WHITE); textY += fontSize;
        DrawText("Press T to toggle movement", 0, textY, fontSize, WHITE); textY += fontSize;
        DrawText("Press F to toggle drawing FPS", 0, textY, fontSize, WHITE); textY += fontSize;
    }
    else
    {
        DrawText("Hold H for help", 0, screenHeight - fontSize, fontSize, WHITE);
    }

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "End draw");
    }
    EndDrawing();

    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Cleanup");
    }
    DeleteBeachlineItem(fortune.beachlineRoot);
    for(CompleteEdge* edge : fortune.edges)
    {
        delete edge;
    }
    for(SweepEvent* evt : fortune.unencounteredEvents)
    {
        delete evt;
    }
    if(shouldLog)
    {
        TraceLog(LOG_INFO, "Done");
    }
}

int main()
{
    char windowTitle[] = "Fortunes Algorithm";
    InitWindow(screenWidth, screenHeight, windowTitle);
    ClearBackground(BLACK);
    vector<Vector2> initialPoints;

    int testCase = 0;
    switch(testCase)
    {
        case 0:
            // Example case: The points shown in the GIF of Fortune's algorithm on wikipedia
            initialPoints.emplace_back(Vector2{155, 552});
            initialPoints.emplace_back(Vector2{405, 552});
            initialPoints.emplace_back(Vector2{624, 463});
            initialPoints.emplace_back(Vector2{211, 419});
            initialPoints.emplace_back(Vector2{458, 358});
            initialPoints.emplace_back(Vector2{673, 299});
            initialPoints.emplace_back(Vector2{261, 278});
            initialPoints.emplace_back(Vector2{ 88, 196});
            initialPoints.emplace_back(Vector2{497, 177});
            initialPoints.emplace_back(Vector2{715, 118});
            initialPoints.emplace_back(Vector2{275,  99});
            break;
        case 1:
            // Test case 1: Points with equal x
            initialPoints.emplace_back(Vector2{300, 300});
            initialPoints.emplace_back(Vector2{300, 400});
            initialPoints.emplace_back(Vector2{400, 350});
            break;

        case 2:
            // Test case 2: Points with equal y (where those points are not the first points)
            initialPoints.emplace_back(Vector2{300, 300});
            initialPoints.emplace_back(Vector2{200, 200});
            initialPoints.emplace_back(Vector2{400, 200});
            break;

        case 3:
            // Test case 3: Points with equal y (where those points are the first points)
            //              With a third point that is slightly off to one side.
            //              Requires a special case for the first points to prevent errors in finding the replacedarc
            initialPoints.emplace_back(Vector2{320, 200});
            initialPoints.emplace_back(Vector2{200, 300});
            initialPoints.emplace_back(Vector2{400, 300});
            break;

        case 31:
            // Test case 3a: Points with equal y (where those points are the first points).
            //               With a third point that exactly lines up with the edge between the first 2.
            //               Requires the special case for edges that intersect at both of their starting points (they should not be counted as intersecting).
            initialPoints.emplace_back(Vector2{300, 200});
            initialPoints.emplace_back(Vector2{200, 300});
            initialPoints.emplace_back(Vector2{400, 300});
            break;

        case 32:
            // Test case 3b: 3 points with equal y (and nothing else)
            initialPoints.emplace_back(Vector2{300, 300});
            initialPoints.emplace_back(Vector2{200, 300});
            initialPoints.emplace_back(Vector2{400, 300});
            break;

        case 4:
            // Test case 4: A completely-surrounded site
            initialPoints.emplace_back(Vector2{100, 100});
            initialPoints.emplace_back(Vector2{500, 150});
            initialPoints.emplace_back(Vector2{300, 300});
            initialPoints.emplace_back(Vector2{100, 550});
            initialPoints.emplace_back(Vector2{500, 500});
            break;

        case 5:
            // Test case 5: An arc gets squeezed by a later-created arc before it would be squeezed by its original edges.
            //              Requires handling of events that get "pre-empted" and are no longer required by the time they would execute.
            initialPoints.emplace_back(Vector2{300, 500});
            initialPoints.emplace_back(Vector2{200, 450});
            initialPoints.emplace_back(Vector2{400, 450});
            initialPoints.emplace_back(Vector2{300, 400});
            break;

        default:
            break;
    }

    default_random_engine generator(2);
    uniform_real_distribution<float> distribution(-1.0f, 1.0f);
    if(initialPoints.size() == 0)
    {
        isInteractive = false;
        int pointCount = 1000;
        for(int i=0; i<pointCount; i++)
        {
            Vector2 site = {};
            site.x = (0.5f + (distribution(generator)*0.5f)) * screenWidth;
            site.y = (0.5f + (distribution(generator)*0.5f)) * screenHeight;
            initialPoints.emplace_back(site);
        }
    }

    float speed = 40.0f;
    for(Vector2 position : initialPoints)
    {
        Vector2 velocity = {};
        velocity.x = speed*distribution(generator);
        velocity.y = speed*distribution(generator);

        float timeAdvance = (1.0f/60.0f)*0;
        Vector2 newPosition = {};
        newPosition.x = position.x + velocity.x * timeAdvance;
        newPosition.y = position.y + velocity.y * timeAdvance;
        MovingPoint mp = {newPosition, velocity};
        inputPoints.emplace_back(mp);
    }

    Vector2 hackfixPosition = Vector2{400, 1500};
    Vector2 hackfixVelocity = {};
    inputPoints.emplace_back(MovingPoint{hackfixPosition, hackfixVelocity});

#ifdef PLATFORM_WEB
    emscripten_set_main_loop(UpdateAndRender, 0, 1);
#else // PLATFORM_WEB
    SetTargetFPS(60);
    while(!WindowShouldClose())
    {
        UpdateAndRender();
    }
#endif // PLATFORM_WEB

    CloseWindow();
    return 0;
}
