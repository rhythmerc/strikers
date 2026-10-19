package dev.strikers;

import org.libsdl.app.SDLActivity;

/* The game, immersive. SDLActivity loads libstrikers.so and runs SDL_main, which is Aurora's
 * lib/main.cpp calling the game's main. */
public class StrikersActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "strikers" };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    public org.libsdl.app.SDLSurface createSDLSurface(android.content.Context context) {
        return new dev.encounter.aurora.AuroraSurface(context);
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        /* Nothing else asks Android for the app's external files directory, so without this
         * /sdcard/Android/data/dev.strikers.game/files never exists, and strikers.ini and the disc
         * have nowhere to go. Before super.onCreate: that loads the library, and the game's static
         * constructors look there (port_executable_dir in src/platform/host.c). */
        getExternalFilesDir(null);
        /* AURORA_XR turns on OpenXR presentation (extern/aurora/lib/xr). Set before the library
         * loads; strikers.ini can still override any other AURORA_* knob. */
        try {
            android.system.Os.setenv("AURORA_XR", "1", true);
        } catch (android.system.ErrnoException e) {
            android.util.Log.e("strikers", "setenv failed", e);
        }
        super.onCreate(savedInstanceState);
    }
}
