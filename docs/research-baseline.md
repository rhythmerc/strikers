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

### Step 2 results (2026-10-19, desktop x86-64)

- **Flags now on by default (Linux and Android):**
  - `STRIKERS_FP_CONTRACT_FAST`, `STRIKERS_NO_STACK_PROTECTOR` and
    `STRIKERS_LTO`.
  - `STRIKERS_TARGET_CPU` is empty by default. The Android build sets it once
    the Quest's cores are read (`-mcpu`).
  - `-O3` was already the Release default. Aurora pinned no flags of its own:
    melee-pc's `-O2` and contract=off pins lived in melee-pc's CMake.
- **ThinLTO stops at Aurora.** The game compiles with `-fshort-wchar` (the
  console's 2-byte `wchar_t`, used in 131 files). LLVM won't import functions
  between modules that disagree on `wchar_t` size, so LTO covers only the game,
  NL, ODE, the SDK math and `src/platform`. Taking it across into Aurora means
  converting the game to `char16_t`.
- **Build fixes for LTO:**
  - `tools/genstubs.py` no longer passes `--no-llvm-bc` when reading the
    build's own objects. Under LTO those objects are bitcode, so it found no
    undefined symbols and wrote no stubs.
  - The Linux deps script now installs `llvm` for `llvm-ar` and `llvm-nm`.
    Without them, configure warns and builds without LTO.
- **Timing.** Main-thread CPU per match frame, fixed seed and timestep,
  3 × 60 s per build run in rotation:

  | Build | mean | p95 | Per-run means (µs) |
  |---|---|---|---|
  | Baseline (all off) | 628 | 852 | 621, 615, 647 |
  | ThinLTO | 599 | 817 | 595, 607, 595 |
  | ThinLTO + x86-64-v3 | 595 | 822 | 586, 599, 599 |
  | ThinLTO + x86-64-v3 + contract=fast | 584 | 808 | 569, 593, 592 |

  - ThinLTO is worth 2–4%: every LTO run beat every baseline run.
  - contract=fast's extra 1–2% (measured with x86-64-v3 standing in for
    AArch64's FMA) is within run-to-run noise. The real number comes from the
    Quest.
  - Neither `x86-64-v3` build plays the same match as the baseline, so their
    rows compare similar matches, not identical ones.
- **Correctness:**
  - With the flags off, frames are pixel-identical to step 1.
  - The ThinLTO build is pixel-identical to the baseline on all 17 frames
    captured during a deterministic match.
  - contract=fast repeats itself exactly run to run. Against the baseline it
    drifts by a few pixels after about 25 s and plays a different match from
    about 75 s. Screenshots look normal, and there are no NaNs or asserts.
- **Netplay.** contract=fast doesn't rule it out. Identical binaries still
  agree. Cross-platform lockstep would need contract=off on the simulation
  targets and its own libm, regardless of this flag.

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

### Step 3 results (2026-10-19, Quest 3)

**The gate is met:** a match plays at the display rate on the virtual screen
at both 72 and 90 Hz. Each figure below is from one 60 s benchmark demo match
(Wario Stadium, seed 1) with warm shader caches. The flat frame is shown on
Aurora's virtual screen.

| | 72 Hz | 90 Hz |
|---|---|---|
| Presented | 72.02 fps | 90.01 fps |
| Fresh screen frames from the XR layer | 72.0/s | 90.0/s |
| Game thread busy, mean / p95 | 2.14 / 2.97 ms | 1.89 / 2.46 ms |
| Present/drain, mean / p95 | 3.65 / 6.49 ms | 3.81 / 5.88 ms |
| Frame budget | 13.9 ms | 11.1 ms |

**Notes on these numbers:**

- The CPU budget isn't the problem the frame-rate section feared: the game
  thread uses under a quarter of the frame at 90 Hz.
- **Cores:** the Quest 3's six cores are all Cortex-A78C (`0xd4b`), so the
  build uses `-mcpu=cortex-a78c` for the whole tree, with ThinLTO and
  contract=fast from step 2.
- **Pacing:** the game runs its own limiter at the headset's rate
  (`fps_limit`). It isn't locked to the XR loop yet:
  - At 72 Hz the XR loop never waited for a frame.
  - At 90 Hz it waited for the screen image on every display frame (up to
    9.2 ms) but never went without one.
  - Locking the game to `xrWaitFrame` is still the plan from section 4. Until
    then, a game hitch shows as a missed display frame.
- **Staging pool:** the Android `storage` pool went from 16 to 32 MiB. One
  match start needed 16.8 MiB and aborted. Strikers stages far more vertex
  data than Melee, and Android's CPU vertex decode adds to it. The steady peak
  is 11.7 MiB, cold or warm; the 16.8 MiB frame hasn't recurred.
- **Texture uploads:** they reach the 12 MiB pool's cap and fall back to
  one-off buffers, which is handled.

**What it took** (the runbook is `smstrikers-port/tools/android/README.md`):

- **Packaging:** `libstrikers.so` entered through Aurora's `SDL_main`, one
  immersive activity (no 2D launcher panel yet), and `tools/android/build.sh`
  using NDK r29, the `rhythmerc/dawn` fork and the OpenXR loader.
- **Data and settings:** "beside the game" is the app's external files
  directory, so the disc image, `strikers.ini` (including `AURORA_*` keys)
  and `frames.csv` all live there.
- **Logs:** stdout and stderr go to logcat.
- **Input:** the Touch controllers are merged into port 0 with the port's
  other synthetic input.
- **Android-specific fixes:**
  - The game's `include/malloc.h` re-entered bionic's `<stdlib.h>`.
  - Bionic has no `backtrace()` before API 33, so the crash handler is off
    and debuggerd's tombstones take its place.
  - Movies and Discord are off.

**Not yet verified:** the Touch controllers by hand, and audio. Both need
someone in the headset.

**Since verified by hand:** audio is right. The controllers needed fixes:
- The Touch merge had been compiled out, because `AURORA_ENABLE_OPENXR` never
  reached the game code.
- The keyboard pad held port 0, so it's now off by default on Android.
- A gamepad now moves to port 0 in XR, as melee-pc does: SDL counts the
  Quest's own input devices, so a lone pad landed on port 1.

### Step 4 results (2026-10-19, Quest 3)

**The gate is met at 72 Hz:** a demo match on Wario Stadium plays in 3D on a
1 m pitch at a steady 72.0 frames a second. At 90 Hz, 3D reaches 53–62 fps.
Each figure is a mean over the 10 s windows that were all 3D, from 90 s
benchmark demo matches (seed 1), with dynamic resolution off.

| | 72 Hz | 90 Hz | 90 Hz, no MSAA |
|---|---|---|---|
| 3D frames released | 72.0/s | 53.3/s | 62.1/s |
| GPU, both eyes in one multiview pass | 9.5 ms | 9.2 ms | 7.5 ms |
| Frame budget | 13.9 ms | 11.1 ms | 11.1 ms |

**How it's built** (`smstrikers-port/src/platform/xr_scene.cpp`,
`include/port/xr.h`):

