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
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.annotation.NonNull;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.LinkedList;
import java.util.concurrent.Semaphore;

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
            mScreenSurface = holder.getSurface();
            maybeResume();
        }

        @Override
        public void surfaceChanged(@NonNull SurfaceHolder holder, int _fmt, int _w, int _h) {
            maybePause();
            mScreenSurface = holder.getSurface();
            maybeResume();
        }

        @Override
        public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
            maybePause();
            mScreenSurface = null;
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
    Handler mRenderingHandler;
    HandlerThread mRenderingHandlerThread;
    Surface mScreenSurface;

    // Cache method references for performance reasons
    final Runnable mRenderRunnable = this::render;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 1) Surface any crash from the PREVIOUS run. The C++ logger writes to our
        //    private dir; on the next launch we read the tail and show it on-screen.
        //    No USB / wireless-debug / file-manager needed.
        maybeShowPreviousCrash();

        // 2) Load native libs defensively, loading the Qiyu SDK dependency chain
        //    explicitly so any failure names the EXACT missing library/symbol
        //    instead of surfacing deep inside native_lib. Any failure is caught.
        try {
            loadNativeLibs();
        } catch (Throwable t) {
            String libs = "Native library load failed.\n"
                    + "If the message says \"cannot locate symbol\", that symbol is missing from\n"
                    + "the named .so (version/ABI mismatch or packaging issue).\n"
                    + "If it says \"library ... not found\", that .so is not packaged in the APK.\n\n";
            showErrorScreen(libs + "Error detail:\n\n" + Log.getStackTraceString(t));
            return;
        }

        // 3) Point the C++ file logger at our private, always-writable directory.
        setLogFilePath(new File(getFilesDir(), LOG_FILE_NAME).getAbsolutePath());

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        setContentView(R.layout.activity_main);
        SurfaceView surfaceView = findViewById(R.id.surfaceview);

        mRenderingHandlerThread = new HandlerThread("Rendering thread");
        mRenderingHandlerThread.start();
        mRenderingHandler = new Handler(mRenderingHandlerThread.getLooper());
        mRenderingHandler.post(this::initializeNative);

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(new RenderingCallbacks());

        this.registerReceiver(this.mBatInfoReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
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

        mResumed = true;
        maybeResume();
    }

    void maybeResume() {
        if (mResumed && mScreenSurface != null) {
            mRenderingHandler.post(() -> {
                onResumeNative(mScreenSurface);

                // bootstrap the rendering loop
                mRenderingHandler.post(mRenderRunnable);
            });
        }
    }

    @Override
    protected void onPause() {
        maybePause();
        mResumed = false;

        super.onPause();
    }

    void maybePause() {
        // the check (mResumed && mScreenSurface != null) is intended: either mResumed or
        // mScreenSurface != null will be false after this method returns.
        if (mResumed && mScreenSurface != null) {
            mRenderingHandler.post(this::onPauseNative);
        }
    }

    private void render() {
        if (mResumed && mScreenSurface != null) {
            renderNative();

            mRenderingHandler.removeCallbacks(mRenderRunnable);
            mRenderingHandler.postDelayed(mRenderRunnable, 2);
        }
    }

    native void initializeNative();

    native void destroyNative();

    native void onResumeNative(Surface screenSurface);

    native void onPauseNative();

    native void onStreamStartNative();

    native void onStreamStopNative();

    native void renderNative();

    native void onBatteryChangedNative(int battery, boolean plugged);

    native void setLogFilePath(String path);

    // Load the full native dependency chain in the correct order. Loading each
    // Qiyu SDK lib explicitly means a failure is attributed to the exact library
    // (and its missing symbol), giving us a precise diagnosis on-device.
    private void loadNativeLibs() {
        // Qiyu VR SDK chain (bundled in jniLibs). Order matters: each may depend
        // on the ones listed before it.
        String[] qiyuChain = {
                "vrapi",
                "sxrapi",
                "qiyivrsdkcore",
                "ashreader",
                "qiyuapi",
        };
        for (String lib : qiyuChain) {
            try {
                System.loadLibrary(lib);
            } catch (Throwable t) {
                throw new RuntimeException("Failed while loading lib" + lib + ".so", t);
            }
        }
        // ALVR Rust core, then our bridge which depends on both.
        System.loadLibrary("alvr_client_core");
        System.loadLibrary("native_lib");
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

    // Render the error as a full-screen 2D surface. The Qiyu VR compositor shows a
    // 2D Android surface split across the two eyes (left eye -> left half of the
    // screen, right eye -> right half). To guarantee BOTH eyes can read the FULL
    // message, we draw the same text into the left and right halves of the screen.
    private void showErrorScreen(String msg) {
        writeCrashFile(msg);

        StringBuilder full = new StringBuilder();
        full.append("=== ALVR Qiyu native error ===\n");
        full.append("Screenshot this and send it. Do NOT close yet.\n\n");
        full.append(msg);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(0xFF000000);
        sv.setFillViewport(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(0xFF000000);

        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);

        root.addView(makeErrorText(full.toString()), half);
        root.addView(makeErrorText(full.toString()), half);

        sv.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(sv);
    }

    private TextView makeErrorText(String text) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextColor(0xFFFFFFFF);
        t.setBackgroundColor(0xFF000000);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        t.setTypeface(Typeface.MONOSPACE);
        t.setPadding(10, 10, 10, 10);
        t.setGravity(Gravity.TOP | Gravity.START);
        return t;
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
