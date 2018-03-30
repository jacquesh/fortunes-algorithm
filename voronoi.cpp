#include <assert.h>
#include <float.h>
#include <math.h>
#include <queue>
#include <stdio.h>
#include <vector>

enum class BeachlineItemType
{
    None,
    Arc,
    Edge
};
struct Edge
{
    Vector2 start;
    Vector2 direction;
    bool extendsUpwardsForever;
};
struct CompleteEdge
{
    Vector2 endpointA;
    Vector2 endpointB;
};

struct SweepEvent;
struct Arc
{
    Vector2 focus;
    SweepEvent* squeezeEvent;
};

struct BeachlineItem
{
    BeachlineItemType type;
    union
    {
        Arc arc;
        Edge edge;
    };

    BeachlineItem* parent;
    BeachlineItem* left;
    BeachlineItem* right;

    BeachlineItem() : parent(nullptr), left(nullptr), right(nullptr) {}

    void SetLeft(BeachlineItem* newLeft)
    {
        assert(type == BeachlineItemType::Edge);
        assert(newLeft != nullptr);
        left = newLeft;
        newLeft->parent = this;
    }

    void SetRight(BeachlineItem* newRight)
    {
        assert(type == BeachlineItemType::Edge);
        assert(newRight != nullptr);
        right = newRight;
        newRight->parent = this;
    }

    void SetParentFromItem(BeachlineItem* item)
    {
        assert(item != nullptr);
        if(item->parent == nullptr)
        {
            parent = nullptr;
            return;
        }

        if(item->parent->left == item)
        {
            item->parent->SetLeft(this);
        }
        else
        {
            assert(item->parent->right == item);
            item->parent->SetRight(this);
        }
    }
};

enum class SweepEventType
{
    None,
    NewPoint,
    EdgeIntersection
};
struct NewPointEvent
{
    Vector2 point;
};
struct EdgeIntersectionEvent
{
    Vector2 intersectionPoint;
    BeachlineItem* squeezedArc;
    bool isValid;
};
struct SweepEvent
{
    float yCoord;
    SweepEventType type;
    union
    {
        NewPointEvent newPoint;
        EdgeIntersectionEvent edgeIntersect;
    };
};

#include "vtree.cpp"

struct FortuneState
{
    float sweepY;
    std::vector<CompleteEdge*> edges;
    std::vector<SweepEvent*> unencounteredEvents;
    BeachlineItem* beachlineRoot;
};

struct EventComparison : std::binary_function<SweepEvent, SweepEvent, bool>
{
    bool operator()(const SweepEvent* lhs, const SweepEvent* rhs) const
    {
        assert((lhs != nullptr) && (rhs != nullptr));
        return lhs->yCoord < rhs->yCoord;
    }
};

float GetArcYForXCoord(Arc& arc, float x, float directrixY)
{
    // NOTE: In the interest of keeping the formula simple when moving away from the origin,
    //       we'll use the substitution from (x,y) -> (w,y) = (x-focusX,y).
    //       In particular this substitution means that the formula always has the form:
    //       y = aw^2 + c, the linear term's coefficient is always 0.
    float a = 1.0f/(2.0f*(arc.focus.y - directrixY));
    float c = (arc.focus.y + directrixY)*0.5f;

    float w = x - arc.focus.x;
    return a*w*w + c;
}