- **Which frames are 3D:** the top camera decides. Live-play cameras
  (gameplay, follow, kickoff, top-down, the strike cameras and the goal
  camera) show the match in 3D. Replays, animated cut-scenes and face
  close-ups stay on the virtual screen.
- **Per view:** as each of the 34 views goes out (`glx_SendViews`), it is
  tagged world, HUD, hidden or mono:
  - **Hidden:** the skybox and the screen-space passes.
  - **HUD plane:** Anark, the front end and the transitions.
  - **Mono:** shadow textures stay in the flat frame, where world draws read
    them.
- **Axes:** Strikers is z-up, and Aurora's arena logic is y-up. Each world
  view matrix goes to Aurora with the turn folded in, so Aurora needs no
  change for it.
- **Stands (the near-half rule):** for each side of the pitch the head is
  beyond, everything 2 units past that side's line is cut, with a 1.5-unit
  dithered edge. Everything over 8 units high (domes, cage walls) is cut too.
  The same planes replace the game's frustum planes in 3D frames, so objects
  wholly inside a cut are never drawn: 846 world draws a frame down to 695.
  Without that culling the eyes cost 1–3 ms more.
- **Pause:** the pause menu marks the match paused, so the hands can grab
  the pitch to move, turn or scale it.

**What limits 90 Hz:**

- **GPU per draw, not per pixel:**
  - The eye pass cost about the same at a 0.7 resolution scale as at 1.0.
  - Vertices are few, about 130k a frame.
  - The stadium view's 526 world draws cost 3.3 ms, about 6 µs each, which
    matches melee-pc's figure.
  - Fewer draws (batching, merged stadium meshes) is the lever.
- **The image hand-over:** the render worker waited over 2 ms for a 3D image
  about 440 times in 10 s. In direct mode, the XR thread releases a finished
  image and acquires the next only once per display frame. That suits a game
  ticked by that loop (Melee) and costs Strikers frames. Staged images made
  it worse (the display fell to 49 fps). This is step 6's pacing work.

