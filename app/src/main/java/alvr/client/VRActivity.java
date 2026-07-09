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

    // Load the full native dependency chain in the correct order, using absolute
    // paths inside the app's extracted native lib dir. Loading each lib explicitly
    // (and libqiyuvrsdkcore's C++ runtime libc++_shared.so FIRST) means a failure is
    // attributed to the exact library, giving us a precise on-device diagnosis.
    private void loadNativeLibs() {
        // With android:extractNativeLibs="true" the libs are extracted here.
        String dir = getApplicationInfo().nativeLibraryDir;
        // Order matters: libc++_shared.so (C++ runtime the Qiyu prebuilts need)
        // must come before everything that depends on it.
        String[] libs = {
                "libc++_shared.so",
                "libvrapi.so",
                "libsxrapi.so",
                "libqiyuvrsdkcore.so",
                "libashreader.so",
                "libqiyuapi.so",
                "libalvr_client_core.so",
                "libnative_lib.so",
        };
        for (String lib : libs) {
            String path = dir + "/" + lib;
            try {
                System.load(path);
            } catch (Throwable t) {
                throw new RuntimeException("Failed to load native lib: " + path, t);
            }
        }
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
    // the middle band is visible (left/right/top/bottom edges are cropped). To stay
    // readable we show ONLY a short, large, centered headline with the key fact
    // (the missing library or symbol). The full text is saved to a file instead.
    private void showErrorScreen(String msg) {
        writeCrashFile(msg);

        TextView head = new TextView(this);
        head.setText(extractKeyFact(msg));
        head.setTextColor(0xFFFFFF00); // yellow, high contrast on black
        head.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        head.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        head.setGravity(Gravity.CENTER);
        head.setPadding(16, 16, 16, 16);

        TextView hint = new TextView(this);
        hint.setText("Send me this name (or screenshot). Full log saved to file.");
        hint.setTextColor(0xFFFFFFFF);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
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
    private String extractKeyFact(String msg) {
        Matcher m1 = Pattern.compile("library \"([^\"]+)\" not found").matcher(msg);
        if (m1.find()) return "MISSING LIBRARY:\n" + m1.group(1);
        Matcher m2 = Pattern.compile("cannot locate symbol \"([^\"]+)\"").matcher(msg);
        if (m2.find()) return "MISSING SYMBOL:\n" + m2.group(1);
        for (String line : msg.split("\n")) {
            line = line.trim();
            if (!line.isEmpty()) return line;
        }
        return "Unknown native load error";
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
