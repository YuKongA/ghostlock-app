package com.ghostlock.app;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Insets;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.system.ErrnoException;
import android.system.Os;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MainActivity extends Activity {
    private static final String TAG = "GhostLockApp";
    private static final String BINARY_NAME = "libghostlock.so";
    private static final String EXTRACT_NAME = "libextract.so";
    private static final String KSUD_NAME = "ksud";
    private static final String OFFSETS_JSON = "offsets.json";
    private static final String KSU_LOG_NAME = ".ghostlock_ksu.log";
    /**
     * MediaTek DRAM-base marker emitted by the extractor when phys is not an override.
     */
    private static final long MTK_DEFAULT_PHYS_LOAD = 0x80000000L;
    private static final int REQ_IMPORT_OFFSETS = 1001;
    private static final int REQ_PICK_BOOT = 1002;
    private static final int REQ_PICK_XBL = 1003;
    private static final String PREFS = "ghostlock_prefs";
    private static final String PREF_CPU_PAIR = "cpu_pair";
    private static final String[] KSU_MANAGER_PACKAGES = {"me.weishu.kernelsu", "com.resukisu.resukisu", "com.kowx712.supermanager",};
    private static final int COLOR_RED = 0xFFFF6B6B;
    private static final int COLOR_GREEN = 0xFF5FD68A;
    private static final int COLOR_YELLOW = 0xFFFFC94D;
    private static final int COLOR_BLUE = 0xFF60A5FA;
    private static final int COLOR_NONE = -1;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final AtomicBoolean running = new AtomicBoolean(false);
    private final StringBuilder logBuffer = new StringBuilder();
    private final List<int[]> cpuPairs = new ArrayList<>();
    private final List<String> cpuPairLabels = new ArrayList<>();
    private final Map<View, ValueAnimator> viewAnimators = new HashMap<>();
    private TextView deviceInfo;
    private TextView logView;
    private LinearLayout kernelChip;
    private TextView kernelChipText;
    private Spinner cpuSpinner;
    private ScrollView logScroll;
    private Button runButton;
    private ImageButton advancedButton;
    private LinearLayout advancedPanel;
    private Button copyButton;
    private Button importButton;
    private Button otaButton;
    private Button parseButton;
    private Button exportButton;
    private View rootView;
    private int cpuPairIndex;
    private boolean parseWantsXbl;

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

    private static String readSysFile(String path) {
        File f = new File(path);
        if (!f.isFile()) {
            return "";
        }
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line = r.readLine();
            return line == null ? "" : line.trim();
        } catch (IOException ignored) {
            return "";
        }
    }

    private static List<Integer> parseCpuList(String s) {
        List<Integer> out = new ArrayList<>();
        if (s == null || s.isEmpty()) {
            return out;
        }
        for (String part : s.split(",")) {
            String[] range = part.split("-");
            try {
                int lo = Integer.parseInt(range[0].trim());
                int hi = range.length > 1 ? Integer.parseInt(range[1].trim()) : lo;
                for (int cpu = lo; cpu <= hi; cpu++) {
                    out.add(cpu);
                }
            } catch (NumberFormatException ignored) {
            }
        }
        return out;
    }

    private static long readMaxFreq(int cpu) {
        String v = readSysFile("/sys/devices/system/cpu/cpu" + cpu + "/cpufreq/cpuinfo_max_freq");
        if (v.isEmpty()) {
            return -1;
        }
        try {
            return Long.parseLong(v);
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static String formatFreq(long khz) {
        if (khz >= 1_000_000L) {
            return String.format(Locale.ROOT, "%.2f GHz", khz / 1_000_000.0);
        }
        return String.format(Locale.ROOT, "%.0f MHz", khz / 1000.0);
    }

    /**
     * Color a whole log line by its leading marker. The native binary only
     * colors the "[..]" prefix (message text stays default) and the script log
     * is plain text, so per-line coloring is what makes the log readable.
     */
    private static CharSequence colorize(String line) {
        int color = markerColor(line);
        if (color == COLOR_NONE) {
            return line;
        }
        SpannableStringBuilder sb = new SpannableStringBuilder(line);
        sb.setSpan(new ForegroundColorSpan(color), 0, line.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        return sb;
    }

    /**
     * Marker of the leading "[x] " tag, or 0 if the line has none.
     */
    private static char markerOf(String line) {
        return line.length() > 2 && line.charAt(0) == '[' && line.charAt(2) == ']' ? line.charAt(1) : 0;
    }

    /**
     * True for W1/W2/W3 stage round lines (progress or in-round failure).
     */
    private static boolean isWriteRound(String msg) {
        String stage = msg.startsWith("=== ") ? msg.substring(4) : msg;
        return stage.startsWith("W1") || stage.startsWith("W2") || stage.startsWith("W3") || stage.startsWith("Write 1");
    }

    private static int markerColor(String line) {
        char marker = markerOf(line);
        /* W-round progress renders blue; in-round failures keep red. */
        if (isWriteRound(logMessage(line))) {
            if (marker == '-' || marker == '!') return COLOR_RED;
            return COLOR_BLUE;
        }
        if (marker == '+') return COLOR_GREEN;
        if (marker == '-' || marker == '!') return COLOR_RED;
        if (marker == '*') return COLOR_YELLOW;
        if (line.startsWith("error") || line.startsWith("Error")) return COLOR_RED;
        if (line.startsWith("warning")) return COLOR_YELLOW;
        return COLOR_NONE;
    }

    /**
     * Strip leading "[..] " tags (marker, TIMER) and return the message.
     */
    private static String logMessage(String line) {
        String s = line;
        while (s.startsWith("[")) {
            int end = s.indexOf(']');
            if (end < 0) break;
            s = s.substring(end + 1);
            if (s.startsWith(" ")) s = s.substring(1);
        }
        return s;
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

    /**
     * True when `entry` matches the built-in table for its release.  Fields the
     * entry leaves null (extractor could not resolve them) are ignored: the
     * native side falls back to defaults for them, and the built-in macro
     * values equal those defaults.  A kernel_phys_load of 0x80000000 is the
     * MediaTek "no override" marker and is ignored as well.
     */
    static boolean matchesBuiltin(JSONObject entry) {
        Map<String, Long> builtin = SupportedKernels.BUILTIN.get(entry.optString("release", ""));
        if (builtin == null) {
            return false;
        }
        if (builtinFieldDiffers(builtin, entry, "pselect_waiter_shift")) {
            return false;
        }
        if (builtinFieldDiffers(builtin, entry, "compact_waiter")) {
            return false;
        }
        if (builtinFieldDiffers(builtin, entry, "mm_struct_sz")) {
            return false;
        }
        if (entry.has("kernel_phys_load") && !entry.isNull("kernel_phys_load")) {
            long phys = entry.optLong("kernel_phys_load", 0);
            if (phys != MTK_DEFAULT_PHYS_LOAD && builtinFieldDiffers(builtin, entry, "kernel_phys_load")) {
                return false;
            }
        }
        return !objectFieldDiffers(builtin, entry.optJSONObject("symbols")) && !objectFieldDiffers(builtin, entry.optJSONObject("struct_fields"));
    }

    static boolean builtinFieldDiffers(Map<String, Long> builtin, JSONObject entry, String key) {
        if (!entry.has(key) || entry.isNull(key)) {
            return false;
        }
        Long expected = builtin.get(key);
        return expected != null && entry.optLong(key, 0) != expected;
    }

    /**
     * True when any non-null field in `fields` differs from the built-in value.
     */
    static boolean objectFieldDiffers(Map<String, Long> builtin, JSONObject fields) {
        if (fields == null) {
            return false;
        }
        for (Iterator<String> it = fields.keys(); it.hasNext(); ) {
            String key = it.next();
            if (fields.isNull(key)) {
                continue;
            }
            Long expected = builtin.get(key);
            if (expected != null && fields.optLong(key, 0) != expected) {
                return true;
            }
        }
        return false;
    }

    /**
     * A kernel is supported when its release is in the built-in tables or in an
     * imported offsets.json, so new kernels work without rebuilding the app.
     */
    private boolean isKernelSupported() {
        String version = System.getProperty("os.version", "");
        for (String supported : SupportedKernels.UNAMES) {
            if (supported.equals(version)) {
                return true;
            }
        }
        return importedOffsetsMatch(version);
    }

    /**
     * True when <filesDir>/offsets.json contains the current release.
     */
    private boolean importedOffsetsMatch(String version) {
        File offsets = new File(getFilesDir(), OFFSETS_JSON);
        if (!offsets.isFile()) {
            return false;
        }
        try (BufferedReader reader = new BufferedReader(new FileReader(offsets))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append('\n');
            }
            Matcher matcher = Pattern.compile("\"release\"\\s*:\\s*\"([^\"]+)\"").matcher(sb);
            while (matcher.find()) {
                if (version.equals(matcher.group(1))) {
                    return true;
                }
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    private void buildCpuPairs() {
        cpuPairs.clear();
        cpuPairLabels.clear();
        List<Integer> online = parseCpuList(readSysFile("/sys/devices/system/cpu/online"));
        if (!online.isEmpty()) {
            Map<Long, List<Integer>> byFreq = new TreeMap<>(Collections.reverseOrder());
            for (int cpu : online) {
                long freq = readMaxFreq(cpu);
                if (freq > 0) {
                    byFreq.computeIfAbsent(freq, k -> new ArrayList<>()).add(cpu);
                }
            }
            for (Map.Entry<Long, List<Integer>> entry : byFreq.entrySet()) {
                List<Integer> cluster = entry.getValue();
                Collections.sort(cluster);
                String freqText = " · " + formatFreq(entry.getKey());
                for (int i = 0; i + 1 < cluster.size(); i += 2) {
                    int main = cluster.get(i);
                    int consumer = cluster.get(i + 1);
                    cpuPairs.add(new int[]{main, consumer});
                    cpuPairLabels.add(main + "," + consumer + freqText);
                }
            }
        }
        // Safe 0,1 fallback last: big cores are the default and listed first.
        boolean hasSafe = false;
        for (int[] pair : cpuPairs) {
            if (pair[0] == 0 && pair[1] == 1) {
                hasSafe = true;
                break;
            }
        }
        if (!hasSafe) {
            cpuPairs.add(new int[]{0, 1});
            long autoFreq = readMaxFreq(0);
            cpuPairLabels.add("0,1" + (autoFreq > 0 ? " · " + formatFreq(autoFreq) : ""));
        }
    }

    private void restoreCpuPair() {
        cpuPairIndex = 0;
        String saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_CPU_PAIR, null);
        if (saved == null || saved.equals("auto")) {
            // Default to the big-core pair (first entry); a legacy "auto"
            // preference is treated the same.  The native side falls back to
            // 0/1 when the pair is unavailable.
            return;
        }
        String[] parts = saved.split(",");
        if (parts.length != 2) {
            return;
        }
        try {
            int main = Integer.parseInt(parts[0].trim());
            int consumer = Integer.parseInt(parts[1].trim());
            for (int i = 0; i < cpuPairs.size(); i++) {
                int[] pair = cpuPairs.get(i);
                if (pair[0] == main && pair[1] == consumer) {
                    cpuPairIndex = i;
                    return;
                }
            }
        } catch (NumberFormatException ignored) {
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        setupSystemBars();

        rootView = findViewById(R.id.root);
        deviceInfo = findViewById(R.id.deviceInfo);
        logView = findViewById(R.id.logView);
        logScroll = findViewById(R.id.logScroll);
        runButton = findViewById(R.id.runButton);
        advancedButton = findViewById(R.id.advancedButton);
        advancedPanel = findViewById(R.id.advancedPanel);
        copyButton = findViewById(R.id.copyButton);
        importButton = findViewById(R.id.importButton);
        otaButton = findViewById(R.id.otaButton);
        parseButton = findViewById(R.id.parseButton);
        exportButton = findViewById(R.id.exportButton);
        kernelChip = findViewById(R.id.kernelChip);
        kernelChipText = findViewById(R.id.kernelChipText);
        cpuSpinner = findViewById(R.id.cpuSpinner);

        applyWindowInsetsPadding();
        deviceInfo.setText(buildDeviceSummary());
        buildCpuPairs();
        restoreCpuPair();
        applyKernelStatus();
        setRunState(RunState.IDLE);

        runButton.setOnClickListener(v -> startExploit());
        advancedButton.setOnClickListener(v -> {
            if (advancedPanel.getVisibility() == View.VISIBLE) {
                animateHide(advancedPanel);
            } else {
                animateShow(advancedPanel);
            }
        });
        copyButton.setOnClickListener(v -> copyLogs());
        importButton.setOnClickListener(v -> importOffsets());
        otaButton.setOnClickListener(v -> promptParseUrl());
        parseButton.setOnClickListener(v -> parseOffsets());
        exportButton.setOnClickListener(v -> exportOffsets());

        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.spinner_item_right, cpuPairLabels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        cpuSpinner.setAdapter(adapter);
        cpuSpinner.setSelection(cpuPairIndex);
        cpuSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                cpuPairIndex = position;
                int[] pair = cpuPairs.get(position);
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(PREF_CPU_PAIR, pair[0] + "," + pair[1]).apply();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    private void setupSystemBars() {
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller == null) {
            return;
        }
        int lightStatus = getResources().getBoolean(R.bool.window_light_status_bar) ? WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS : 0;
        int lightNav = getResources().getBoolean(R.bool.window_light_navigation_bar) ? WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS : 0;
        controller.setSystemBarsAppearance(lightStatus | lightNav, WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);
    }

    private void importOffsets() {
        pickDocument(REQ_IMPORT_OFFSETS);
    }

    /**
     * Export one kernel's offsets as a shareable offsets.json entry.  The
     * candidate list only covers entries that a recipient would not already
     * have: new releases plus built-in releases whose values were overwritten
     * with different data.  The current device release is listed first.
     */
    private void exportOffsets() {
        String current = System.getProperty("os.version", "");
        Map<String, JSONObject> byRelease;
        try {
            byRelease = exportableEntries();
        } catch (IOException e) {
            appendLog("export offsets failed: " + e.getMessage());
            toast(R.string.export_failed);
            return;
        }
        if (byRelease.isEmpty()) {
            toast(R.string.export_none);
            return;
        }
        List<String> releases = new ArrayList<>(byRelease.keySet());
        releases.sort(Comparator.comparing((String release) -> release.equals(current) ? 0 : 1).thenComparing(Comparator.naturalOrder()));
        String[] labels = new String[releases.size()];
        for (int i = 0; i < releases.size(); i++) {
            String release = releases.get(i);
            labels[i] = release.equals(current) ? getString(R.string.export_current_marker, release) : release;
        }
        new AlertDialog.Builder(this).setTitle(R.string.export_title).setItems(labels, (dialog, which) -> {
            String release = releases.get(which);
            JSONObject entry = byRelease.get(release);
            worker.execute(() -> shareOffsets(entry, release));
        }).setNegativeButton(R.string.cancel, null).show();
    }

    /**
     * Imported/parsed entries that a recipient would not already have: new
     * releases plus built-in releases whose values differ from the table.
     */
    private Map<String, JSONObject> exportableEntries() throws IOException {
        Map<String, JSONObject> byRelease = new LinkedHashMap<>();
        JSONArray imported = readOffsetsFile(new File(getFilesDir(), OFFSETS_JSON));
        if (imported != null) {
            for (int i = 0; i < imported.length(); i++) {
                JSONObject entry = imported.optJSONObject(i);
                if (entry == null) {
                    continue;
                }
                String release = entry.optString("release", "");
                if (release.isEmpty()) {
                    continue;
                }
                if (SupportedKernels.BUILTIN.containsKey(release) && matchesBuiltin(entry)) {
                    continue;
                }
                byRelease.putIfAbsent(release, entry);
            }
        }
        return byRelease;
    }

    /**
     * Show the export button only when there is data worth sharing.
     */
    private void updateExportButton() {
        boolean visible;
        try {
            visible = !exportableEntries().isEmpty();
        } catch (IOException e) {
            visible = false;
        }
        if (advancedPanel.getVisibility() != View.VISIBLE) {
            exportButton.setVisibility(visible ? View.VISIBLE : View.GONE);
            return;
        }
        if (visible && exportButton.getVisibility() != View.VISIBLE) {
            animateShow(exportButton);
        } else if (!visible && exportButton.getVisibility() == View.VISIBLE) {
            animateHide(exportButton);
        }
    }

    private void cancelViewAnimation(View view) {
        ValueAnimator running = viewAnimators.remove(view);
        if (running != null) {
            running.cancel();
        }
    }

    /**
     * Smoothly reveal a view: animate its height from 0 to the measured
     * content height while fading in.  The measure uses the parent width and
     * an unconstrained height, so padding and child margins are included in
     * the final size.
     */
    private void animateShow(View view) {
        cancelViewAnimation(view);
        final ViewGroup.LayoutParams lp = view.getLayoutParams();
        int width = view.getParent() instanceof View ? ((View) view.getParent()).getWidth() : 0;
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        final int target = Math.max(view.getMeasuredHeight(), 1);
        lp.height = 0;
        view.setVisibility(View.VISIBLE);
        view.setAlpha(0f);
        ValueAnimator anim = ValueAnimator.ofInt(0, target);
        anim.addUpdateListener(a -> {
            lp.height = (int) a.getAnimatedValue();
            view.setAlpha(a.getAnimatedFraction());
            view.requestLayout();
        });
        anim.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                lp.height = ViewGroup.LayoutParams.WRAP_CONTENT;
                view.setAlpha(1f);
                view.requestLayout();
            }
        });
        anim.setDuration(220);
        viewAnimators.put(view, anim);
        anim.start();
    }

    /**
     * Smoothly hide a view: animate its height to 0 while fading out.
     */
    private void animateHide(View view) {
        cancelViewAnimation(view);
        final ViewGroup.LayoutParams lp = view.getLayoutParams();
        final int start = view.getHeight();
        ValueAnimator anim = ValueAnimator.ofInt(start, 0);
        anim.addUpdateListener(a -> {
            lp.height = (int) a.getAnimatedValue();
            view.setAlpha(1f - a.getAnimatedFraction());
            view.requestLayout();
        });
        anim.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                view.setVisibility(View.GONE);
                lp.height = ViewGroup.LayoutParams.WRAP_CONTENT;
                view.setAlpha(1f);
            }
        });
        anim.setDuration(180);
        viewAnimators.put(view, anim);
        anim.start();
    }

    /**
     * Write one entry to Downloads and open the system share sheet.
     */
    private void shareOffsets(JSONObject entry, String release) {
        try {
            JSONArray payload = new JSONArray();
            payload.put(entry);
            String safeRelease = release.replaceAll("[^A-Za-z0-9._-]", "_");
            String name = "offsets-" + safeRelease + ".json";
            ContentValues values = new ContentValues();
            values.put(MediaStore.Downloads.DISPLAY_NAME, name);
            values.put(MediaStore.Downloads.MIME_TYPE, "application/json");
            Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                throw new IOException("cannot create download entry");
            }
            try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                if (out == null) {
                    throw new IOException("cannot open download entry");
                }
                out.write(payload.toString(2).getBytes(StandardCharsets.UTF_8));
            }
            appendLog("exported offsets: " + name);
            final Uri sharedUri = uri;
            ui.post(() -> {
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("application/json");
                send.putExtra(Intent.EXTRA_STREAM, sharedUri);
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(Intent.createChooser(send, getString(R.string.export_share)));
            });
        } catch (Throwable t) {
            appendLog("export offsets failed: " + t.getMessage());
            ui.post(() -> toast(R.string.export_failed));
        }
    }

    private void pickDocument(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, requestCode);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) {
            return;
        }
        if (requestCode == REQ_PICK_BOOT || requestCode == REQ_PICK_XBL) {
            handleParsePick(requestCode, data);
            return;
        }
        if (requestCode != REQ_IMPORT_OFFSETS) {
            return;
        }
        Uri uri = data.getData();
        if (uri == null) {
            toast(R.string.import_failed);
            return;
        }
        worker.execute(() -> {
            File target = new File(getFilesDir(), OFFSETS_JSON);
            try {
                File tmp = new File(getFilesDir(), "offsets_import.tmp");
                copyUriToFile(uri, tmp);
                JSONArray imported;
                try {
                    imported = readOffsetsFile(tmp);
                } finally {
                    tmp.delete();
                }
                if (imported == null) {
                    throw new IOException("not a valid offsets.json");
                }
                JSONArray existing = readOffsetsFile(target);
                if (existing == null) {
                    existing = new JSONArray();
                }
                final JSONArray existingEntries = existing;
                final JSONArray importedEntries = imported;
                List<String> skipped = new ArrayList<>();
                List<String> differ = new ArrayList<>();
                JSONArray fresh = filterBuiltin(importedEntries, skipped, differ);
                for (String release : skipped) {
                    appendLog("already built-in, skipped: " + release);
                }
                if (fresh.length() == 0) {
                    ui.post(() -> {
                        applyKernelStatus();
                        toast(R.string.offsets_already_exist);
                    });
                    return;
                }
                final JSONArray freshEntries = fresh;
                List<String> overlaps = overlappingReleases(existingEntries, freshEntries);
                List<String> replaced = new ArrayList<>(overlaps);
                for (String release : differ) {
                    if (!replaced.contains(release)) {
                        replaced.add(release);
                    }
                }
                if (replaced.isEmpty()) {
                    mergeAndSave(target, existingEntries, freshEntries, false);
                    ui.post(this::finishImport);
                } else {
                    ui.post(() -> promptOverwrite(replaced, existingEntries, freshEntries, target));
                }
            } catch (IOException e) {
                appendLog("import offsets failed: " + e.getMessage());
                ui.post(() -> {
                    applyKernelStatus();
                    toast(R.string.import_failed);
                });
            }
        });
    }

    private void parseOffsets() {
        new AlertDialog.Builder(this).setTitle(R.string.parse_title).setItems(new CharSequence[]{getString(R.string.parse_option_boot), getString(R.string.parse_option_boot_xbl),}, (dialog, which) -> {
            switch (which) {
                case 0:
                    pickParseBoot(false);
                    break;
                case 1:
                    pickParseBoot(true);
                    break;
            }
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private void promptParseUrl() {
        EditText input = new EditText(this);
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setHint(R.string.parse_url_hint);
        // Inset the input (underline included) from the dialog edges.
        int sidePadding = dp(14);
        LinearLayout container = new LinearLayout(this);
        container.setPadding(sidePadding, 0, sidePadding, 0);
        container.addView(input, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        new AlertDialog.Builder(this).setTitle(R.string.parse_url_title).setView(container).setPositiveButton(R.string.parse_start, (dialog, which) -> {
            String url = input.getText().toString().trim();
            if (url.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://"))) {
                appendLog("error: invalid OTA URL: " + url);
                toast(R.string.parse_failed_url);
                return;
            }
            appendLog("parse OTA: " + url);
            runExtract(url, null);
        }).setNegativeButton(R.string.cancel, null).show();
    }

    private void pickParseBoot(boolean withXbl) {
        parseWantsXbl = withXbl;
        if (withXbl) {
            toast(R.string.parse_pick_boot_hint);
        }
        pickDocument(REQ_PICK_BOOT);
    }

    private void handleParsePick(int requestCode, Intent data) {
        Uri uri = data.getData();
        if (uri == null) {
            return;
        }
        worker.execute(() -> {
            try {
                File workDir = getFilesDir();
                File boot = new File(workDir, "boot.img");
                if (requestCode == REQ_PICK_BOOT) {
                    copyUriToFile(uri, boot);
                    appendLog("boot.img ready: " + boot.getAbsolutePath());
                    if (parseWantsXbl) {
                        ui.post(() -> {
                            toast(R.string.parse_pick_xbl_hint);
                            pickDocument(REQ_PICK_XBL);
                        });
                    } else {
                        ui.post(() -> runExtract(boot.getAbsolutePath(), null));
                    }
                } else {
                    File xbl = new File(workDir, "xbl_config.img");
                    copyUriToFile(uri, xbl);
                    appendLog("xbl_config.img ready: " + xbl.getAbsolutePath());
                    ui.post(() -> runExtract(boot.getAbsolutePath(), xbl));
                }
            } catch (IOException e) {
                appendLog("parse error: " + e.getMessage());
            }
        });
    }

    private void copyUriToFile(Uri uri, File target) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(target, false)) {
            if (in == null) {
                throw new IOException("cannot open " + uri);
            }
            copyStream(in, out);
        }
    }

    /**
     * Parse an offsets file (single object or array) into an array of entries.
     */
    private JSONArray readOffsetsFile(File file) throws IOException {
        if (!file.isFile()) {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append('\n');
            }
        }
        if (sb.toString().trim().isEmpty()) {
            return null;
        }
        try {
            Object value = new JSONTokener(sb.toString()).nextValue();
            if (value instanceof JSONArray) {
                return (JSONArray) value;
            }
            if (value instanceof JSONObject) {
                JSONArray arr = new JSONArray();
                arr.put(value);
                return arr;
            }
        } catch (JSONException ignored) {
        }
        return null;
    }

    private Set<String> importedReleases(JSONArray entries) {
        Set<String> releases = new HashSet<>();
        for (int i = 0; i < entries.length(); i++) {
            JSONObject entry = entries.optJSONObject(i);
            if (entry != null) {
                releases.add(entry.optString("release", ""));
            }
        }
        return releases;
    }

    /**
     * Releases already present in the store that the import also contains.
     */
    private List<String> overlappingReleases(JSONArray existing, JSONArray imported) {
        Set<String> known = importedReleases(existing);
        List<String> overlaps = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (int i = 0; i < imported.length(); i++) {
            JSONObject entry = imported.optJSONObject(i);
            if (entry == null) {
                continue;
            }
            String release = entry.optString("release", "");
            if (!release.isEmpty() && known.contains(release) && seen.add(release)) {
                overlaps.add(release);
            }
        }
        return overlaps;
    }

    /**
     * Drop entries the built-in tables already cover with identical values.
     * `skipped` receives those releases (nothing to store), `differ` the
     * built-in releases whose values differ (overwriting them is optional).
     */
    private JSONArray filterBuiltin(JSONArray imported, List<String> skipped, List<String> differ) {
        JSONArray fresh = new JSONArray();
        for (int i = 0; i < imported.length(); i++) {
            JSONObject entry = imported.optJSONObject(i);
            if (entry == null) {
                continue;
            }
            String release = entry.optString("release", "");
            if (!SupportedKernels.BUILTIN.containsKey(release)) {
                fresh.put(entry);
                continue;
            }
            if (matchesBuiltin(entry)) {
                skipped.add(release);
            } else {
                differ.add(release);
                fresh.put(entry);
            }
        }
        return fresh;
    }

    /**
     * Merge `imported` into `existing` and write the result back.  With
     * overwrite=true imported entries replace stored entries with the same
     * release; otherwise the stored values are kept.
     */
    private void mergeAndSave(File target, JSONArray existing, JSONArray imported, boolean overwrite) throws IOException {
        Map<String, JSONObject> importedByRelease = new HashMap<>();
        for (int i = 0; i < imported.length(); i++) {
            JSONObject entry = imported.optJSONObject(i);
            if (entry != null) {
                importedByRelease.put(entry.optString("release", ""), entry);
            }
        }
        Set<String> known = importedReleases(existing);
        JSONArray merged = new JSONArray();
        for (int i = 0; i < existing.length(); i++) {
            JSONObject entry = existing.optJSONObject(i);
            if (entry == null) {
                continue;
            }
            if (overwrite && importedByRelease.containsKey(entry.optString("release", ""))) {
                continue;
            }
            merged.put(entry);
        }
        for (int i = 0; i < imported.length(); i++) {
            JSONObject entry = imported.optJSONObject(i);
            if (entry == null) {
                continue;
            }
            if (!overwrite && known.contains(entry.optString("release", ""))) {
                continue;
            }
            merged.put(entry);
        }
        try (OutputStream out = new FileOutputStream(target, false)) {
            out.write(merged.toString(2).getBytes(StandardCharsets.UTF_8));
        } catch (JSONException e) {
            throw new IOException("serialize offsets failed", e);
        }
    }

    /**
     * Ask the user whether stored or built-in offsets for these kernels should
     * be replaced by the imported values.
     */
    private void promptOverwrite(List<String> replaced, JSONArray existing, JSONArray imported, File target) {
        new AlertDialog.Builder(this).setTitle(R.string.overwrite_title).setMessage(getString(R.string.overwrite_message, String.join("\n", replaced))).setPositiveButton(R.string.overwrite_yes, (dialog, which) -> worker.execute(() -> {
            try {
                mergeAndSave(target, existing, imported, true);
                ui.post(this::finishImport);
            } catch (IOException e) {
                appendLog("import offsets failed: " + e.getMessage());
                ui.post(() -> {
                    applyKernelStatus();
                    toast(R.string.import_failed);
                });
            }
        })).setNegativeButton(R.string.cancel, null).show();
    }

    /**
     * Ask whether parsed offsets should replace built-in tables for kernels
     * whose values differ from the built-in ones.
     */
    private void promptParseOverwrite(List<String> differ, JSONArray existing, JSONArray fresh, File target) {
        new AlertDialog.Builder(this).setTitle(R.string.overwrite_title).setMessage(getString(R.string.overwrite_message, String.join("\n", differ))).setPositiveButton(R.string.overwrite_yes, (dialog, which) -> worker.execute(() -> {
            try {
                mergeAndSave(target, existing, fresh, true);
                ui.post(() -> {
                    applyKernelStatus();
                    toast(R.string.parse_success);
                    appendLog("offsets.json written: " + target.getAbsolutePath());
                });
            } catch (IOException e) {
                appendLog("merge offsets failed: " + e.getMessage());
                ui.post(() -> {
                    applyKernelStatus();
                    toast(R.string.parse_failed);
                });
            }
        })).setNegativeButton(R.string.cancel, null).show();
    }

    private void finishImport() {
        applyKernelStatus();
        toast(R.string.import_success);
        appendLog("offsets.json imported: " + new File(getFilesDir(), OFFSETS_JSON).getAbsolutePath());
    }

    /**
     * Run the extractor on an OTA URL or local boot/xbl files; writes offsets.json on success.
     */
    private void runExtract(String input, File xblFile) {
        worker.execute(() -> {
            int code = 1;
            try {
                File binary = new File(getApplicationInfo().nativeLibraryDir, EXTRACT_NAME);
                if (!binary.isFile()) {
                    throw new IOException("missing native binary: " + binary.getAbsolutePath());
                }
                File workDir = getFilesDir();
                List<String> args = new ArrayList<>();
                args.add(input);
                if (xblFile != null) {
                    args.add("--xbl-config");
                    args.add(xblFile.getAbsolutePath());
                }
                args.add("--format");
                args.add("json");
                args.add("--out");
                args.add(new File(workDir, "offsets_parse.tmp").getAbsolutePath());
                args.add("--work-dir");
                args.add(workDir.getAbsolutePath());
                appendLog("extract: " + input);
                code = runExtractProcess(binary, args, workDir);
                appendLog("extract exit code=" + code);
                File offsets = new File(workDir, OFFSETS_JSON);
                File parsed = new File(workDir, "offsets_parse.tmp");
                boolean ok = code == 0 && parsed.isFile();
                Runnable uiResult = null;
                try {
                    if (ok) {
                        JSONArray imported = readOffsetsFile(parsed);
                        if (imported == null) {
                            ok = false;
                        } else {
                            JSONArray existing = readOffsetsFile(offsets);
                            if (existing == null) {
                                existing = new JSONArray();
                            }
                            List<String> skipped = new ArrayList<>();
                            List<String> differ = new ArrayList<>();
                            JSONArray fresh = filterBuiltin(imported, skipped, differ);
                            for (String release : skipped) {
                                appendLog("already built-in, skipped: " + release);
                            }
                            if (fresh.length() == 0) {
                                uiResult = () -> toast(R.string.offsets_already_exist);
                            } else if (differ.isEmpty()) {
                                // Parsing this kernel is explicit: replace any
                                // stored entry with the same release.
                                mergeAndSave(offsets, existing, fresh, true);
                                uiResult = () -> {
                                    toast(R.string.parse_success);
                                    appendLog("offsets.json written: " + offsets.getAbsolutePath());
                                };
                            } else {
                                final JSONArray existingFinal = existing;
                                final JSONArray freshFinal = fresh;
                                uiResult = () -> promptParseOverwrite(differ, existingFinal, freshFinal, offsets);
                            }
                        }
                    }
                } catch (IOException e) {
                    appendLog("merge offsets failed: " + e.getMessage());
                    ok = false;
                } finally {
                    parsed.delete();
                }
                final int extractCode = code;
                final Runnable result = uiResult;
                ui.post(() -> {
                    applyKernelStatus();
                    if (result != null) {
                        result.run();
                    } else {
                        toast(parseFailureToast(extractCode));
                    }
                });
            } catch (Throwable t) {
                appendLog("error: " + t.getClass().getSimpleName() + ": " + t.getMessage());
                ui.post(() -> toast(R.string.parse_failed));
            }
        });
    }

    /**
     * Map an extractor exit code to the user-facing failure message.
     * 3: pselect route infeasible, 4: missing required offsets, 5: kallsyms
     * recovery failure; -1 means the process was killed on timeout.
     */
    private int parseFailureToast(int code) {
        return switch (code) {
            case 3, 4 -> R.string.parse_failed_route;
            case 5 -> R.string.parse_failed_kallsyms;
            case -1 -> R.string.parse_timeout;
            default -> R.string.parse_failed;
        };
    }

    private int runExtractProcess(File binary, List<String> args, File workDir) throws IOException, InterruptedException {
        // The extractor recovers kallsyms from the image itself; no root needed.
        List<String> cmd = new ArrayList<>();
        cmd.add(binary.getAbsolutePath());
        cmd.addAll(args);
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(workDir);
        pb.redirectErrorStream(true);
        pb.environment().put("GHOSTLOCK_HOME", workDir.getAbsolutePath());
        pb.environment().put("TMPDIR", workDir.getAbsolutePath());
        pb.environment().put("HOME", workDir.getAbsolutePath());
        // Payload downloads and on-device analysis can take a while.
        return runProcess(pb, 1800);
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
        updateExportButton();
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
        setRunState(RunState.RUNNING);
        // keep the screen awake
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        appendLog("==== start ====");
        appendLog("cpu pair: " + cpuPairLabels.get(cpuPairIndex));
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
                File logFile = new File(workDir, KSU_LOG_NAME);
                logFile.delete();
                AtomicLong ksuOffset = new AtomicLong();
                Thread tailer = new Thread(() -> {
                    try {
                        while (!Thread.currentThread().isInterrupted()) {
                            tailKsuLog(logFile, ksuOffset);
                            Thread.sleep(200);
                        }
                    } catch (InterruptedException ignored) {
                    }
                }, "ksu-log-tailer");
                tailer.setDaemon(true);
                tailer.start();
                code = runBinary(binary, workDir);
                tailer.interrupt();
                try {
                    tailer.join(1000);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
                tailKsuLog(logFile, ksuOffset);
                appendLog("exit code=" + code);
            } catch (Throwable t) {
                appendLog("error: " + t.getClass().getSimpleName() + ": " + t.getMessage());
            } finally {
                int finalCode = code;
                ui.post(() -> {
                    running.set(false);
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    if (finalCode == 0) {
                        setRunState(RunState.SUCCESS);
                    } else {
                        setRunState(RunState.FAILED);
                    }
                });
            }
        });
    }

    private void setRunState(RunState state) {
        runButton.setEnabled(state != RunState.RUNNING);
        runButton.setText(state == RunState.RUNNING ? R.string.action_running : R.string.action_run);
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
        boolean anyInstalled = false;
        for (String pkg : KSU_MANAGER_PACKAGES) {
            ApplicationInfo appInfo;
            try {
                appInfo = getPackageManager().getApplicationInfo(pkg, 0);
            } catch (PackageManager.NameNotFoundException ignored) {
                continue;
            }
            anyInstalled = true;
            File src = new File(appInfo.nativeLibraryDir, "libksud.so");
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
        if (!anyInstalled) {
            appendLog("KernelSU/ReSukiSU app not installed");
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
        int[] pair = cpuPairs.get(cpuPairIndex);
        if (pair[0] != 0 || pair[1] != 1) {
            pb.environment().put("GHOSTLOCK_CORE", String.valueOf(pair[0]));
            pb.environment().put("GHOSTLOCK_CONSUMER_CORE", String.valueOf(pair[1]));
        }
        return runProcess(pb);
    }

    private void tailKsuLog(File logFile, AtomicLong offset) {
        if (!logFile.isFile()) {
            return;
        }
        synchronized (offset) {
            try (RandomAccessFile raf = new RandomAccessFile(logFile, "r")) {
                long size = raf.length();
                long pos = offset.get();
                if (size < pos) {
                    pos = 0; // log was truncated/recreated by this run
                }
                raf.seek(pos);
                long lastComplete = pos;
                StringBuilder pending = new StringBuilder(512);
                int b;
                while ((b = raf.read()) != -1) {
                    if (b == '\n') {
                        if (pending.length() > 0) {
                            appendLog(pending.toString());
                            pending.setLength(0);
                        }
                        lastComplete = raf.getFilePointer();
                    } else {
                        pending.append((char) b);
                    }
                }
                offset.set(lastComplete);
            } catch (IOException ignored) {
            }
        }
    }

    private int runProcess(ProcessBuilder pb) throws IOException, InterruptedException {
        return runProcess(pb, 300);
    }

    private int runProcess(ProcessBuilder pb, long timeoutSeconds) throws IOException, InterruptedException {
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

        boolean finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS);
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

        return finished ? process.exitValue() : -1;
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

    private void toast(int resId) {
        Toast.makeText(this, resId, Toast.LENGTH_SHORT).show();
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
