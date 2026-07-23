package alvr.client;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.graphics.Typeface;
import android.speech.tts.TextToSpeech;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.LinkedList;
import java.util.Locale;
import java.util.concurrent.Semaphore;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class VRActivity extends Activity {
    // NOTE: native libraries are loaded DEFENSIVELY inside onCreate(), not in a
    // static block. A static-block load that fails (missing/ABI-mismatched .so,
    // Rust static-init panic) would throw before onCreate even runs, producing a
    // silent flash-crash with no logs. Loading in onCreate lets us catch it.

    final static String TAG = "VRActivity";
    private static final String LOG_FILE_NAME = "alvr_runtime.log";

    class RenderingCallbacks implements SurfaceHolder.Callback {
        @Override
        public void surfaceCreated(@NonNull final SurfaceHolder holder) {
            Log.i(TAG, "surfaceCreated");
            mScreenSurface = holder.getSurface();
            // Convert the Java Surface to ANativeWindow immediately. Dream Pro
            // destroys the Java presentation Surface a few milliseconds later,
            // before the rendering thread finishes native initialization.
            setSurfaceNative(mScreenSurface);
            maybeResume();
        }

        @Override
        public void surfaceChanged(@NonNull SurfaceHolder holder, int _fmt, int _w, int _h) {
            Log.i(TAG, "surfaceChanged " + _w + "x" + _h);
            // Android normally sends surfaceChanged immediately after
            // surfaceCreated for the same Surface. Restarting native VR here
            // queued pause/resume back-to-back, which detached the EGL context
            // after the first two submitted frames and left the headset black.
            mScreenSurface = holder.getSurface();
            maybeResume();
        }

        @Override
        public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
            // Dream Pro transiently destroys the Android presentation surface
            // while transferring ownership to the Qiyu compositor. Keep the
            // Surface reference and native VR session alive; treating this as
            // a real app shutdown stops submissions after one or two frames.
            Log.i(TAG, "surfaceDestroyed (owned by Qiyu; keeping VR active)");
        }
    }

    final BroadcastReceiver mBatInfoReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctxt, Intent intent) {
            onBatteryChangedNative(intent.getIntExtra(BatteryManager.EXTRA_LEVEL, 0),
                    intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0);
        }
    };

    boolean mResumed = false;
    boolean mNativeResumed = false;
    boolean mNativeInitialized = false;
    Handler mRenderingHandler;
    HandlerThread mRenderingHandlerThread;
    Surface mScreenSurface;

    // TextToSpeech for speaking errors aloud -- in VR the compositor crops 2D text
    // so aggressively that reading a multi-word message is nearly impossible. Hearing
    // it is the only reliable channel. Lazy-init on first error.
    TextToSpeech mTts;
    boolean mTtsReady = false;

    // Cache method references for performance reasons
    final Runnable mRenderRunnable = this::render;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Android requires window features to be requested before any code can
        // display a fallback/error view through setContentView().
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        // 1) Surface any crash from the PREVIOUS run. The C++ logger writes to our
        //    private dir; on the next launch we read the tail and show it on-screen.
        //    No USB / wireless-debug / file-manager needed.
        maybeShowPreviousCrash();

        // 2) Load native libs defensively. loadNativeLibs() shows its own error
        //    screen on failure (with the EXACT failed library name prominent),
        //    so we just bail if it returns false.
        if (!loadNativeLibs()) {
            return;
        }

        // 3) The Qiyu SDK's AndroidPlugin constructor requires (Activity, int) and
        //    its setNativeAssetManager is an instance method. Rather than guessing
        //    constructor params (which may have side effects), we let qiyu_Init()
        //    handle AndroidPlugin initialization — it already receives our Activity
        //    object and can create the instance + set the AssetManager internally.
        //    The qiyu_recenter.png asset is in app/src/main/assets/ for it to find.

        // 4) Point the C++ file logger at our private, always-writable directory.
        setLogFilePath(new File(getFilesDir(), LOG_FILE_NAME).getAbsolutePath());

        setContentView(R.layout.activity_main);
        SurfaceView surfaceView = findViewById(R.id.surfaceview);

        mRenderingHandlerThread = new HandlerThread("Rendering thread");
        mRenderingHandlerThread.start();
        mRenderingHandler = new Handler(mRenderingHandlerThread.getLooper());
        mRenderingHandler.post(() -> {
            initializeNative();
            synchronized (VRActivity.this) {
                mNativeInitialized = true;
            }
            runOnUiThread(this::maybeResume);
        });

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(new RenderingCallbacks());

        this.registerReceiver(this.mBatInfoReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // If native lib loading failed, the rendering thread was never created.
        if (mRenderingHandler == null) return;
        Semaphore sem = new Semaphore(1);
        try {
            sem.acquire();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        mRenderingHandler.post(() -> {
            Log.i(TAG, "Destroying vrapi state.");
            destroyNative();
            sem.release();
        });
        mRenderingHandlerThread.quitSafely();
        try {
            // Wait until destroyNative() is finished. Can't use Thread.join here, because
            // the posted lambda might not run, so wait on an object instead.
            sem.acquire();
            sem.release();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();

        Log.i(TAG, "onResume");
        synchronized (this) {
            mResumed = true;
        }
        if (mRenderingHandler != null) {
            maybeResume();
        }
    }

    synchronized void maybeResume() {
        Log.i(TAG, "maybeResume resumed=" + mResumed
                + " surface=" + (mScreenSurface != null)
                + " native=" + mNativeResumed
                + " initialized=" + mNativeInitialized);
        if (mScreenSurface != null && !mNativeResumed) {
            final Surface surface = mScreenSurface;
            mNativeResumed = true;
            mRenderingHandler.post(() -> {
                onResumeNative(surface);

                // bootstrap the rendering loop
                mRenderingHandler.post(mRenderRunnable);
            });
        }
    }

    @Override
    protected void onPause() {
        // Qiyu's SDK can issue a transient Activity pause while the VR surface
        // remains valid. Tearing down native VR here detaches EGL and produces
        // a permanent black screen. surfaceDestroyed() owns native teardown.
        Log.i(TAG, "onPause (keeping VR active until surfaceDestroyed)");
        super.onPause();
    }

    synchronized void maybePause() {
        if (mNativeResumed) {
            mNativeResumed = false;
            mRenderingHandler.post(this::onPauseNative);
        }
    }

    private void render() {
        if (mNativeResumed && mScreenSurface != null) {
            renderNative();

            mRenderingHandler.removeCallbacks(mRenderRunnable);
            mRenderingHandler.postDelayed(mRenderRunnable, 2);
        }
    }

    native void initializeNative();

    native void destroyNative();

    native void onResumeNative(Surface screenSurface);

    native void setSurfaceNative(Surface screenSurface);

    native void onPauseNative();

    native void onStreamStartNative();

    native void onStreamStopNative();

    native void renderNative();

    native void onBatteryChangedNative(int battery, boolean plugged);

    native void setLogFilePath(String path);

    // Load the full native dependency chain in CORRECT topological order,
    // derived from the actual ELF DT_NEEDED graph (parsed via tools/elf_needed.py):
    //
    //   libvrapi.so            -> system libs only
    //   libashreader.so        -> system libs only (uses libstdc++.so, NOT libc++_shared)
    //   libsxrapi.so           -> system libs only
    //   libqiyivrsdkcore.so    -> NEEDS libsxrapi.so + libashreader.so  (must load AFTER both!)
    //   libqiyuapi.so          -> NEEDS libqiyivrsdkcore.so + libsxrapi.so
    //   libalvr_client_core.so -> Rust core (may want libc++_shared)
    //   libnative_lib.so       -> our bridge, needs all above
    //
    // Shows a MINIMAL on-screen status: each lib gets an OK/FAIL line. On failure,
    // the failed lib name is shown HUGE and red so it's readable even when the VR
    // compositor crops the 2D surface. Returns false on failure (screen already shown).
    private boolean loadNativeLibs() {
        String dir = getApplicationInfo().nativeLibraryDir;

        // Build the status text as we go. On failure this becomes the error screen.
        StringBuilder status = new StringBuilder();

        // libc++_shared.so is OPTIONAL: the Qiyu prebuilts use libstdc++.so (system),
        // not libc++_shared; only the Rust core *might* need it. Try it, don't fail.
        try {
            System.load(dir + "/libc++_shared.so");
            status.append("  OK   libc++_shared.so\n");
        } catch (Throwable ignored) {
            status.append("  --   libc++_shared.so (optional)\n");
        }

        // Required chain in dependency order.
        String[] required = {
                "libvrapi.so",
                "libashreader.so",        // <-- BEFORE qiyivrsdkcore (it's a dep)
                "libsxrapi.so",
                "libqiyivrsdkcore.so",
                "libqiyuapi.so",
                "libalvr_client_core.so",
                "libnative_lib.so",
        };

        for (String lib : required) {
            try {
                System.load(dir + "/" + lib);
                status.append("  OK   ").append(lib).append("\n");
            } catch (Throwable t) {
                // FAILED. Show a minimal screen: the status list (small) + the
                // failed lib name HUGE + the cause keyword. Each piece is short
                // enough to survive VR compositor cropping.
                status.append(" FAIL  ").append(lib).append("\n");

                String cause = t.getMessage();
                if (cause == null) cause = t.toString();
                String fact = extractKeyFact(cause);

                // Full detail to file for later analysis.
                writeCrashFile("Failed to load " + lib + "\nCause: " + cause
                        + "\n\nStatus:\n" + status.toString()
                        + "\n\nFull stack:\n" + Log.getStackTraceString(t));

                // Best-effort TTS (may not work on custom VR ROMs without TTS engine).
                speak(lib + " failed to load. " + fact.replace("\n", " "));

                showLoadErrorScreen(lib, fact, status.toString());
                return false;
            }
        }
        return true;
    }

    // Minimal error screen: failed library name HUGE + cause keyword + small
    // status list. Every line is short so the VR compositor's center-crop
    // doesn't hide the key information.
    private void showLoadErrorScreen(String failedLib, String causeFact, String statusList) {
        // The failed library name — THE key fact. Huge, red, centered.
        TextView libName = new TextView(this);
        libName.setText(failedLib);
        libName.setTextColor(0xFFFF0000);
        libName.setTextSize(TypedValue.COMPLEX_UNIT_SP, 32);
        libName.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        libName.setGravity(Gravity.CENTER);
        libName.setPadding(8, 8, 8, 8);

        // The cause keyword (e.g. "MISSING LIB:\nlibstdc++.so"). Yellow, large.
        TextView cause = new TextView(this);
        cause.setText(causeFact);
        cause.setTextColor(0xFFFFFF00);
        cause.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        cause.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        cause.setGravity(Gravity.CENTER);
        cause.setPadding(8, 4, 8, 8);

        // The full status list (small, green) so the user sees which libs loaded OK.
        TextView list = new TextView(this);
        list.setText(statusList);
        list.setTextColor(0xFF00FF00);
        list.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        list.setTypeface(Typeface.MONOSPACE);
        list.setGravity(Gravity.CENTER);
        list.setPadding(8, 8, 8, 8);

        LinearLayout center = new LinearLayout(this);
        center.setOrientation(LinearLayout.VERTICAL);
        center.setGravity(Gravity.CENTER);
        center.setBackgroundColor(0xFF000000);
        center.addView(libName);
        center.addView(cause);
        center.addView(list);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF000000);
        root.addView(center, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER));

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(root);
    }

    @SuppressWarnings("unused")
    public void onStreamStart() {
        mRenderingHandler.post(this::onStreamStartNative);
    }

    @SuppressWarnings("unused")
    public void onStreamStop() {
        mRenderingHandler.post(this::onStreamStopNative);
    }

    // ---- On-device crash surfacing (no USB / wireless-debug needed) ---------

    private File crashLogFile() {
        return new File(getFilesDir(), LOG_FILE_NAME);
    }

    private void maybeShowPreviousCrash() {
        File f = crashLogFile();
        if (!f.exists() || f.length() == 0) return;
        // Scan the recent tail for a clean-exit marker. If the previous run did
        // NOT exit cleanly (crashed anywhere: lib load, init, render), surface it.
        String scan = readTail(f, 500);
        if (scan == null) return;
        if (scan.toLowerCase().contains("clean exit")) return; // previous run exited fine
        String tail = readTail(f, 200);
        showErrorScreen("PREVIOUS RUN DID NOT EXIT CLEANLY. Last log:\n\n"
                + (tail != null ? tail : scan));
    }

    private String readTail(File f, int lines) {
        try {
            BufferedReader br = new BufferedReader(new FileReader(f));
            LinkedList<String> queue = new LinkedList<>();
            String line;
            while ((line = br.readLine()) != null) {
                queue.add(line);
                if (queue.size() > lines) queue.removeFirst();
            }
            br.close();
            StringBuilder sb = new StringBuilder();
            for (String l : queue) sb.append(l).append("\n");
            return sb.toString();
        } catch (IOException e) {
            return null;
        }
    }

    // The Qiyu VR compositor shows a 2D Android surface ZOOMED/centered, so only
    // the middle band is visible (left/right/top/bottom edges are cropped). We show
    // ONLY a short, large, centered headline with the key fact (the missing library
    // or symbol), AND speak it aloud via TTS (the only reliable channel in VR).
    // The full text is also saved to a file.
    private void showErrorScreen(String msg) {
        writeCrashFile(msg);

        String fact = extractKeyFact(msg);

        // Speak the error aloud so the user can hear it even if the VR compositor
        // crops the on-screen text beyond readability.
        speak(fact);

        TextView head = new TextView(this);
        head.setText(fact);
        head.setTextColor(0xFFFFFF00); // yellow, high contrast on black
        head.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        head.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        head.setGravity(Gravity.CENTER);
        head.setPadding(16, 16, 16, 16);

        TextView hint = new TextView(this);
        hint.setText("Listen for spoken error. Full log saved to file.");
        hint.setTextColor(0xFFFFFFFF);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        hint.setGravity(Gravity.CENTER);

        LinearLayout center = new LinearLayout(this);
        center.setOrientation(LinearLayout.VERTICAL);
        center.setGravity(Gravity.CENTER);
        center.setBackgroundColor(0xFF000000);
        center.addView(head);
        center.addView(hint);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF000000);
        FrameLayout.LayoutParams flp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER);
        root.addView(center, flp);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(root);
    }

    // Pull the single most useful fact out of the (long) stack trace so it fits in
    // the visible center of the VR view: the missing library, or the missing symbol.
    // Handles several Android linker error phrasings across versions.
    private String extractKeyFact(String msg) {
        if (msg == null || msg.isEmpty()) return "Unknown error";
        // "library \"libfoo.so\" not found"
        Matcher m = Pattern.compile("library \"([^\"]+)\" not found").matcher(msg);
        if (m.find()) return "MISSING LIB:\n" + shortName(m.group(1));
        // "cannot locate symbol \"foo\""
        m = Pattern.compile("cannot locate symbol \"([^\"]+)\"").matcher(msg);
        if (m.find()) return "MISSING SYMBOL:\n" + m.group(1);
        // "cannot load library \"libfoo.so\""
        m = Pattern.compile("cannot load library \"([^\"]+)\"").matcher(msg);
        if (m.find()) return "MISSING LIB:\n" + shortName(m.group(1));
        // "cannot find library \"libfoo.so\"" (different verb on some Android versions)
        m = Pattern.compile("cannot find library \"([^\"]+)\"").matcher(msg);
        if (m.find()) return "MISSING LIB:\n" + shortName(m.group(1));
        // "needed by ... libfoo.so" -- the lib that has the unmet dependency
        m = Pattern.compile("needed by [^ ]*lib([a-z0-9_]+)\\.so").matcher(msg);
        if (m.find()) return "DEP MISSING FOR:\nlib" + m.group(1) + ".so";
        // "Library libfoo.so not found" (old format, no quotes)
        m = Pattern.compile("Library (lib[a-z0-9_]+\\.so) not found").matcher(msg);
        if (m.find()) return "MISSING LIB:\n" + m.group(1);
        // any ".so" path quoted
        m = Pattern.compile("\"([^\"]+\\.so)\"").matcher(msg);
        if (m.find()) return "LIB ERROR:\n" + shortName(m.group(1));
        // bare "libXXX.so" mention
        m = Pattern.compile("(lib[a-z0-9_]+\\.so)").matcher(msg);
        if (m.find()) return "LIB ERROR:\n" + m.group(1);
        // fallback: first non-empty line, truncated
        for (String line : msg.split("\n")) {
            line = line.trim();
            if (!line.isEmpty()) {
                if (line.length() > 40) line = line.substring(0, 40) + "...";
                return line;
            }
        }
        return "Unknown error";
    }

    private String shortName(String lib) {
        int i = lib.lastIndexOf('/');
        return i >= 0 ? lib.substring(i + 1) : lib;
    }

    // Lazy-init TextToSpeech and speak the given text. In VR, on-screen text is
    // often cropped by the compositor, so audio is the most reliable error channel.
    private void speak(String text) {
        try {
            if (mTts == null) {
                mTts = new TextToSpeech(this, status -> {
                    mTtsReady = (status == TextToSpeech.SUCCESS);
                    if (mTtsReady) {
                        try { mTts.setLanguage(Locale.US); } catch (Exception ignored) {}
                        doSpeak(text);
                    }
                });
            } else if (mTtsReady) {
                doSpeak(text);
            }
        } catch (Exception ignored) {
            // TTS not critical; ignore failures.
        }
    }

    private void doSpeak(String text) {
        try {
            mTts.speak(text, TextToSpeech.QUEUE_FLUSH, null, "alvr_err");
        } catch (Exception ignored) {
        }
    }

    // Persist the error to locations that are always writable without extra
    // permissions, so the user can pull it when a USB/cable is available later.
    private void writeCrashFile(String msg) {
        File[] candidates = new File[]{
                new File(getExternalFilesDir(null), "alvr_crash.txt"), // /sdcard/Android/data/<pkg>/files
                new File(getFilesDir(), "alvr_crash.txt"),             // /data/data/<pkg>/files
                new File(Environment.getExternalStoragePublicDirectory(
                        Environment.DIRECTORY_DOWNLOADS), "alvr_crash.txt"),
        };
        for (File f : candidates) {
            if (f == null) continue;
            try {
                File parent = f.getParentFile();
                if (parent != null) parent.mkdirs();
                try (FileWriter w = new FileWriter(f, true)) {
                    w.write(msg);
                    w.write("\n\n--------------------------------------------------\n\n");
                }
            } catch (IOException ignored) {
                // best effort
            }
        }
    }
}
