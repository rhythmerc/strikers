#ifndef _PORT_XR_H_
#define _PORT_XR_H_

// Mixed reality on the Quest (Aurora's lib/xr): which frames show the match in 3D, on a pitch
// placed in the room, and which stay on the virtual screen, and how each view's draws are tagged
// for it. Without an XR session, and in builds without AURORA_ENABLE_OPENXR, all of it is a no-op
// and PortXr3D() is 0.

#ifdef __cplusplus
extern "C" {
#endif

// Once a frame, from BeginFrameTask, with the top camera's eCameraType and the stadium being
// played (eStadiumID), or -1 when no world is loaded. halfLength and halfWidth: the pitch's
// extent from its centre, along x (to the goal lines) and y (to the touchlines).
void PortXrBeginFrame(int cameraType, int stadium, float halfLength, float halfWidth);

// Non-zero while this frame is shown in 3D.
int PortXr3D(void);

// In a 3D frame, the game's six frustum planes (a, b, c, d: kept where a x + b y + c z + d >= 0) are
// replaced with these: the stands and roof cut away from the 3D view, and nothing else, since the
// eyes see the whole pitch whatever the game camera does. Returns 0, leaving them, otherwise.
int PortXrCullPlanes(float planes[6][4]);

// Before each view's packets go out, with that view's view matrix in GX layout (world to camera,
// the game's z up).
void PortXrSendView(int view, const float viewMtx[3][4]);

// After the last view: whatever is drawn later is flat-frame only.
void PortXrEndViews(void);

// The pause menu opened (1) or closed (0). While it's open the hands can grab the pitch.
void PortXrPauseMenu(int open);

#ifdef __cplusplus
}
#endif

#endif // _PORT_XR_H_
