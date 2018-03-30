static float clampf(float val, float min, float max)
{
    if(val < min) return min;
    if(val > max) return max;
    return val;
}

static float max(float a, float b)
{
    if(a > b) return a;
    return b;
}

static float min(float a, float b)
{
    if(a > b) return b;
    return a;
}

static float Magnitude(Vector2 v)
{
    float result = sqrt(v.x*v.x + v.y*v.y);
    return result;
}

static Vector2 normalize(Vector2 v)
{
    assert((v.x != 0.0f) || (v.y != 0.0f));

    float length = Magnitude(v);
    Vector2 result;
    result.x = v.x/length;
    result.y = v.y/length;

    return result;
}
