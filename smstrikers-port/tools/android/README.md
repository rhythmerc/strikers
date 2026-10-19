# Meta Quest build

The game runs as an immersive OpenXR app. It is shown on a virtual screen over
passthrough, through Aurora's `lib/xr`. There is no 2D launcher panel yet: the
disc and the settings come from the app's external files directory.

## Build

Run this in a Linux environment with:

- an Android SDK at `~/Android` (or `ANDROID_HOME`), with an NDK r28 or newer;
- JDK 17;
- Python 3;
- CMake 3.28 or newer, and Ninja.

```sh
tools/android/build.sh            # build/android-arm64/strikers-quest-debug.apk
```

- **Dawn:** the script builds Dawn from the `melee-xr` branch of
  `github.com/rhythmerc/dawn`. Aurora's OpenXR presentation renders into the
  runtime's swapchain images on Dawn's own device, which stock Dawn cannot do.
  The script uses `~/projects/dawn` when that exists, otherwise it clones the
  pinned commit into `build/dawn-src`. Override with `STRIKERS_DAWN_SOURCE`.
- **Build time:** the first build compiles Dawn and takes a while. Later
  builds only recompile what changed.
- **CPU target:** `cortex-a78c`, which is all six cores of the Quest 3's XR2
  Gen 2. Override with `STRIKERS_TARGET_CPU`.

## Install and set up

```sh
adb install -r build/android-arm64/strikers-quest-debug.apk
adb shell am start -n dev.strikers.game/dev.strikers.StrikersActivity
```

The first launch creates `/sdcard/Android/data/dev.strikers.game/files` and
then stops, because there is no disc yet.

- **Disc:** put the disc image (`.iso`, `.gcm`, `.ciso` or `.gcz`) in that
  directory.
  - Don't `adb push` into it before the app has created it. A directory adb
    makes is owned by the shell user, and the app cannot read it.
  - Pushing to `/sdcard/Download` and then `adb shell mv` into place is quick.
- **Settings:** `strikers.ini` goes in the same directory. It takes the same
  keys as on desktop, plus Aurora's own `AURORA_*` keys, because `am start`
  cannot pass an environment.
- **Saves and caches:** these live in the app's internal storage
  (`run-as dev.strikers.game ls files`).

Useful settings (the first two default to 80 here; see below):

```ini
AURORA_XR_REFRESH = 90     # 72, 80, 90 or 120
fps_limit = 90             # the game's limiter, at the headset's rate
xr_pitch = 1.0             # the 3D pitch's length on the table, in metres
xr_board_end = 3           # game units of board past each goal line
xr_board_side = 2          # game units of board past each touchline
xr_board_depth = 2.5       # game units of board under the pitch
xr_stands = 1              # the stadium in its stands (near sides cut) instead of on a board
xr_roof = 8                # game units: everything higher is cut from the 3D view
xr_flat = 1                # matches on the virtual screen instead of in 3D
```

Matches are in 3D while the camera follows live play, on a metal board standing on the table.
Replays and cut-scenes stay on the screen. At 80 Hz the 3D match runs at the top of dynamic
resolution (1.3 times the headset's recommended size); at 90 Hz it holds about as well at native
resolution, and at 72 Hz with room to spare.
Pause to grab the pitch and move, turn or scale it.

## Measure

`benchmark = 1` (with `benchmark_seconds`, `seed` and `bench_record`) drives
the demo match and prints the usual summary to logcat. That includes the
staging pools' peaks, because an overflow of one is fatal.

- **Logs:** everything the game prints goes to logcat under the tag
  `strikers` (`adb logcat -s strikers`).
- **XR statistics:** Aurora's XR layer adds a statistics line every 10
  seconds, beginning `[aurora::xr] NN display fps`.
- **Crashes:** a crash leaves a tombstone. The port's own crash handler is
  off here.
