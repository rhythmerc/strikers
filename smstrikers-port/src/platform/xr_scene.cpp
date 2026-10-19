// Mixed reality: the match in 3D on a pitch placed in the room (port/xr.h).
//
// Aurora's lib/xr re-draws every draw tagged AURORA_XR_WORLD once per eye, from the headset's
// pose, and every one tagged AURORA_XR_HUD onto a flat plane over the pitch. A frame with no
// world draws goes to the virtual screen instead. Strikers draws through 34 named views, so the
// tagging is one category per view, set as each view's packets go out.
//
// Aurora works in Melee's axes, y up; Strikers' world has z up (x to the goal lines, y to the
// touchlines). Each world view matrix goes to Aurora with a turn from one to the other folded in,
// so Aurora sees a y-up world and everything it measures in game units (the grab box, the
// footprint, clip planes) holds. Points and plane normals turn the same way: (x, y, z) in the
// game is (x, z, -y) to Aurora.

#include "port/xr.h"

#include <cmath>
#include <cstdlib>

#include "dolphin/os.h"

#if defined(AURORA_ENABLE_OPENXR)
#include <aurora/xr.h>
#endif

#if !defined(AURORA_ENABLE_OPENXR)

extern "C" void PortXrBeginFrame(int, int, float, float) {}
extern "C" int PortXr3D(void) { return 0; }
extern "C" int PortXrCullPlanes(float[6][4]) { return 0; }
extern "C" void PortXrSendView(int, const float[3][4]) {}
extern "C" void PortXrEndViews(void) {}
extern "C" void PortXrPauseMenu(int) {}

#else

