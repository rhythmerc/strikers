# Strikers XR: research baseline (2026-10-18)

First pass at bringing the melee-pc Quest XR work (`~/projects/melee-pc`,
branch `quest-xr`) to the Super Mario Strikers port
([new-coke/strikers](https://github.com/new-coke/strikers), `f3369ac`).
This is research only. Nothing has been built or measured yet. Any number
below that comes from melee-pc is a Melee measurement.

## Bottom line

- **Strikers is a better XR target than Melee was, in three structural ways:**
  1. Every draw goes through one switch between 34 named render views. That
     gives world/HUD tagging at a single call site.
  2. The simulation already runs at a fixed 50 Hz, separate from a
     variable-rate render, so the game can render once per display frame
     natively.
  3. There's no netplay or Slippi parity to protect, so every flag that
     melee-pc held back for determinism can go.
- **Recommended base:** take our Aurora (melee-pc `extern/aurora`) as the
  trunk and forward-port Strikers' Aurora delta onto it, not the other way
  round. Ours is 24 upstream commits newer plus about 20k lines of
  XR/perf work. Strikers' delta is about 3.1k lines, mostly texture packs,
  perf and diagnostics.
- **The XR layer (`lib/xr`, `include/aurora/xr.h`) is mostly game-agnostic.**
  What's Melee-specific is the game-side glue in `src/pc/xr_*.c`, and that
  gets rewritten against Strikers' `glxSend.cpp`. It doesn't get ported.
- **The biggest design change is pacing.** melee-pc is built around a
  60 Hz game on a 120 Hz display (lock-step, SpaceWarp, early eyes, fixed
  latency). Strikers can just render at 72/90 Hz. That's melee-pc's
  "Option B" without its hard part (re-rendering stale game frames). The
  cost moves to CPU: the whole game frame has to fit in 11–14 ms on the
  XR2 Gen 2.

## 1. The Strikers port as it stands

| | |
|---|---|
| Layout | `smstrikers-port/`: `src/Game` + `src/NL` (decomp game and NL engine, ~260k lines, 503 TUs), `src/ode` (ODE physics), `extern/musyx` (MusyX audio), `src/platform` (port layer), `settings/` (Qt settings app) |
| Toolchain | Clang everywhere except Switch (devkitA64 GCC). C++11 game, C99, `-fsigned-char -fshort-wchar -fno-strict-aliasing`, a forced-include prelude |
| Data model | Native 64-bit pointers. Assets are byte-swapped at load time (`bmd_endian.c`, `skin_endian.c`, `wld_endian.c`, `cam_endian.c`). No MEM1 mapping, no non-PIE link, no `DISC_PTR`. Game allocations come from one 768 MB anonymous `mmap` region (`memalloc.cpp`, committed on touch) |
| Platforms | Windows, macOS, Linux x86-64/arm64, Steam Deck, **Switch (aarch64, NVK Vulkan via a Dawn fork, SDL3-on-SDL2)**. The Switch build already proves aarch64, Vulkan-on-Dawn and a low-power GPU. That's the closest thing to a Quest it has |
| Aurora | Vendored at `extern/aurora`. Upstream base ≈ `f118954` (2026-09-01), plus local edits (`tools/aurora-local-changes.patch` is stale and doesn't reverse-apply cleanly, so diff the tree instead). Built with `AURORA_ENABLE_DVD=OFF` (own `dvd.c`/`disc_reader.cpp`: ISO/GCM, CISO, GCZ, extracted folder), `CARD=ON`, `RMLUI=OFF`. MTX/VEC/QUAT come from the decomp's own `src/Dolphin/mtx`, not Aurora's (`include/port/aurora-overlay`) |
| Audio | MusyX runtime on a software mixer (`audio_mix.cpp`, `audio_out.cpp`). MusyX has a 3D emitter API (`snd3d.c`), the natural hook for spatial audio |
| Frame loop | `nlTaskManager::RunAllTasks` with a host-clock dt. `FixedUpdateTask` steps AI/physics/pads at **50 Hz** (`g_fFixedUpdateTick = 0.02`) from an accumulator. Render and animation run at whatever rate the limiter allows (`vi.c`, `framerate.h`: display-refresh-derived period, `STRIKERS_FPS_LIMIT`, a display-clock task-dt snap) |
| Determinism | Test-only (`STRIKERS_SEED`, `STRIKERS_FIXED_DT` for repeatable benchmarks). No netplay. No optimisation or FP policy in CMake at all |
| License | Port code CC0. Reconstructed game code: no rights claimed |

## 2. Aurora: lineage and merge plan

```
encounter/aurora ── f118954 (09-01) ── … 24 commits … ── 77326d4 (09-21) ── … ── 5179721 (10-12, HEAD)
                        │                                    │
                 Strikers vendored                   melee-pc vendored
                 +3,151 / −258, 62 files             +20,196 / −628, 101 files
                                                     (165 fork commits, 124 XR/perf/pipeline)
```

Both pin the same Dawn (`AURORA_DAWN_REF 1155e0ed`), so the `rhythmerc/dawn`
`melee-xr` fork (`343ff6c`) applies as is.

### What to keep from our fork

All of `lib/xr` (10.3k lines), `include/aurora/xr.h`, `gfx/xr_replay.hpp`,
and the gx/gfx work it depends on:

- Draw-category tagging through the FIFO (`GX_AURORA_XR_CAMERA`).
- The command processor's eye replay and multiview.
- Bindless eye batching and the eye sort.
- The motion pass.
- The pipeline-cache recording and precompile.
- Async pipeline compiles, and the shared Dawn blob cache across processes.
- The single-device Dawn path into OpenXR swapchains.
- Dynamic resolution.
- The Tracy/atrace hooks and `AURORA_XR_TIMING`.

### What to drop (Melee-only or Slippi parity)

| Piece | Why it goes |
|---|---|
| `include/dolphin/ppc_math.h` fused-op helpers, `lib/dolphin/mtx/{mtx,vec,mtxvec}.c`, `include/dolphin/mtx.h` changes (commits `9f66326`, `5c38f4b`, `a0f3959`, `48277c7`) | Gekko-exact math for Slippi replay parity. Strikers doesn't even compile Aurora's MTX |
| `-ffp-contract=off -fno-fast-math` on `aurora_gx`/`aurora_gd`/`aurora_mtx`, routing trig through `pc_trig.h` | Netplay bit-identity |
| `lib/dolphin/os/OSMemory.cpp` (MEM1 at `0x80000000`, the macOS/iOS high-half trick), most of `OSAlloc.cpp`/`AR.cpp` | Melee's 32-bit disc-pointer data model. Strikers has its own allocator and doesn't need it. Check that it's isolated behind the OS layer Strikers doesn't link |
| Dev-tools Stage mixer / `MELEE_STAGE_MIX`, the room grade's per-stage table, Melee naming in logs (`applicationName "melee-pc"`) | Game-specific. Keep the mechanisms, drop the data |

### What to forward-port from Strikers' delta

Ranked by how much it matters on Quest:

1. **Command processor perf**: u32 indices for large strips, in-place index
   fill, a per-config analysis memo, a capped memo. Melee's fork touched
   the same file heavily (+1,460 lines), so expect manual merges.
2. **Texture-replacement streaming** (+543 lines, `include/aurora/replacement.h`)
   and the texture-cache changes. Optional on Quest at first.
3. **Framebuffer-scale and overscan API**: `aurora_set_frame_buffer_scale`,
   `aurora_apply_frame_buffer_resize`, `gx_overscan_test`. Strikers' widescreen
   (`PortLogicalFrameWidth()`) depends on these.
4. **Diagnostics**: prim-desync reporter, `AURORA_DIAG_IDX`, the GPU frame-time
   API. Cheap to keep.
5. **Desktop-only** (skip on Quest): `startMaximized`, macOS window polling,
   `aurora_capture_frame`.

### Housekeeping

Melee-pc vendors Aurora in-tree. Two copies of a 20k-line fork will
drift. Consider moving it to its own repo (for example a `rhythmerc/aurora`
`xr` branch) and pulling it into both ports as a subtree.

## 3. Melee XR mechanism → Strikers equivalent

| melee-pc | What it does | Strikers equivalent |
|---|---|---|
| `xr_scene.c` tags in `cm/camera.c` (`fn_800301D0`) and `if/ifall.c` → `aurora_xr_camera(WORLD/HUD, view)` | World vs HUD vs mono | **`glx_SwitchViews(eGLView)`** in `src/NL/glx/glxSend.cpp:2585`, the one place `GXSetProjection` is called per view (plus `AdjustViewport`, `:1957`). It already loads `mview` per view, and `glx_SwitchMatrix` composes `M·V` into pos-matrix 0, the same `mv_pos` contract the eye replay assumes. A static `eGLView → category` table plus a per-view `aurora_xr_camera` call |
| Stage layer/part/joint/mesh rules, clip planes, moves | Trim stage backgrounds for tabletop | One rule set per stadium, keyed by view first, then model. Strikers has a handful of stadiums against Melee's 29 stages. `GLV_Skybox` is hidden wholesale. Crowd (`CrowdManager`, `3d crowd` programs), sideline props and the stadium shell are the survey targets |
| `MELEE_XR_OFFSCREEN` (keep drawing off-screen fighters) | The game camera's culling must not hide the table | `g_bClipToFrustum` (`world.cpp:44`) gates frustum culling. `DrawableCharacter` calls `IsSphereInFrustum`. Turn it off in XR, or feed the arena's frustum. Check LOD selection too |
| Pokémon Stadium screen / refraction need the mono frame | EFB-copy consumers | `GLV_GrabTexture`, `GLV_ScreenGrab`, `GLV_ScreenBlur*`, `GLV_DepthOfField`, `GLV_Warble*` and `Jumbotron` are screen-space or EFB-copy. Keep the mono frame for them and leave the screen-space post passes out of the eyes |
| HUD plane (`AURORA_XR_HUD_*`, stage-top clearance) | In-fight HUD | Ortho views: `GLV_FrontEnd`, `GLV_UnsortedOrtho`, `GLV_Anark*`, `GLV_Transitions*`. Anark is the in-match HUD and menu UI. `Indicators` (player arrows) are world-space and stay in 3D |
| Menus on the virtual screen | Non-fight scenes | Same. Also likely right for NIS cut-scenes, goal replays (`ReplayCamera`) and the kickoff/goal cams, which are authored camera work the tabletop view would undo. Open question: show replays on the screen, or as the game camera in 3D |
| `aurora_xr_set_stage(id, center, scale)`, footprint, top | Arena placement per stage | Per stadium: pitch centre, field extents from `cField` (goal lines, touchlines), roof height for the HUD |
| `aurora_xr_get_pad` merged in `keyboard.c` | Touch controllers as port 1 | Merge in `src/platform/input.cpp` / the Aurora PAD path Strikers already uses |
| `xr_pointer.c` laser on the screen | Menu pointing | Strikers' FE takes pad input only. Start with pad navigation and add a pointer later |
| `spatial_audio.c` (Steam Audio over AX voices) | Placed sounds | MusyX `snd3d` emitters already carry positions. Feed `aurora_xr_audio_listener()` into the listener. The crowd suits the decorrelated-seats treatment melee-pc already tuned |
| `stage_ambience.c`, `stage_mix.c` | Added stage sounds | Out of scope for the baseline |
| `xr_place.c` placement hold, cards, survey | Place the arena before each fight | Reuse as is. Re-author the card art |

**Things that will look wrong, by design** (as on Melee):

- Billboards and particles face the game camera.
- Camera shake and zoom disappear.
- Fog uses the eye's depth.

The fog point matters more here: stadiums are larger, and fog may be
tuned to the broadcast camera.

## 4. Frame rate: the main departure

melee-pc's pacing is built around a game that must tick at 60 Hz:

- `aurora_xr_pace` lock-step at 120 Hz.
- SpaceWarp at 60→120, with motion vectors and draw-identity pairing.
- Fixed-latency presentation.
- Early and deferred eyes.
- Dynamic resolution budgeted on `1e9/60`. Hard-coded 60s sit around
  `xr.cpp:2611`, `:8695` and `:8702`.

Strikers' gameplay state advances at 50 Hz regardless of render rate, and
everything else steps by a dt the port already snaps to whole display
periods (`PortTaskClockFrame`). So:

- **Plan of record: one game frame per display frame at 72 or 90 Hz.**
  - Every display frame is fresh, so there's no repeated-frame
    reprojection.
  - SpaceWarp, motion vectors and draw pairing aren't needed at first.
  - `aurora_xr_pace` becomes "every display frame", and the display period
    goes straight to the task clock.
  - The 60 Hz constants in `xr.cpp` become the display period.
- **The budget moves to CPU.** Melee measured the CPU chain (game → FIFO
  thread → render worker → submit) at 12–13 ms of a 16.7 ms tick on heavy
  stages. At 72 Hz Strikers has 13.9 ms, at 90 Hz 11.1 ms. It also has 10
  skinned characters, a crowd and ODE every 50 Hz step. **This is the first
  number to measure:** run the Linux arm64 build at a fixed 90 Hz with
  `STRIKERS_FIXED_DT`, then on the headset.
- **Check for 50 Hz judder.** If root motion and the ball update only on
  fixed ticks and rendering doesn't interpolate them, 72 or 90 Hz output
  will beat against 50 Hz motion. Look on desktop at 120 Hz before
  committing. If it judders, interpolate positions between ticks (state is
  already double-buffered in places, e.g. `m_v3PrevPosition`).
- **SpaceWarp stays available as a fallback** (half-rate render at 36/45 Hz
  → 72/90) if the CPU can't hold full rate. The machinery exists. It would
  need retuning off the 60 Hz loop.

## 5. Performance freed by dropping determinism

melee-pc parked all of these for netplay. In Strikers they cost nothing in
correctness:

- **`-O3` and `-ffp-contract=fast`** (fused multiply-add on AArch64)
  everywhere, including the game, ODE and Aurora's gx. melee-pc pins
  `-O2` and `contract=off` so peers match bit for bit.
- **ThinLTO across game and Aurora.** It was impossible on melee-pc because
  the game builds with GCC while Aurora builds with Clang. Strikers is Clang
  end to end on Android.
- **No `-ftrivial-auto-var-init=zero`** (melee-pc's C3). Strikers never had
  it, so don't add it.
- **No stack protector on hot TUs**: `aurora_gx`, game, ODE (melee-pc's C4).
- **`-mcpu` for XR2 Gen 2 on every target** (melee-pc's C6: Melee's `-mtune`
  never reached Aurora). Check the core type first rather than assuming
  Cortex-A73.
- **Still worth bringing over:** C1 (the bind-group path), C7/C8 (vertex
  cache and `ByteBuffer`), and G2 (stricter eye-batch reordering). These are
  all in Aurora and shared.

## 6. Quest build plumbing to carry over

From melee-pc `platforms/android`, `tools/build_android.sh` and
`docs/agent-build-deploy.md`:

- **Gradle project and activities.** The hybrid-app split: a 2D launcher
  panel activity plus an immersive `:xr`-process activity that sets
  `AURORA_XR=1`. The panel hands the disc over as an extra.
- **NDK and Dawn.** The `melee-xr` distrobox. Dawn from the `rhythmerc/dawn`
  fork, which needs NDK r28 or later. The OpenXR loader
  (`AURORA_OPENXR_ANDROID_VERSION 1.1.63`).
- **The GCC cross-build goes away.** Strikers builds with NDK Clang, so
  there's no r26/r29 split and no `gcc_launcher.py`. The whole tree is one
  toolchain.
- **Env file.** `melee-env.txt` becomes a `strikers-env.txt` under
  `/sdcard/Android/data/<pkg>/files`.
- **Perf harness.** `tools/quest_perf.py`, `ovrgpuprofiler` and the Perfetto
  configs in `tools/perfetto`.
- **Launcher.** Strikers' settings app is Qt and won't run on Quest.
  melee-pc's launcher is RmlUi, which Strikers builds without. For the
  baseline, a minimal Java panel (pick a disc, Play) that writes
  `strikers.ini` is less work than either.
- **Disc access.** Android scoped storage. See how melee-pc's
  `disc_open.c`/`android_compat.cpp` take a picked file. Strikers' own DVD
  layer reads by path, so it needs an fd/content-URI path. Its extracted-folder
  mode is a quick way to get a first boot.

## 7. Proposed order of work

1. **Repo setup.** Fork new-coke/strikers. Swap `extern/aurora` for ours
   with the Slippi math reverted. Forward-port Strikers' Aurora delta.
   Desktop Linux builds and plays with XR compiled out. *Gate:* parity with
   upstream Strikers on desktop.
2. **Flags pass.** `-O3`, contract=fast, ThinLTO, `-mcpu`. Use
   `benchmark.sh` and `STRIKERS_FIXED_DT` for a before/after on desktop
   arm64.
3. **Flat Quest build.** Android packaging, Touch → PAD, a virtual screen
   only (`AURORA_XR_3D=0`). *Gate:* a match plays at the display rate on the
   virtual screen. Measure CPU per frame at 72 and 90 Hz.
4. **3D matches.** Tag views in `glx_SwitchViews`. Turn frustum culling off.
   Hide the skybox. HUD views go to the HUD plane, replays and NIS to the
   screen. *Gate:* one stadium on the table at full rate.
5. **Per-stadium survey.** Crowd, shell and sidelines. Arena centre and
   scale from `cField`. Placement-hold cards.
6. **Pacing rework** if step 3 shows headroom problems: dynamic resolution
   on the display period, and SpaceWarp at half rate as a fallback.
7. **Spatial audio** through MusyX `snd3d` with the headset listener.

## 8. Open questions and risks

- **CPU at 72/90 Hz on the XR2 Gen 2.** This decides whether section 4's
  plan holds or SpaceWarp comes back.
- **Judder** between the 50 Hz simulation and 72/90 Hz render (section 4).
- **LOD and culling.** Beyond `g_bClipToFrustum`, the game may cut crowd
  density, NPCs or character LOD by distance from the broadcast camera.
- **Pitch scale on a table.** The pitch is far bigger than a Melee stage.
  Characters at tabletop scale may be hard to read, so a default
  floor-scale placement may suit better than a table.
- **Anark HUD.** Confirm the in-match HUD is all ortho views. If some is
  drawn in world space (score bug over the pitch, power-up icons), it needs
  rules.
- **The Strikers Aurora tree differs from its own patch file**, so the
  forward-port has to be a tree diff against `f118954`, not `git apply`.
- **Licensing.** Strikers' port code is CC0. melee-pc's game-side XR glue
  (`src/pc/xr_*.c`, `spatial_audio.c`) sits under the repo's GPL-3.0-or-later
  port license, but every commit to those files and to `lib/xr` is yours
  (rhythmerc), so relicensing is up to you. Aurora's own changes stay MIT.