**Aurora changes** (on `strikers`):
- **Dynamic resolution:** for a game on its own timer, it now budgets against
  one display frame, not Melee's 60 Hz tick. It may also go below native
  resolution without SpaceWarp.

**Android defaults** (set after `strikers.ini`, so the file still wins):
- 72 Hz, with the game's limiter to match.
- SpaceWarp off: every display frame is fresh, so it only added a 1.8 ms
  motion pass.
- A dynamic resolution floor of 0.7.

**Known gaps, next:**
- **The jumbotron:** replays and cut-scenes on a screen in the stadium. Some
  stadium meshes already carry jumbotron screens (`Jumbotron.cpp` drives
  them with canned clips). But during a replay the game rewinds the world
  itself, so the 3D pitch would replay too. That needs a design decision.
- **Player dissolve** between goal and kickoff
  (`aurora_xr_world_clips4_fade`).
- **Other stadiums:** the roof height and stand margin are one setting for
  every stadium for now (`xr_roof`, `xr_stand_margin`); step 5 sets them per
  stadium.
- `DoTranslucency` fades objects that block the game camera's view. In 3D
  that camera isn't the viewer, so the fade may be wrong. Not yet checked.
- `AURORA_XR_BINDLESS=0` dies on a WGSL extension error. It's off the default
  path, so it isn't fixed.

**Later the same day: the board, resolution and pacing.**

- **On a board:** at the user's request the stadium is no longer kept in its
  stands. Everything outside a box around the pitch is cut hard, 3 units past
  each goal line and 2 past each touchline. The pitch stands on a tapered
  metal plinth (`src/platform/xr_board.cpp`): one unlit, vertex-coloured mesh
  of 52 vertices, drawn in its own view so the cuts don't take it.
  - The arena's position is the plinth's foot, so it stands on the table.
  - The old near-side rule is still available as `xr_stands`.
  - World draws fell from 695 to about 510.
- **Resolution:** dynamic resolution can now climb in this mode. The climb
  waited on margins from lock-step pacing's probe, which a game on its own
  timer never gets. The 0.7 floor is gone too (it bought nothing here).

| On the board | 3D frames | Scale | Eyes |
|---|---|---|---|
| 72 Hz | full | 1.30 | 9.1 ms |
| 80 Hz | 78.3/s (98%) | 1.30 | 8.1 ms |
| 90 Hz | 88.0/s (98%) | 1.00 | 6.6 ms |
| 90 Hz, MSAA off | 88.4/s | 1.00 | 6.7 ms |

- **Pacing, corrected:** a system trace (`atrace`, Aurora's trace sections)
  shows the hand-over isn't what limits 90 Hz.
  - The render worker encodes the eyes ahead into an image the XR thread has
    acquired but not yet waited on, and submits about 1 ms after each loop
    wakes.
  - A frame misses only when its GPU work (the eyes plus Meta's compositor)
    isn't done by the next loop.
  - The 3D image waits counted above were that, not a cause.
  - With the stands in, the eyes cost 9 ms; on the board they cost 6.6 ms
    at native resolution.
- **Default:** 72 Hz, every frame at the top of dynamic resolution: the playable baseline.
  80 Hz was the default at first; a clean 90 Hz is later optimisation work.

**Lighting in the eyes:**
- **Symptom:** stages that are bright in 2D came out dark and tinted in 3D. Mario Stadium's pale
  pitch was navy and Forbidden Dome's concrete was murky. Wario, Yoshi and Super looked close.
- **Cause:** Aurora's batched eye shader read each draw's normal matrices at Melee's uniform offset
  (word 260). Strikers' Aurora has 13 position matrices, not 10, so the normal matrices start at
  word 296. Every batched lit draw was lit with texture-matrix words as its normals.
  `AURORA_XR_BATCH=0` showed the right colours, which is how it was found. The offsets now come
  from `MaxPnMtx` and `MaxTexMtx`.
- **Fog in the eyes** also used the eye's own depth, and a per-column range table indexed by the
  eye's pixel columns. It now takes the game camera's depth, as the flat frame does, without the
  range adjustment.
- **Tools:** a stadium can be forced with `force_stadium` (0–6). The eye dump (`AURORA_XR_DUMP`)
  set beside desktop shots of the same stadium is the quickest way to compare.

**Debug settings:**
- `xr_hide` (eGLView numbers): leaves views out of 3D, to measure each one's
  cost.
- `probe_views`: logs packets and vertices per view.
- `xr_flat`: keeps matches on the screen.

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