namespace
{

// eCameraType (Game/Camera/BaseCamera.h): the cameras that follow live play. The rest (replays,
// animated cut-scenes, face close-ups, the debug and viewer cameras) are authored shots that the
// 3D view would undo, so their frames stay on the screen.
bool camera_is_live(int type)
{
    switch (type)
    {
    case 1:    // FollowCharacter
    case 2:    // FollowBall
    case 5:    // KickOff
    case 6:    // TopDown
    case 7:    // Gameplay
    case 8:    // MatrixEffect
    case 9:    // Goal
    case 10:   // ShootToScore
        return true;
    default:
        return false;
    }
}

// eGLView (NL/gl/gl.h) to its category in a 3D frame.
const int kViewCategory[34] = {
    AURORA_XR_MONO,     // ShadowTexture: rendered to a texture, read by world draws
    AURORA_XR_MONO,     // GrabTexture
    AURORA_XR_HIDDEN,   // Skybox: the room shows instead
    AURORA_XR_WORLD,    // Shadowed
    AURORA_XR_WORLD,    // Shadow0
    AURORA_XR_HIDDEN,   // ShadowBlend0: a screen-space pass
    AURORA_XR_WORLD,    // WorldShadowed
    AURORA_XR_WORLD,    // Unshadowed
    AURORA_XR_HUD,      // BigBlackPolygon: flat, the strike arrow's backing
    AURORA_XR_HIDDEN,   // Warble: refracts a grab of the flat frame
    AURORA_XR_HIDDEN,   // WarbleBlend
    AURORA_XR_WORLD,    // Characters
    AURORA_XR_WORLD,    // CoPlanar0
    AURORA_XR_WORLD,    // CoPlanar
    AURORA_XR_WORLD,    // Shadow1
    AURORA_XR_HIDDEN,   // ShadowBlend1: a screen-space pass
    AURORA_XR_WORLD,    // UnsortedPerspective
    AURORA_XR_HIDDEN,   // DepthOfField
    AURORA_XR_WORLD,    // LingeringParticles
    AURORA_XR_WORLD,    // Particles
    AURORA_XR_WORLD,    // InvisiblePlane
    AURORA_XR_WORLD,    // ElectricFence
    AURORA_XR_HIDDEN,   // CameraSpace: fixed to the game camera
    AURORA_XR_HIDDEN,   // ScreenBlur
    AURORA_XR_HIDDEN,   // ScreenBlur2
    AURORA_XR_HIDDEN,   // ScreenGrab
    AURORA_XR_HUD,      // FrontEnd
    AURORA_XR_HUD,      // UnsortedOrtho
    AURORA_XR_HUD,      // Transitions3D
    AURORA_XR_HUD,      // Transitions
    AURORA_XR_HUD,      // Anark3D_BG
    AURORA_XR_HUD,      // Anark: the match HUD and the pause menu
    AURORA_XR_HUD,      // Anark3D_FG
    AURORA_XR_HUD,      // Debug
};

// xr_hide in strikers.ini: views (eGLView numbers, comma-separated) left out of 3D, to measure
// what each costs the eyes.
bool view_left_out(int view)
{
    static unsigned long long mask = [] {
        unsigned long long m = 0;
        for (const char* p = std::getenv("STRIKERS_XR_HIDE"); p != nullptr && *p != '\0';)
        {
            char* end;
            const long v = std::strtol(p, &end, 10);
            if (end == p)
                break;
            if (v >= 0 && v < 64)
                m |= 1ull << v;
            p = *end == ',' ? end + 1 : end;
        }
        return m;
    }();
    return (mask >> view) & 1;
}

// Default arena scale (AURORA_XR_ARENA_SCALE's default): Aurora's sizes are against it.
constexpr float kAuroraArenaScale = 0.0035f;

bool s_3d;
bool s_worldSent;   // this frame's clip planes went out with its first world view
int s_stadium = -1;
float s_halfLength, s_halfWidth;
int s_pauseMenus;

float cfg_float(const char* env, float fallback)
{
    const char* v = std::getenv(env);
    if (v == nullptr || *v == '\0')
        return fallback;
    const float f = std::strtof(v, nullptr);
    return f > 0.f ? f : fallback;
}

// The pitch's length on the table, in metres (xr_pitch in strikers.ini).
float pitch_metres() { return cfg_float("STRIKERS_XR_PITCH", 1.0f); }

void place_stadium(int stadium, float halfLength, float halfWidth)
{
    // Aurora's stage scale is a factor on its default metres per game unit.
    const float scale = pitch_metres() / (2.f * halfLength) / kAuroraArenaScale;
    aurora_xr_set_stage(stadium, 0.f, 0.f, 0.f, scale);

    // The pitch and its fences stand on the real surface; the stands beyond them are cut on the
    // near sides (update_cuts) and seen across the pitch, so they cast no shadow of their own.
    aurora_xr_set_stage_footprint(-halfLength, halfLength, -halfWidth, halfWidth, 0.f);
    OSReport("[port] xr: stadium %d, pitch %.1f x %.1f units, %.2f m long\n", stadium,
             2.f * halfLength, 2.f * halfWidth, pitch_metres());
}

// The stands between the player and the pitch would hide it, so each side of the pitch the head
// is beyond has everything past that side's line cut away (a little past it, with a soft edge, so
// the fences and the goals stay whole). Looking from a corner, two sides go. Everything above the
// stands goes too: roofs and domes would close over the player's head. Kept where
// a x + b y + c z + d >= 0, in the game's axes; worked out once a frame, at its start, so the
// game's culling (PortXrCullPlanes) and the eyes' clipping agree.
constexpr int kMaxCuts = 5;
float s_cuts[kMaxCuts][4];
int s_cutCount;

void update_cuts()
{
    s_cutCount = 0;
    auto keep = [](float a, float b, float c, float d) {
        float* p = s_cuts[s_cutCount++];
        p[0] = a;
        p[1] = b;
        p[2] = c;
        p[3] = d;
    };

    keep(0.f, 0.f, -1.f, cfg_float("STRIKERS_XR_ROOF", 8.0f));

    float m[3][4];
    if (!aurora_xr_audio_listener(m))
        return;

    // m takes Aurora's game units to the head's space: m = s R | t, so the head sits at
    // -(s R)^T t / s^2 in game units.
    const float s2 = m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0];
    float head[3];
    for (int c = 0; c < 3; c++)
        head[c] = -(m[0][c] * m[0][3] + m[1][c] * m[1][3] + m[2][c] * m[2][3]) / s2;
    const float gx = head[0], gy = -head[2];   // back to the game's ground axes