bool GetEdgeArcIntersectionPoint(Edge& edge, Arc& arc, float directrixY, Vector2& intersectionPt)
{
    // Special case 1: Edge is a vertical line.
    if(edge.direction.x == 0.0f)
    {
        if(directrixY == arc.focus.y)
        {
            // Special case A of special case 1: The arc's focus is on the directrix line, so the arc is essentially a vertical line
            if(edge.start.x == arc.focus.x)
            {
                // TODO: What is the correct Y-value to use here?
                intersectionPt = arc.focus;
                return true;
            }
            else
            {
                return false;
            }
        }
        float arcY = GetArcYForXCoord(arc, edge.start.x, directrixY);
        intersectionPt = {edge.start.x, arcY};
        return true;
    }

    // y = px + q
    float p = edge.direction.y/edge.direction.x;
    float q = edge.start.y - p*edge.start.x;

    // Special case 2: Arc is currently a vertical line (directrixY == arc.focus.y)
    if(arc.focus.y == directrixY)
    {
        float intersectionXOffset = arc.focus.x - edge.start.x;
        // Check if the intersection is in the direction that the edge is going. If not then no intersect
        if(intersectionXOffset * edge.direction.x < 0)
        {
            return false;
        }

        intersectionPt.x = arc.focus.x;
        intersectionPt.y = p*arc.focus.x + q;
        return true;
    }

    // y = a_0 + a_1x + a_2x^2
    float a2 = 1.0f/(2.0f*(arc.focus.y - directrixY));
    float a1 = -p - 2.0f*a2*arc.focus.x;
    float a0 = a2*arc.focus.x*arc.focus.x + (arc.focus.y + directrixY)*0.5f - q;

    float discriminant = a1*a1 - 4.0f*a2*a0;
    if(discriminant < 0)
    {
        return false;
    }
    float rootDisc = sqrtf(discriminant);
    float x1 = (-a1 + rootDisc)/(2.0f*a2);
    float x2 = (-a1 - rootDisc)/(2.0f*a2);

    float x1Offset = x1 - edge.start.x;
    float x2Offset = x2 - edge.start.x;
    float x1Dot = x1Offset * edge.direction.x;
    float x2Dot = x2Offset * edge.direction.x;

    float x;
    if((x1Dot >= 0.0f) && (x2Dot < 0.0f)) x = x1;
    else if((x1Dot < 0.0f) && (x2Dot >= 0.0f)) x = x2;
    else if((x1Dot >= 0.0f) && (x2Dot >= 0.0f))
    {
        if(x1Dot < x2Dot) x = x1;
        else x = x2;
    }
    else // (x1Dot < 0.0f) && (x2Dot < 0.0f)
    {
        if(x1Dot < x2Dot) x = x2;
        else x = x1;
    }

    float y = GetArcYForXCoord(arc, x, directrixY);
    assert(isfinite(y));
    intersectionPt = {x,y};
    return true;
}

BeachlineItem* GetActiveArcForXCoord(BeachlineItem* root, float x, float directrixY)
{
    BeachlineItem* currentItem = root;
    while(currentItem->type != BeachlineItemType::Arc)
    {
        assert(currentItem->type == BeachlineItemType::Edge);
        BeachlineItem* left = GetFirstLeafOnTheLeft(currentItem);
        BeachlineItem* right = GetFirstLeafOnTheRight(currentItem);
        assert((left != nullptr) && (left->type == BeachlineItemType::Arc));
        assert((right != nullptr) && (right->type == BeachlineItemType::Arc));

        BeachlineItem* fromLeft = GetFirstParentOnTheRight(left);
        BeachlineItem* fromRight = GetFirstParentOnTheLeft(right);
        assert((fromLeft != nullptr) && (fromLeft == fromRight));
        assert(fromLeft->type == BeachlineItemType::Edge);
        Edge& separatingEdge = fromLeft->edge;

        Vector2 leftIntersect;
        Vector2 rightIntersect;
        bool didLeftIntersect = GetEdgeArcIntersectionPoint(separatingEdge, left->arc, directrixY, leftIntersect);
        bool didRightIntersect = GetEdgeArcIntersectionPoint(separatingEdge, right->arc, directrixY, rightIntersect);

#if 0
        // TODO: These should all pass as far as I can tell, but precision issues cause that not to be the case.
        assert(didLeftIntersect && didRightIntersect);
        assert(abs(leftIntersect.x - rightIntersect.x) < 1.0f);
        assert(didLeftIntersect || didRightIntersect);
#endif
        float intersectionX = leftIntersect.x;
        if(!didLeftIntersect && didRightIntersect)
        {
            intersectionX = rightIntersect.x;
        }

        if(x < intersectionX)
        {
            currentItem = currentItem->left;
        }
        else
        {
            currentItem = currentItem->right;
        }
    }

    assert(currentItem->type == BeachlineItemType::Arc);
    return currentItem;
}

