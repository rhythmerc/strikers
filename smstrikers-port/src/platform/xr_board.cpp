// The stand under the pitch in a 3D frame: a tapered metal block whose top is the line the
// stadium is cut along (port/xr.h, PortXrBoard). One unlit mesh, shaded by vertex colour alone: a
// bright bevel along the top edge, the sides lit differently each and darkening downwards, and a
// dark foot band where it meets the table.

#include "port/xr.h"

#include "Game/GL/gluMeshWriter.h"
#include "NL/gl/gl.h"
#include "NL/gl/glState.h"
#include "NL/gl/glView.h"

namespace
{

const unsigned long BoardProgram = glGetProgram("3d unlit");
const unsigned long BoardTexture = glGetTexture("global/white");

struct Shade
{
    unsigned char r, g, b;
};

// Brushed steel, a little blue. The unlit program comes out about 1.7 times brighter than these
// on the Quest: twice as bright as this, the board looked near white.
const Shade kTop = { 28, 30, 34 };
const Shade kBevel = { 104, 108, 116 };
const Shade kSide = { 52, 56, 64 };
const Shade kFoot = { 18, 19, 22 };
const Shade kStand = { 34, 36, 41 };   // the stand's column, darker than the board

nlColour lit(const Shade& s, float k)
{
    nlColour c;
    const auto ch = [k](unsigned char v) {
        const float f = v * k;
        return (unsigned char)(f > 255.f ? 255.f : f);
    };
    nlColourSet(c, ch(s.r), ch(s.g), ch(s.b), 0xFF);
    return c;
}

void quad(GLMeshWriter& w, const nlVector3 (&p)[4], const nlColour (&c)[4])
{
    for (int i = 0; i < 4; i++)
    {
        w.Colour(c[i]);
        w.Texcoord(0.f, 0.f);
        w.Vertex(p[i]);
    }
}

}   // namespace

extern "C" void PortXrDrawBoard(void)
{
    float hx, hy, depth;
    if (!PortXrBoard(&hx, &hy, &depth))
        return;

    const float top = -0.03f;          // just under the pitch, which covers it
    const float bevel = 0.35f;         // the bright edge, out and down at 45 degrees
    const float flare = 1.2f;          // how much wider the foot is than the top
    const float footBand = 0.5f;       // the dark band at the bottom
    const float bevelZ = top - bevel;
    const float footZ = -depth + footBand;

    // Each side's own light, so the block reads as solid from any side: x+, y+, x-, y-.
    const float kSideLight[4] = { 0.95f, 0.8f, 0.7f, 1.05f };

    // The packet takes the state current as it's written.
    glSetDefaultState(true);
    glSetRasterState(GLS_Culling, 0);
    glSetCurrentRasterState(glHandleizeRasterState());
    glSetCurrentTexture(BoardTexture, GLTT_Diffuse);
    glSetCurrentTextureState(glHandleizeTextureState());
    const unsigned long prevProgram = glSetCurrentProgram(BoardProgram);

    float standX, standY, standH;
    const bool stand = PortXrStand(&standX, &standY, &standH);

    static const eGLStream kStreams[3] = { (eGLStream)0, (eGLStream)2, (eGLStream)3 };
    GLMeshWriter w;
    if (!w.Begin(4 * (1 + 4 * 3 + (stand ? 4 : 0)), GLP_QuadList, 3, kStreams, false))
    {
        glSetCurrentProgram(prevProgram);
        return;
    }

    {
        const nlVector3 p[4] = { { -hx, -hy, top }, { hx, -hy, top }, { hx, hy, top }, { -hx, hy, top } };
        const nlColour c[4] = { lit(kTop, 1.f), lit(kTop, 1.f), lit(kTop, 1.f), lit(kTop, 1.f) };
        quad(w, p, c);
    }

    // Corner rings, top to bottom: the top edge, the bevel's foot, the side's foot, the table.
    const float ringX[4] = { hx, hx + bevel, hx + bevel + flare, hx + bevel + flare };
    const float ringY[4] = { hy, hy + bevel, hy + bevel + flare, hy + bevel + flare };
    const float ringZ[4] = { top, bevelZ, footZ, -depth };
    for (int side = 0; side < 4; side++)
    {
        // Corners a and b of this side, counter-clockwise seen from above.
        static const float kCorner[5][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
        const float* a = kCorner[side];
        const float* b = kCorner[side + 1];
        const float k = kSideLight[side];
        for (int band = 0; band < 3; band++)
        {
            const int r0 = band, r1 = band + 1;
            const nlVector3 p[4] = {
                { a[0] * ringX[r0], a[1] * ringY[r0], ringZ[r0] },
                { a[0] * ringX[r1], a[1] * ringY[r1], ringZ[r1] },
                { b[0] * ringX[r1], b[1] * ringY[r1], ringZ[r1] },
                { b[0] * ringX[r0], b[1] * ringY[r0], ringZ[r0] },
            };
            nlColour upper, lower;
            if (band == 0)
            {
                upper = lit(kBevel, k);
                lower = lit(kBevel, k * 0.85f);
            }
            else if (band == 1)
            {
                upper = lit(kSide, k * 1.1f);
                lower = lit(kSide, k * 0.7f);
            }
            else
            {
                upper = lit(kFoot, k);
                lower = lit(kFoot, k * 0.8f);
            }
            const nlColour c[4] = { upper, lower, lower, upper };
            quad(w, p, c);
        }
    }

    // The stand: a plain column from the foot down to the surface the board was placed on, lit like
    // the sides and darkening toward the surface.
    for (int side = 0; stand && side < 4; side++)
    {
        static const float kCorner[5][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
        const float* a = kCorner[side];
        const float* b = kCorner[side + 1];
        const float zTop = -depth, zBottom = -depth - standH;
        const nlVector3 p[4] = {
            { a[0] * standX, a[1] * standY, zTop },
            { a[0] * standX, a[1] * standY, zBottom },
            { b[0] * standX, b[1] * standY, zBottom },
            { b[0] * standX, b[1] * standY, zTop },
        };
        const nlColour upper = lit(kStand, kSideLight[side]), lower = lit(kStand, kSideLight[side] * 0.6f);
        const nlColour c[4] = { upper, lower, lower, upper };
        quad(w, p, c);
    }

    const bool ended = w.End();
    glSetCurrentProgram(prevProgram);
    if (ended)
        glViewAttachModel(GLV_Unshadowed, w.GetModel());   // uncut (xr_scene.cpp, kBoardView)
}