    const float margin = cfg_float("STRIKERS_XR_STAND_MARGIN", 2.0f);
    if (gx < -s_halfLength)
        keep(1.f, 0.f, 0.f, s_halfLength + margin);
    if (gx > s_halfLength)
        keep(-1.f, 0.f, 0.f, s_halfLength + margin);
    if (gy < -s_halfWidth)
        keep(0.f, 1.f, 0.f, s_halfWidth + margin);
    if (gy > s_halfWidth)
        keep(0.f, -1.f, 0.f, s_halfWidth + margin);
}

// The cuts to Aurora, for the eyes: in its axes, where a game point (x, y, z) is (x, z, -y).
void send_cuts()
{
    float planes[kMaxCuts][4];
    float fades[kMaxCuts];
    for (int i = 0; i < s_cutCount; i++)
    {
        planes[i][0] = s_cuts[i][0];
        planes[i][1] = s_cuts[i][2];
        planes[i][2] = -s_cuts[i][1];
        planes[i][3] = s_cuts[i][3];
        fades[i] = 1.5f;
    }
    aurora_xr_world_clips4(planes, fades, s_cutCount);
}

}   // namespace

extern "C" void PortXrBeginFrame(int cameraType, int stadium, float halfLength, float halfWidth)
{
    s_worldSent = false;
    s_3d = aurora_xr_active() && stadium >= 0 && camera_is_live(cameraType) &&
           std::getenv("STRIKERS_XR_FLAT") == nullptr;
    if (!s_3d)
        return;

    if (stadium != s_stadium || halfLength != s_halfLength || halfWidth != s_halfWidth)
    {
        s_stadium = stadium;
        s_halfLength = halfLength;
        s_halfWidth = halfWidth;
        place_stadium(stadium, halfLength, halfWidth);
    }
    aurora_xr_set_paused(s_pauseMenus > 0);
    update_cuts();
}

extern "C" int PortXr3D(void) { return s_3d; }

extern "C" int PortXrCullPlanes(float planes[6][4])
{
    if (!s_3d)
        return 0;
    for (int i = 0; i < 6; i++)
    {
        const bool cut = i < s_cutCount;
        for (int c = 0; c < 4; c++)
            planes[i][c] = cut ? s_cuts[i][c] : (c == 3 ? 1.0e30f : 0.f);
    }
    return 1;
}

extern "C" void PortXrSendView(int view, const float viewMtx[3][4])
{
    if (!s_3d || view < 0 || view >= (int)(sizeof kViewCategory / sizeof kViewCategory[0]))
        return;

    int category = kViewCategory[view];
    if (category == AURORA_XR_WORLD && view_left_out(view))
        category = AURORA_XR_HIDDEN;
    if (category != AURORA_XR_WORLD)
    {
        aurora_xr_camera(category, nullptr);
        return;
    }

    // V B^-1, where B turns the game's z-up world to Aurora's y-up one: (x, y, z) -> (x, z, -y).
    float v[3][4];
    for (int r = 0; r < 3; r++)
    {
        v[r][0] = viewMtx[r][0];
        v[r][1] = viewMtx[r][2];
        v[r][2] = -viewMtx[r][1];
        v[r][3] = viewMtx[r][3];
    }
    aurora_xr_camera(AURORA_XR_WORLD, v);
    if (!s_worldSent)
    {
        s_worldSent = true;
        send_cuts();
    }
}

extern "C" void PortXrEndViews(void)
{
    if (s_3d)
        aurora_xr_camera(AURORA_XR_MONO, nullptr);
}

extern "C" void PortXrPauseMenu(int open) { s_pauseMenus += open ? 1 : -1; }

#endif