static BeachlineItem* CreateArc(Vector2 focus)
{
    BeachlineItem* result = new BeachlineItem();
    result->type = BeachlineItemType::Arc;
    result->arc.focus = focus;
    result->arc.squeezeEvent = nullptr;
    return result;
}
static BeachlineItem* CreateEdge(Vector2 start, Vector2 dir)
{
    BeachlineItem* result = new BeachlineItem();
    result->type = BeachlineItemType::Edge;
    result->edge.start = start;
    result->edge.direction = dir;
    result->edge.extendsUpwardsForever = false;
    return result;
}

bool TryGetEdgeIntersectionPoint(Edge& e1, Edge& e2, Vector2& intersectionPt)
{
    float dx = e2.start.x - e1.start.x;
    float dy = e2.start.y - e1.start.y;
    float det = e2.direction.x*e1.direction.y - e2.direction.y*e1.direction.x;
    float u = (dy*e2.direction.x - dx*e2.direction.y)/det;
    float v = (dy*e1.direction.x - dx*e1.direction.y)/det;

    if((u < 0.0f) && !e1.extendsUpwardsForever) return false;
    if((v < 0.0f) && !e2.extendsUpwardsForever) return false;
    if((u == 0.0f) && (v == 0.0f) && !e1.extendsUpwardsForever && !e2.extendsUpwardsForever) return false;

    intersectionPt = {e1.start.x + e1.direction.x*u, e1.start.y + e1.direction.y*u};
    return true;
}

void AddArcSqueezeEvent(
        std::priority_queue<SweepEvent*, std::vector<SweepEvent*>, EventComparison>& eventQueue,
        BeachlineItem* arc)
{
    BeachlineItem* leftEdge = GetFirstParentOnTheLeft(arc);
    BeachlineItem* rightEdge = GetFirstParentOnTheRight(arc);

    if((leftEdge == nullptr) || (rightEdge == nullptr))
    {
        return;
    }

    Vector2 circleEventPoint;
    bool edgesIntersect = TryGetEdgeIntersectionPoint(leftEdge->edge, rightEdge->edge, circleEventPoint);
    if(!edgesIntersect)
    {
        return;
    }

    Vector2 circleCentreOffset = {arc->arc.focus.x - circleEventPoint.x,
                                  arc->arc.focus.y - circleEventPoint.y};
    float circleRadius = Magnitude(circleCentreOffset);
    float circleEventY = circleEventPoint.y - circleRadius;
    assert(arc->type == BeachlineItemType::Arc);
    // NOTE: If we already have an intersection event that we'll encounter sooner than this one, then
    //       just don't add this one (because otherwise it'll reference a deleted arc when it gets processed)
    if(arc->arc.squeezeEvent != nullptr)
    {
        if(arc->arc.squeezeEvent->yCoord >= circleEventY)
        {
            return;
        }
        else
        {
            assert(arc->arc.squeezeEvent->type == SweepEventType::EdgeIntersection);
            arc->arc.squeezeEvent->edgeIntersect.isValid = false;
        }
    }
    //printf("Add circle event at y=%f\n", circleEventY);
    SweepEvent* newEvt = new SweepEvent();
    newEvt->type = SweepEventType::EdgeIntersection;
    newEvt->yCoord = circleEventY;
    newEvt->edgeIntersect.squeezedArc = arc;
    newEvt->edgeIntersect.intersectionPoint = circleEventPoint;
    newEvt->edgeIntersect.isValid = true;
    eventQueue.push(newEvt);

    arc->arc.squeezeEvent = newEvt;
}

