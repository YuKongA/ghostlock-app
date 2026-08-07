package com.ghostlock.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Insets;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.ErrnoException;
import android.system.Os;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public class MainActivity extends Activity {
    private static final String TAG = "GhostLockApp";
    private static final String BINARY_NAME = "libghostlock.so";
    private static final String KSUD_NAME = "ksud";
    private static final int COLOR_RED = 0xFFFF6B6B;
    private static final int COLOR_GREEN = 0xFF5FD68A;
    private static final int COLOR_YELLOW = 0xFFFFC94D;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final AtomicBoolean running = new AtomicBoolean(false);
    private final StringBuilder logBuffer = new StringBuilder();
    private TextView deviceInfo;
    private TextView statusInfo;
    private TextView logView;
    private View statusDot;
    private View statusChip;
    private LinearLayout kernelChip;
    private TextView kernelChipText;
    private ScrollView logScroll;
    private Button runButton;
    private Button copyButton;
    private View rootView;

    /**
     * Mirrors the offset tables under src/kernels: only these uname builds are supported.
     */
    private static boolean isKernelSupported() {
        String version = System.getProperty("os.version", "");
        for (String supported : SupportedKernels.UNAMES) {
            if (supported.equals(version)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Returns the value when it looks like a real device name, else null.
     */
    private static String validDeviceName(String value) {
        if (value == null) {
            return null;
        }
        String v = value.trim();
        if (v.isEmpty()) {
            return null;
        }
        String lower = v.toLowerCase(Locale.ROOT);
        if (lower.contains("unknown") || lower.contains("null")) {
            return null;
        }
        return v;
    }

    @SuppressLint("PrivateApi")
    private static String getSystemProperty(String key) {
        try {
            Class<?> props = Class.forName("android.os.SystemProperties");
            Object value = props.getMethod("get", String.class).invoke(null, key);
            return value instanceof String ? (String) value : "";
        } catch (Throwable ignored) {
            return "";
        }
    }

    /**
     * Color a whole log line by its leading marker. The native binary only
     * colors the "[..]" prefix (message text stays default) and the script log
     * is plain text, so per-line coloring is what makes the log readable.
     */
    private static CharSequence colorize(String line) {
        int color = markerColor(line);
        if (color == -1) {
            return line;
        }
        SpannableStringBuilder sb = new SpannableStringBuilder(line);
        sb.setSpan(new ForegroundColorSpan(color), 0, line.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        return sb;
    }

    private static int markerColor(String line) {
        if (line.startsWith("[+]")) return COLOR_GREEN;
        if (line.startsWith("[-]") || line.startsWith("[!]")) return COLOR_RED;
        if (line.startsWith("[*]")) return COLOR_YELLOW;
        if (line.startsWith("error") || line.startsWith("Error")) return COLOR_RED;
        if (line.startsWith("warning")) return COLOR_YELLOW;
        return -1;
    }

    private static String stripAnsi(String input) {
        return input.replaceAll("\u001B\\[[;\\d]*m", "");
    }

    private static void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) >= 0) {
            out.write(buf, 0, n);
        }
        out.flush();
    }

    private static void copyFile(File src, File dst) throws IOException {
        try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dst, false)) {
            copyStream(in, out);
        }
    }

    private static void setViewColor(View view, int color) {
        if (view.getBackground() instanceof GradientDrawable) {
            ((GradientDrawable) view.getBackground().mutate()).setColor(color);
        } else {
            view.setBackgroundTintList(ColorStateList.valueOf(color));
        }
    }

    private static String firstValidProperty(String... keys) {
        for (String key : keys) {
            String value = validDeviceName(getSystemProperty(key));
            if (value != null) {
                return value;
            }
        }
        return null;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        setupSystemBars();

        rootView = findViewById(R.id.root);
        deviceInfo = findViewById(R.id.deviceInfo);
        statusInfo = findViewById(R.id.statusInfo);
        statusDot = findViewById(R.id.statusDot);
        statusChip = findViewById(R.id.statusChip);
        logView = findViewById(R.id.logView);
        logScroll = findViewById(R.id.logScroll);
        runButton = findViewById(R.id.runButton);
        copyButton = findViewById(R.id.copyButton);
        kernelChip = findViewById(R.id.kernelChip);
        kernelChipText = findViewById(R.id.kernelChipText);

        applyWindowInsetsPadding();
        deviceInfo.setText(buildDeviceSummary());
        applyKernelStatus();
        setRunState(RunState.IDLE, getString(R.string.status_idle));

        runButton.setOnClickListener(v -> startExploit());
        copyButton.setOnClickListener(v -> copyLogs());
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    private void setupSystemBars() {
        Window window = getWindow();
        int barColor = getColor(R.color.status_bar);

        window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS | WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        window.setStatusBarColor(barColor);
        window.setNavigationBarColor(getColor(R.color.nav_bar));

        window.setStatusBarContrastEnforced(false);
        window.setNavigationBarContrastEnforced(false);

        WindowInsetsController controller = window.getInsetsController();
        if (controller != null) {
            int lightStatus = getResources().getBoolean(R.bool.window_light_status_bar) ? WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS : 0;
            int lightNav = getResources().getBoolean(R.bool.window_light_navigation_bar) ? WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS : 0;
            controller.setSystemBarsAppearance(lightStatus | lightNav, WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);
        }
    }

    private void applyWindowInsetsPadding() {
        rootView.setOnApplyWindowInsetsListener((v, insets) -> {
            int top;
            int bottom;
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
            top = bars.top;
            bottom = bars.bottom;
            int side = dp(20);
            v.setPadding(side, top + dp(12), side, bottom + dp(12));
            return insets;
        });
        rootView.requestApplyInsets();
    }

    private String buildDeviceSummary() {
        return getString(R.string.device_label) + ": " + resolveDeviceName() + "\n" + getString(R.string.kernel_label) + ": " + System.getProperty("os.version", "unknown");
    }

    private void applyKernelStatus() {
        boolean ok = isKernelSupported();
        int color = getColor(ok ? R.color.status_success : R.color.status_error);
        int bg = getColor(ok ? R.color.status_success_bg : R.color.status_error_bg);
        kernelChipText.setText(ok ? R.string.kernel_supported : R.string.kernel_unsupported);
        kernelChipText.setTextColor(color);
        setViewColor(kernelChip, bg);
    }

    /**
     * Sales/market name of the device, matching the language of the current region.
     */
    private String resolveDeviceName() {
        boolean cn = "CN".equalsIgnoreCase(Locale.getDefault().getCountry());
        String marketName = firstValidProperty(cn ? "ro.vendor.oplus.market.name" : "ro.vendor.oplus.market.enname", cn ? "ro.vendor.oplus.market.enname" : "ro.vendor.oplus.market.name", "ro.product.marketname");
        return marketName != null ? marketName : Build.MANUFACTURER + " " + Build.MODEL;
    }

    private void startExploit() {
        if (!running.compareAndSet(false, true)) {
            return;
        }
        setRunState(RunState.RUNNING, getString(R.string.status_running));
        // keep the screen awake
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        appendLog("==== start ====");
        worker.execute(() -> {
            int code = 1;
            try {
                File workDir = getFilesDir();
                File binary = resolveBinary();
                File ksud = prepareKsud(workDir);
                if (ksud != null) {
                    appendLog("ksud ready");
                } else {
                    appendLog("warning: ksud not found");
                }
                code = runBinary(binary, workDir);
                appendLog("exit code=" + code);
            } catch (Throwable t) {
                appendLog("error: " + t.getClass().getSimpleName() + ": " + t.getMessage());
            } finally {
                int finalCode = code;
                ui.post(() -> {
                    running.set(false);
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    if (finalCode == 0) {
                        setRunState(RunState.SUCCESS, getString(R.string.status_success));
                    } else {
                        setRunState(RunState.FAILED, getString(R.string.status_failed) + " (" + finalCode + ")");
                    }
                });
            }
        });
    }

    private void setRunState(RunState state, String text) {
        statusInfo.setText(text);
        runButton.setEnabled(state != RunState.RUNNING);
        runButton.setText(state == RunState.RUNNING ? R.string.action_running : R.string.action_run);

        int color;
        int chipBg = switch (state) {
            case RUNNING -> {
                color = getColor(R.color.status_running);
                yield getColor(R.color.status_running_bg);
            }
            case SUCCESS -> {
                color = getColor(R.color.status_success);
                yield getColor(R.color.status_success_bg);
            }
            case FAILED -> {
                color = getColor(R.color.status_error);
                yield getColor(R.color.status_error_bg);
            }
            default -> {
                color = getColor(R.color.status_idle);
                yield getColor(R.color.status_idle_bg);
            }
        };

        statusInfo.setTextColor(color);
        if (statusDot.getBackground() instanceof GradientDrawable) {
            ((GradientDrawable) statusDot.getBackground().mutate()).setColor(color);
        } else {
            statusDot.setBackgroundTintList(ColorStateList.valueOf(color));
        }

        if (statusChip.getBackground() instanceof GradientDrawable) {
            ((GradientDrawable) statusChip.getBackground().mutate()).setColor(chipBg);
        } else {
            statusChip.setBackgroundTintList(ColorStateList.valueOf(chipBg));
        }
    }

    private File resolveBinary() throws IOException {
        File packaged = new File(getApplicationInfo().nativeLibraryDir, BINARY_NAME);
        if (packaged.isFile()) {
            appendLog("binary ready (" + packaged.length() + " bytes)");
            return packaged;
        }
        throw new IOException("missing native binary: " + packaged.getAbsolutePath());
    }

    private File prepareKsud(File workDir) {
        File out = new File(workDir, KSUD_NAME);
        List<String> candidates = new ArrayList<>();
        try {
            ApplicationInfo appInfo = getPackageManager().getApplicationInfo("me.weishu.kernelsu", 0);
            candidates.add(new File(appInfo.nativeLibraryDir, "libksud.so").getAbsolutePath());
        } catch (PackageManager.NameNotFoundException ignored) {
            appendLog("KernelSU app not installed");
        }
        candidates.add("/data/local/tmp/ksud");
        candidates.add("/data/adb/ksu/bin/ksud");

        for (String path : candidates) {
            File src = new File(path);
            if (!src.isFile()) {
                continue;
            }
            try {
                copyFile(src, out);
                try {
                    Os.chmod(out.getAbsolutePath(), 448);
                } catch (ErrnoException ignored) {
                }
                return out;
            } catch (Throwable t) {
                appendLog("copy ksud failed: " + t.getMessage());
            }
        }
        if (out.isFile()) {
            return out;
        }
        return null;
    }

    private int runBinary(File binary, File workDir) throws IOException, InterruptedException {
        ProcessBuilder pb = new ProcessBuilder(binary.getAbsolutePath());
        pb.directory(workDir);
        pb.redirectErrorStream(true);
        pb.environment().put("GHOSTLOCK_HOME", workDir.getAbsolutePath());
        pb.environment().put("TMPDIR", workDir.getAbsolutePath());
        pb.environment().put("HOME", workDir.getAbsolutePath());

        Process process = pb.start();
        Thread reader = new Thread(() -> {
            try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = br.readLine()) != null) {
                    appendLog(line);
                }
            } catch (IOException ignored) {
            }
        }, "ghostlock-reader");
        reader.setDaemon(true);
        reader.start();

        boolean finished = process.waitFor(300, TimeUnit.SECONDS);
        if (!finished) {
            process.destroy();
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.destroyForcibly();
            }
        }
        // Drain buffered output, then force-unblock the reader: a late-load
        // daemon (zygisk) can inherit our pipe and keep it open forever.
        try {
            reader.join(3000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        try {
            process.getInputStream().close();
        } catch (IOException ignored) {
        }
        try {
            reader.join(3000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        appendKsudLogTail(workDir);
        return finished ? process.exitValue() : -1;
    }

    /**
     * Show any late-load log lines (root-owned, chmod 644) the binary could
     * not stream to us before it was killed by the vendor root guard.
     */
    private void appendKsudLogTail(File workDir) {
        File logFile = new File(workDir, ".ghostlock_ksu.log");
        if (!logFile.isFile()) {
            return;
        }
        List<String> lines = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(logFile))) {
            String line;
            while ((line = r.readLine()) != null) {
                lines.add(line);
            }
        } catch (IOException ignored) {
            return;
        }
        String joined;
        synchronized (logBuffer) {
            joined = logBuffer.toString();
        }
        for (String line : lines) {
            String t = line.trim();
            if (t.isEmpty() || joined.contains(t)) {
                continue;
            }
            appendLog(line);
        }
    }

    private void copyLogs() {
        ClipboardManager cm = getSystemService(ClipboardManager.class);
        if (cm == null) {
            return;
        }
        String text;
        synchronized (logBuffer) {
            text = logBuffer.toString();
        }
        cm.setPrimaryClip(ClipData.newPlainText("ghostlock-log", text));
        Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show();
    }

    private void appendLog(String line) {
        if (line == null) {
            return;
        }
        final String msg = line.endsWith("\n") ? line : line + "\n";
        final String plain = stripAnsi(msg);
        synchronized (logBuffer) {
            logBuffer.append(plain);
        }
        final CharSequence display = colorize(plain);
        ui.post(() -> {
            logView.append(display);
            logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
        });
        android.util.Log.i(TAG, plain.trim());
    }

    private int dp(int value) {
        float density = getResources().getDisplayMetrics().density;
        return Math.round(value * density);
    }

    private enum RunState {
        IDLE, RUNNING, SUCCESS, FAILED
    }
}