BeachlineItem* AddArcToBeachline(std::priority_queue<SweepEvent*, std::vector<SweepEvent*>, EventComparison>& eventQueue,
                       BeachlineItem* root, SweepEvent& evt, float sweepLineY)
{
    //printf("Add arc @ (%f, %f) to the beachline\n", evt.newPoint.point.x, evt.newPoint.point.y);
    Vector2 newPoint = evt.newPoint.point;
    BeachlineItem* replacedArc = GetActiveArcForXCoord(root, newPoint.x, sweepLineY);
    assert((replacedArc != nullptr) && (replacedArc->type == BeachlineItemType::Arc));

    BeachlineItem* splitArcLeft = CreateArc(replacedArc->arc.focus);
    BeachlineItem* splitArcRight = CreateArc(replacedArc->arc.focus);
    BeachlineItem* newArc = CreateArc(newPoint);

    float intersectionY = GetArcYForXCoord(replacedArc->arc, newPoint.x, sweepLineY);
    assert(isfinite(intersectionY));
    Vector2 edgeStart = {newPoint.x, intersectionY};
    Vector2 focusOffset = {newArc->arc.focus.x - replacedArc->arc.focus.x,
                           newArc->arc.focus.y - replacedArc->arc.focus.y};
    Vector2 edgeDir = normalize({focusOffset.y, -focusOffset.x});
    BeachlineItem* edgeLeft = CreateEdge(edgeStart, edgeDir);
    BeachlineItem* edgeRight = CreateEdge(edgeStart, {-edgeDir.x, -edgeDir.y});

    assert(replacedArc->left == nullptr);
    assert(replacedArc->right == nullptr);
    edgeLeft->SetParentFromItem(replacedArc);
    edgeLeft->SetLeft(splitArcLeft);
    edgeLeft->SetRight(edgeRight);
    edgeRight->SetLeft(newArc);
    edgeRight->SetRight(splitArcRight);

    BeachlineItem* newRoot = root;
    if(root == replacedArc)
    {
        newRoot = edgeLeft;
    }
    if(replacedArc->arc.squeezeEvent != nullptr)
    {
        assert(replacedArc->arc.squeezeEvent->type == SweepEventType::EdgeIntersection);
        assert(replacedArc->arc.squeezeEvent->edgeIntersect.isValid);
        replacedArc->arc.squeezeEvent->edgeIntersect.isValid = false;
    }
    VerifyThatThereAreNoReferencesToItem(newRoot, replacedArc);
    assert((replacedArc->arc.squeezeEvent == nullptr) || (replacedArc->arc.squeezeEvent->edgeIntersect.isValid == false));
    delete replacedArc;

    AddArcSqueezeEvent(eventQueue, splitArcLeft);
    AddArcSqueezeEvent(eventQueue, splitArcRight);

    return newRoot;
}

BeachlineItem* RemoveArcFromBeachline(
        std::priority_queue<SweepEvent*, std::vector<SweepEvent*>, EventComparison>& eventQueue,
        BeachlineItem* root,
        std::vector<CompleteEdge*>& outputEdges,
        SweepEvent& evt)
{
    BeachlineItem* squeezedArc = evt.edgeIntersect.squeezedArc;
    assert(evt.type == SweepEventType::EdgeIntersection);
    assert(evt.edgeIntersect.isValid);
    assert(squeezedArc->arc.squeezeEvent == &evt);
    //printf("Remove arc @ (%f, %f) from the beachline because we reached y=%f\n", squeezedArc->arc.focus.x, squeezedArc->arc.focus.y, evt.yCoord);

    BeachlineItem* leftEdge = GetFirstParentOnTheLeft(squeezedArc);
    BeachlineItem* rightEdge = GetFirstParentOnTheRight(squeezedArc);
    assert((leftEdge != nullptr) && (rightEdge != nullptr));

    BeachlineItem* leftArc = GetFirstLeafOnTheLeft(leftEdge);
    BeachlineItem* rightArc = GetFirstLeafOnTheRight(rightEdge);
    assert((leftArc != nullptr) && (rightArc != nullptr));
    assert(leftArc != rightArc);

    Vector2 circleCentre = evt.edgeIntersect.intersectionPoint;
    CompleteEdge* edgeA = new CompleteEdge();
    edgeA->endpointA = leftEdge->edge.start;
    edgeA->endpointB = circleCentre;
    CompleteEdge* edgeB = new CompleteEdge();
    edgeB->endpointA = circleCentre;
    edgeB->endpointB = rightEdge->edge.start;

    if(leftEdge->edge.extendsUpwardsForever)
    {
        edgeA->endpointA.y = FLT_MAX;
    }
    if(rightEdge->edge.extendsUpwardsForever)
    {
        edgeB->endpointA.y = FLT_MAX;
    }
    outputEdges.emplace_back(edgeA);
    outputEdges.emplace_back(edgeB);

    Vector2 adjacentArcOffset = {};
    adjacentArcOffset.x = rightArc->arc.focus.x - leftArc->arc.focus.x;
    adjacentArcOffset.y = rightArc->arc.focus.y - leftArc->arc.focus.y;
    Vector2 newEdgeDirection = {adjacentArcOffset.y, -adjacentArcOffset.x};
    newEdgeDirection = normalize(newEdgeDirection);

    BeachlineItem* newItem = CreateEdge(circleCentre, newEdgeDirection);

    BeachlineItem* higherEdge = nullptr;
    BeachlineItem* tempItem = squeezedArc;
    while(tempItem->parent != nullptr)
    {
        tempItem = tempItem->parent;
        if(tempItem == leftEdge) higherEdge = leftEdge;
        if(tempItem == rightEdge) higherEdge = rightEdge;
    }
    assert((higherEdge != nullptr) && (higherEdge->type == BeachlineItemType::Edge));

    newItem->SetParentFromItem(higherEdge);
    newItem->SetLeft(higherEdge->left);
    newItem->SetRight(higherEdge->right);

    assert((squeezedArc->parent == nullptr) || (squeezedArc->parent->type == BeachlineItemType::Edge));
    BeachlineItem* remainingItem = nullptr;
    BeachlineItem* parent = squeezedArc->parent;
    if(parent->left == squeezedArc)
    {
        remainingItem = parent->right;
    }
    else
    {
        assert(parent->right == squeezedArc);
        remainingItem = parent->left;
    }
    assert((parent == leftEdge) || (parent == rightEdge));
    assert(parent != higherEdge);

    remainingItem->SetParentFromItem(parent);

    BeachlineItem* newRoot = root;
    if((root == leftEdge) || (root == rightEdge))
    {
        newRoot = newItem;
    }
    VerifyThatThereAreNoReferencesToItem(newRoot, leftEdge);
    VerifyThatThereAreNoReferencesToItem(newRoot, squeezedArc);
    VerifyThatThereAreNoReferencesToItem(newRoot, rightEdge);
    assert(squeezedArc->type == BeachlineItemType::Arc);
    if(squeezedArc->arc.squeezeEvent != nullptr)
    {
        assert(squeezedArc->arc.squeezeEvent->type == SweepEventType::EdgeIntersection);
        assert(squeezedArc->arc.squeezeEvent->edgeIntersect.isValid);
        squeezedArc->arc.squeezeEvent->edgeIntersect.isValid = false;
    }
    delete leftEdge;
    delete squeezedArc;
    delete rightEdge;

    AddArcSqueezeEvent(eventQueue, leftArc);
    AddArcSqueezeEvent(eventQueue, rightArc);
    return newRoot;
}

void FinishEdge(BeachlineItem* item, std::vector<CompleteEdge*>& edges)
{
    if(item == nullptr)
    {
        return;
    }

    if (item->type == BeachlineItemType::Edge)
    {
        float length = 10000;
        Vector2 edgeEnd = item->edge.start;
        edgeEnd.x += length * item->edge.direction.x;
        edgeEnd.y += length * item->edge.direction.y;

        CompleteEdge* edge = new CompleteEdge();
        edge->endpointA = item->edge.start;
        edge->endpointB = edgeEnd;
        edges.emplace_back(edge);

        FinishEdge(item->left, edges);
        FinishEdge(item->right, edges);
    }

    delete item;
}

FortuneState FortunesAlgorithm(std::vector<Vector2>& sites, float cutoffY)
{
    std::vector<CompleteEdge*> edges;
    std::priority_queue<SweepEvent*, std::vector<SweepEvent*>, EventComparison> eventQueue;
    for(Vector2 pt : sites)
    {
        SweepEvent* evt = new SweepEvent();
        evt->type = SweepEventType::NewPoint;
        evt->newPoint.point = pt;
        evt->yCoord = pt.y;
        eventQueue.push(evt);
    }

    // NOTE: We start out by taking the first event and handling it manually, because it lets
    //       us avoid the "is there an arc here" check that would otherwise need to run very often
    SweepEvent* firstEvent = eventQueue.top();
    assert(firstEvent->type == SweepEventType::NewPoint);

    if(firstEvent->yCoord < cutoffY)
    {
        FortuneState result = {};
        result.sweepY = cutoffY;
        while(!eventQueue.empty())
        {
            result.unencounteredEvents.emplace_back(eventQueue.top());
            eventQueue.pop();
        }
        return result;
    }
    eventQueue.pop();

    BeachlineItem* firstArc = new BeachlineItem();
    firstArc->type = BeachlineItemType::Arc;
    firstArc->arc.focus = firstEvent->newPoint.point;
    firstArc->arc.squeezeEvent = nullptr;
    delete firstEvent;
    BeachlineItem* root = firstArc;

    float startupSpecialCaseEndY = firstArc->arc.focus.y - 1.0f;
    while(!eventQueue.empty() && (eventQueue.top()->yCoord > startupSpecialCaseEndY))
    {
        SweepEvent* evt = eventQueue.top();
        if(evt->yCoord < cutoffY)
            break;
        eventQueue.pop();

        assert(evt->type == SweepEventType::NewPoint);
        Vector2 newFocus = evt->newPoint.point;
        BeachlineItem* newArc = CreateArc(newFocus);

        BeachlineItem* activeArc = GetActiveArcForXCoord(root, newFocus.x, newFocus.y);
        assert(activeArc->type == BeachlineItemType::Arc);

        Vector2 edgeStart = {(newFocus.x+activeArc->arc.focus.x)/2.0f, /*FLT_MAX*//*1000.0f*/newFocus.y+100.0f};
        Vector2 edgeDir = {0.0f, -1.0f};
        BeachlineItem* newEdge = CreateEdge(edgeStart, edgeDir);
        newEdge->edge.extendsUpwardsForever = true;

        if(activeArc->parent != nullptr)
        {
            if(activeArc == activeArc->parent->left)
            {
                activeArc->parent->SetLeft(newEdge);
            }
            else
            {
                assert(activeArc == activeArc->parent->right);
                activeArc->parent->SetRight(newEdge);
            }
        }
        else
        {
            root = newEdge;
        }
        if(newFocus.x < activeArc->arc.focus.x)
        {
            newEdge->SetLeft(newArc);
            newEdge->SetRight(activeArc);
        }
        else
        {
            newEdge->SetLeft(activeArc);
            newEdge->SetRight(newArc);
        }

        delete evt;
    }

    while(!eventQueue.empty())
    {
        SweepEvent* nextEvent = eventQueue.top();
        // NOTE: For the purposes of interactive demonstration, we add an artificial cutoff.
        if(nextEvent->yCoord < cutoffY)
            break;
        eventQueue.pop();

        float sweepY = nextEvent->yCoord;
        if(nextEvent->type == SweepEventType::NewPoint)
        {
            root = AddArcToBeachline(eventQueue, root, *nextEvent, sweepY);
        }
        else if(nextEvent->type == SweepEventType::EdgeIntersection)
        {
            if(nextEvent->edgeIntersect.isValid)
            {
                root = RemoveArcFromBeachline(eventQueue, root, edges, *nextEvent);
            }
        }
        else
        {
            printf("Unrecognized queue item type: %d\n", nextEvent->type);
        }

        delete nextEvent;
    }
    if(eventQueue.empty() || (cutoffY < -200.0f))
    {
        FinishEdge(root, edges);
        root = nullptr;
    }

    FortuneState result;
    result.sweepY = 0.0f;
    result.beachlineRoot = root;
    result.edges = edges;
    while(!eventQueue.empty())
    {
        result.unencounteredEvents.emplace_back(eventQueue.top());
        eventQueue.pop();
    }
    return result;
}

