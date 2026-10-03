package df.root;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import df.root.databinding.ActivityMainBinding;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MainActivity extends AppCompatActivity implements IReporter {

    private static final String TAG = "dfroot";
    private static final int REQUEST_SELECT_KSUD = 4108;
    private static final int REQUEST_SELECT_KO = 4109;
    private static final long MAX_PAYLOAD_BYTES = 64L * 1024L * 1024L;
    private static final Pattern KMI_PATTERN = Pattern.compile("^(\\d+)\\.(\\d+).*?(android\\d+)");
    private static final Pattern KERNEL_VERSION_PATTERN = Pattern.compile("^(\\d+)\\.(\\d+)");
    private static final Pattern KMI_VERSION_PATTERN = Pattern.compile("^android\\d+-(\\d+\\.\\d+)$");
    private static final String PREF_SELECTED_KO = ExploitRunner.PREF_SELECTED_BUNDLED_KO_KMI;
    private static final String AUTO_KO = ExploitRunner.AUTO_BUNDLED_KO;
    private static final String[] BUNDLED_KMIS = {
            "android12-5.10", "android13-5.10", "android13-5.15", "android14-5.15",
            "android14-6.1", "android15-6.6", "android16-6.12", "android17-6.18"
    };

    private ActivityMainBinding binding;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private final Executor mExec = Executors.newSingleThreadExecutor();

    @Override
    public void report(String msg) {
        Log.i(TAG, msg.trim());
        mMain.post(() -> {
            boolean followTail = !binding.outputScroll.canScrollVertically(1);
            binding.outputView.append(msg);
            if (followTail) {
                binding.outputScroll.post(() -> binding.outputScroll.fullScroll(View.FOCUS_DOWN));
            }
        });
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        setSupportActionBar(binding.toolbar);

        // Keep long-press selection enabled for copying log lines. LogScrollView
        // separates a real vertical drag from a long press so the outer page
        // does not steal the gesture.
        binding.outputScroll.setNestedScrollingEnabled(false);

        updateDeviceProfile();
        updateKsudMode();
        updateKoMode();
        updateVendorTargetsUi();
        binding.btnSelectKsud.setOnClickListener(v -> selectPayload(REQUEST_SELECT_KSUD));
        binding.btnUseManagerKsud.setOnClickListener(v -> useManagerKsud());
        binding.btnSelectKo.setOnClickListener(v -> selectPayload(REQUEST_SELECT_KO));
        binding.btnUseBundledKo.setOnClickListener(v -> useBundledKo());
        binding.btnSaveVendorTargets.setOnClickListener(v -> saveVendorTargets());
        binding.btnResetVendorTargets.setOnClickListener(v -> resetVendorTargets());
        binding.koSelection.setOnItemClickListener((parent, view, position, id) ->
                onKoSelection(position));
        binding.githubLink.setOnClickListener(v -> openGithub());
        binding.bottomNavigation.setOnItemSelectedListener(item -> {
            if (item.getItemId() == R.id.nav_settings) {
                binding.homeScroll.setVisibility(View.GONE);
                binding.settingsScroll.setVisibility(View.VISIBLE);
                return true;
            }
            if (item.getItemId() == R.id.nav_home) {
                binding.settingsScroll.setVisibility(View.GONE);
                binding.homeScroll.setVisibility(View.VISIBLE);
                return true;
            }
            return false;
        });

        binding.btnRun.setOnClickListener(v -> {
            binding.btnRun.setEnabled(false);
            binding.outputView.setText("");
            mExec.execute(this::runExploit);
        });
    }

    private void selectPayload(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, requestCode);
        } catch (Exception e) {
            Toast.makeText(this, "No file picker is available", Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if ((requestCode != REQUEST_SELECT_KSUD && requestCode != REQUEST_SELECT_KO)
                || resultCode != RESULT_OK || data == null || data.getData() == null) return;

        try {
            if (requestCode == REQUEST_SELECT_KSUD) importCustomKsud(data.getData());
            else importCustomKo(data.getData());
        } catch (Exception e) {
            Log.e(TAG, "payload import failed", e);
            Toast.makeText(this, "Could not use that file", Toast.LENGTH_LONG).show();
            report("payload import failed: " + e + "\n");
        }
    }

    private long copyToTemp(Uri source, File temp) throws IOException {
        long copied = 0;
        try (InputStream in = getContentResolver().openInputStream(source);
             FileOutputStream out = new FileOutputStream(temp)) {
            if (in == null) throw new IOException("file provider returned no data");
            byte[] buffer = new byte[8192];
            for (int n; (n = in.read(buffer)) != -1; ) {
                copied += n;
                if (copied > MAX_PAYLOAD_BYTES) throw new IOException("file is over 64 MiB");
                out.write(buffer, 0, n);
            }
        } catch (IOException e) {
            temp.delete();
            throw e;
        }
        if (copied == 0) {
            temp.delete();
            throw new IOException("selected file is empty");
        }
        return copied;
    }

    private static void replaceFile(File temp, File destination) throws IOException {
        if (destination.exists() && !destination.delete()) {
            temp.delete();
            throw new IOException("could not replace the previous file");
        }
        if (!temp.renameTo(destination)) {
            temp.delete();
            throw new IOException("could not stage selected file");
        }
    }

    private void importCustomKsud(Uri source) throws IOException {
        File dataDir = appDataDir();
        if (!dataDir.isDirectory() && !dataDir.mkdirs())
            throw new IOException("could not create app data directory");

        File customKsud = new File(dataDir, "ksud.custom");
        File tempKsud = new File(dataDir, "ksud.custom.tmp");
        copyToTemp(source, tempKsud);
        if (!isArm64Executable(tempKsud)) {
            tempKsud.delete();
            throw new IOException("choose an arm64 Android ksud executable or shell wrapper");
        }
        if (!tempKsud.setExecutable(true, false)) {
            tempKsud.delete();
            throw new IOException("could not mark ksud executable");
        }
        replaceFile(tempKsud, customKsud);

        String label = source.getLastPathSegment();
        if (label == null || label.isEmpty()) label = "custom file";
        getSharedPreferences("dfroot", MODE_PRIVATE)
                .edit().putString("custom_ksud_label", label).apply();
        updateKsudMode();
        report("custom ksud selected: " + label + "\n");
    }

    private void importCustomKo(Uri source) throws IOException {
        File dataDir = appDataDir();
        if (!dataDir.isDirectory() && !dataDir.mkdirs())
            throw new IOException("could not create app data directory");

        File customKo = ExploitRunner.customKoFile(this);
        File tempKo = new File(dataDir, "dirtyfrag.custom.ko.tmp");
        copyToTemp(source, tempKo);
        String label = displayName(source);
        if (!isDfRootKo(tempKo)) {
            tempKo.delete();
            throw new IOException("choose an arm64 DFRoot dirtyfrag.ko for kernel " + currentKmiLabel()
                    + " with matching kernel vermagic");
        }
        replaceFile(tempKo, customKo);

        getSharedPreferences("dfroot", MODE_PRIVATE)
                .edit().putString("custom_ko_label", label)
                .putString(PREF_SELECTED_KO, "custom").apply();
        updateKoMode();
        report("manual DFRoot module selected: " + label + " (kernel " + currentKmiLabel() + ")\n");
    }

    private static boolean isArm64Executable(File file) throws IOException {
        byte[] header = readHeader(file, 20);
        boolean arm64Elf = (header[0] & 0xff) == 0x7f
                && header[1] == 'E' && header[2] == 'L' && header[3] == 'F'
                && header[4] == 2 && header[5] == 1
                && (header[18] & 0xff) == 0xb7 && header[19] == 0;
        return arm64Elf || (header[0] == '#' && header[1] == '!');
    }

    private boolean isDfRootKo(File file) throws IOException {
        byte[] header = readHeader(file, 20);
        boolean arm64Relocatable = (header[0] & 0xff) == 0x7f
                && header[1] == 'E' && header[2] == 'L' && header[3] == 'F'
                && header[4] == 2 && header[5] == 1
                && header[16] == 1 && header[17] == 0
                && (header[18] & 0xff) == 0xb7 && header[19] == 0;
        String kernelVersion = currentKernelVersion();
        String kernelVermagicPrefix = kernelVersion == null
                ? null : "vermagic=" + kernelVersion + ".";
        return arm64Relocatable
                && containsAscii(file, "name=dirtyfrag")
                && containsAscii(file, "description=DFRoot LKM")
                && kernelVermagicPrefix != null
                && containsAscii(file, kernelVermagicPrefix);
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) {
                    String value = cursor.getString(column);
                    if (value != null && !value.isEmpty()) return value;
                }
            }
        } catch (Exception ignored) { }
        String fallback = uri.getLastPathSegment();
        return fallback == null || fallback.isEmpty() ? "custom module" : fallback;
    }

    private static byte[] readHeader(File file, int length) throws IOException {
        byte[] header = new byte[length];
        try (InputStream in = new FileInputStream(file)) {
            int offset = 0;
            while (offset < header.length) {
                int count = in.read(header, offset, header.length - offset);
                if (count < 0) throw new IOException("file is too short");
                offset += count;
            }
        }
        return header;
    }

    private static boolean containsAscii(File file, String text) throws IOException {
        byte[] needle = text.getBytes(StandardCharsets.US_ASCII);
        byte[] buffer = new byte[8192 + needle.length];
        int carry = 0;
        try (InputStream in = new FileInputStream(file)) {
            for (int read; (read = in.read(buffer, carry, 8192)) != -1; ) {
                int length = carry + read;
                for (int i = 0; i <= length - needle.length; i++) {
                    int j = 0;
                    while (j < needle.length && buffer[i + j] == needle[j]) j++;
                    if (j == needle.length) return true;
                }
                carry = Math.min(needle.length - 1, length);
                System.arraycopy(buffer, length - carry, buffer, 0, carry);
            }
        }
        return false;
    }

    private void useManagerKsud() {
        File customKsud = new File(appDataDir(), "ksud.custom");
        if (customKsud.exists() && !customKsud.delete()) {
            Toast.makeText(this, "Could not remove the custom ksud file", Toast.LENGTH_LONG).show();
            return;
        }
        getSharedPreferences("dfroot", MODE_PRIVATE).edit().remove("custom_ksud_label").apply();
        updateKsudMode();
        report("using automatic manager ksud selection (default)\n");
    }

    private void useBundledKo() {
        File customKo = ExploitRunner.customKoFile(this);
        if (customKo.exists() && !customKo.delete()) {
            Toast.makeText(this, "Could not remove the selected module", Toast.LENGTH_LONG).show();
            return;
        }
        getSharedPreferences("dfroot", MODE_PRIVATE).edit()
                .remove("custom_ko_label").putString(PREF_SELECTED_KO, AUTO_KO).apply();
        updateKoMode();
        report("using bundled DirtyFrag module selected by KMI (default)\n");
    }

    private void onKoSelection(int position) {
        File customKo = ExploitRunner.customKoFile(this);
        int customPosition = BUNDLED_KMIS.length + 1;
        if (customKo.isFile() && position == customPosition) return;

        String selectedKmi = position == 0 ? AUTO_KO : BUNDLED_KMIS[position - 1];
        if (!AUTO_KO.equals(selectedKmi) && !sameKernelVersion(selectedKmi, currentKmi())) {
            Toast.makeText(this, "That module targets a different kernel version. Selection unchanged.",
                    Toast.LENGTH_LONG).show();
            updateKoMode();
            return;
        }

        if (customKo.exists() && !customKo.delete()) {
            Toast.makeText(this, "Could not remove the selected custom module", Toast.LENGTH_LONG).show();
            updateKoMode();
            return;
        }
        getSharedPreferences("dfroot", MODE_PRIVATE).edit()
                .remove("custom_ko_label").putString(PREF_SELECTED_KO, selectedKmi).apply();
        updateKoMode();
        String actualKmi = AUTO_KO.equals(selectedKmi) ? resolvedAutoKmi() : selectedKmi;
        report(actualKmi == null
                ? "bundled module selection: no match for current kernel KMI " + currentKmiLabel() + "\n"
                : "bundled module selection: dirtyfrag-" + actualKmi + ".ko\n");
    }

    private void updateKsudMode() {
        File customKsud = new File(appDataDir(), "ksud.custom");
        String label = getSharedPreferences("dfroot", MODE_PRIVATE)
                .getString("custom_ksud_label", "custom file");
        binding.ksudMode.setText(customKsud.isFile()
                ? "Loader: custom ksud (" + label + ")"
                : "Loader: auto (KernelSU Manager → ReSukiSU Manager → system ksud)");
    }

    private void updateVendorTargetsUi() {
        String targets = ExploitRunner.getVendorTargets(this);
        binding.vendorTargetsInput.setText(targets);
        int count = targets.isEmpty() ? 0 : targets.split("\\R").length;
        binding.vendorTargetsStatus.setText(count + " target(s) configured; tried from top to bottom.");
    }

    private void saveVendorTargets() {
        String raw = binding.vendorTargetsInput.getText() == null
                ? "" : binding.vendorTargetsInput.getText().toString();
        String error = ExploitRunner.validateVendorTargets(raw);
        if (error != null) {
            binding.vendorTargetsLayout.setError(error);
            return;
        }
        String normalized = ExploitRunner.normalizeVendorTargets(raw);
        getSharedPreferences("dfroot", MODE_PRIVATE).edit()
                .putString(ExploitRunner.PREF_VENDOR_TARGETS, normalized).apply();
        binding.vendorTargetsLayout.setError(null);
        updateVendorTargetsUi();
        report("vendor target profile saved; it will be used on the next run\n");
        Toast.makeText(this, "Vendor target profile saved", Toast.LENGTH_SHORT).show();
    }

    private void resetVendorTargets() {
        getSharedPreferences("dfroot", MODE_PRIVATE).edit()
                .remove(ExploitRunner.PREF_VENDOR_TARGETS).apply();
        binding.vendorTargetsLayout.setError(null);
        updateVendorTargetsUi();
        report("vendor target profile reset to defaults\n");
        Toast.makeText(this, "Default vendor targets restored", Toast.LENGTH_SHORT).show();
    }

    private void updateKoMode() {
        File customKo = ExploitRunner.customKoFile(this);
        String label = getSharedPreferences("dfroot", MODE_PRIVATE)
                .getString("custom_ko_label", "custom module");
        if (customKo.isFile()) {
            binding.koMode.setText("Selected: custom DFRoot helper · " + label);
            binding.koGuidance.setText("Using your selected helper. For another kernel, build a DFRoot dirtyfrag.ko for that device's KMI, then select the file here.");
        } else {
            String selection = normalizedKoSelection();
            boolean automatic = AUTO_KO.equals(selection);
            String actualKmi = automatic ? resolvedAutoKmi() : selection;
            if (actualKmi == null) {
                binding.koMode.setText("No automatic bundle for KMI " + currentKmiLabel());
                binding.koGuidance.setText("This kernel does not expose an Android KMI in uname. Choose a matching arm64 DFRoot .ko manually, or build one for this kernel.");
            } else if (automatic && !actualKmi.equals(currentKmi())) {
                binding.koMode.setText("Selected: dirtyfrag-" + actualKmi + ".ko · fallback for " + currentKmiLabel());
                binding.koGuidance.setText("There is no exact bundled KMI match. Automatic mode uses this same-major/minor fallback; compatibility is unverified. Build an exact-KMI DFRoot helper and select it if needed.");
            } else {
                binding.koMode.setText("Selected: dirtyfrag-" + actualKmi + ".ko"
                        + (automatic ? " · automatic for " + currentKmi() : " · manual KMI " + actualKmi));
                binding.koGuidance.setText("Automatic mode uses the detected KMI. You can choose another bundled variant with the same kernel major/minor, or build and select a custom DFRoot helper. KMI alone does not prove full compatibility.");
            }
        }
        updateKoSelector();
        updateRunAvailability();
    }

    private void updateKoSelector() {
        File customKo = ExploitRunner.customKoFile(this);
        String autoKmi = resolvedAutoKmi();
        if (autoKmi == null && currentKernelVersion() == null) {
            String[] options = customKo.isFile()
                    ? new String[]{"No bundled .ko for " + currentKmiLabel(), "Custom file · "
                            + getSharedPreferences("dfroot", MODE_PRIVATE)
                                    .getString("custom_ko_label", "custom module")}
                    : new String[]{"No bundled .ko for " + currentKmiLabel()};
            ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                    android.R.layout.simple_dropdown_item_1line, options);
            binding.koSelection.setAdapter(adapter);
            binding.koSelectionLayout.setEnabled(false);
            binding.koSelection.setEnabled(false);
            binding.koSelection.setText(options[customKo.isFile() ? 1 : 0], false);
            return;
        }

        binding.koSelectionLayout.setEnabled(true);
        binding.koSelection.setEnabled(true);
        String[] options = new String[BUNDLED_KMIS.length + 1 + (customKo.isFile() ? 1 : 0)];
        options[0] = autoKmi == null
                ? "Automatic · no Android KMI match"
                : "Automatic · dirtyfrag-" + autoKmi + ".ko"
                    + (autoKmi.equals(currentKmi()) ? "" : " · fallback for " + currentKmiLabel());
        String runningVersion = currentKernelVersion();
        for (int i = 0; i < BUNDLED_KMIS.length; i++) {
            String kmi = BUNDLED_KMIS[i];
            String suffix = runningVersion != null && runningVersion.equals(kernelVersion(kmi))
                    ? " · same kernel version" : " · different kernel version";
            options[i + 1] = "dirtyfrag-" + kmi + ".ko" + suffix;
        }
        if (customKo.isFile()) {
            String label = getSharedPreferences("dfroot", MODE_PRIVATE)
                    .getString("custom_ko_label", "custom module");
            options[options.length - 1] = "Custom file · " + label;
        }

        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_dropdown_item_1line, options);
        binding.koSelection.setAdapter(adapter);

        String selected = normalizedKoSelection();
        int position = 0;
        if (customKo.isFile()) {
            position = options.length - 1;
        } else if (!AUTO_KO.equals(selected)) {
            for (int i = 0; i < BUNDLED_KMIS.length; i++) {
                if (BUNDLED_KMIS[i].equals(selected)) {
                    position = i + 1;
                    break;
                }
            }
        }
        binding.koSelection.setText(options[position], false);
    }

    private String normalizedKoSelection() {
        String selected = getSharedPreferences("dfroot", MODE_PRIVATE)
                .getString(PREF_SELECTED_KO, AUTO_KO);
        if (ExploitRunner.customKoFile(this).isFile() || AUTO_KO.equals(selected)) return selected;

        boolean known = false;
        for (String kmi : BUNDLED_KMIS) if (kmi.equals(selected)) known = true;
        if (known && sameKernelVersion(selected, currentKmi())) return selected;

        getSharedPreferences("dfroot", MODE_PRIVATE).edit()
                .putString(PREF_SELECTED_KO, AUTO_KO).apply();
        return AUTO_KO;
    }

    private String resolvedAutoKmi() {
        String deviceKmi = currentKmi();
        if (deviceKmi == null) return null;
        for (String kmi : BUNDLED_KMIS) if (kmi.equals(deviceKmi)) return kmi;
        String version = kernelVersion(deviceKmi);
        if (version == null) return null;
        for (String kmi : BUNDLED_KMIS) {
            if (version.equals(kernelVersion(kmi))) return kmi;
        }
        return null;
    }

    private static String kernelVersion(String kmi) {
        if (kmi == null) return null;
        Matcher matcher = KMI_VERSION_PATTERN.matcher(kmi);
        return matcher.matches() ? matcher.group(1) : null;
    }

    private static boolean sameKernelVersion(String firstKmi, String secondKmi) {
        String firstVersion = kernelVersion(firstKmi);
        String secondVersion = secondKmi == null ? currentKernelVersion() : kernelVersion(secondKmi);
        return firstVersion != null && firstVersion.equals(secondVersion);
    }

    private void updateRunAvailability() {
        String selected = normalizedKoSelection();
        boolean manualBundle = !AUTO_KO.equals(selected) && sameKernelVersion(selected, null);
        boolean hasModule = ExploitRunner.customKoFile(this).isFile()
                || resolvedAutoKmi() != null || manualBundle;
        binding.btnRun.setEnabled(hasModule && !new File("/dev/df").exists());
    }

    private void updateDeviceProfile() {
        String kernel = System.getProperty("os.version", "Unknown");
        String kmi = currentKmi();
        String soc = Build.SOC_MODEL;
        if (soc == null || soc.isEmpty() || "unknown".equalsIgnoreCase(soc)) soc = "Unknown";
        String kmiLabel = kmi == null
                ? "unrecognized (kernel " + currentKernelVersion() + ")"
                : kmi;
        binding.deviceDetails.setText("Brand: " + Build.MANUFACTURER + "\n"
                + "Model / device: " + Build.MODEL + " / " + Build.DEVICE + "\n"
                + "SoC: " + soc + "\n"
                + "Android: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")\n"
                + "Security patch: " + Build.VERSION.SECURITY_PATCH + "\n"
                + "Kernel: " + kernel + "\n"
                + "KMI: " + kmiLabel);
    }

    private static String currentKmi() {
        String release = System.getProperty("os.version", "");
        Matcher matcher = KMI_PATTERN.matcher(release);
        if (!matcher.find()) return null;
        return matcher.group(3) + "-" + matcher.group(1) + "." + matcher.group(2);
    }

    private static String currentKernelVersion() {
        Matcher matcher = KERNEL_VERSION_PATTERN.matcher(System.getProperty("os.version", ""));
        return matcher.find() ? matcher.group(1) + "." + matcher.group(2) : null;
    }

    private static String currentKmiLabel() {
        String kmi = currentKmi();
        if (kmi != null) return kmi;
        String version = currentKernelVersion();
        return version == null ? "unknown" : "unknown-" + version;
    }

    private void openGithub() {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/ankitrawatgit")));
        } catch (Exception e) {
            Toast.makeText(this, "No browser is available", Toast.LENGTH_LONG).show();
        }
    }

    private File appDataDir() {
        return createDeviceProtectedStorageContext().getFilesDir().getParentFile();
    }

    private void runExploit() {
        try {
            int rc = ExploitRunner.run(this, this);
            String msg = rc == 0 ? "DirtyFrag Android Root: SUCCESS"
                       : rc == 1 ? "FAILED: ksud exited with error"
                       : rc == 2 ? "FAILED: check logs"
                       : "FAILED: failed to patch files";
            mMain.post(() -> Toast.makeText(this, msg, Toast.LENGTH_LONG).show());
        } catch (Exception e) {
            Log.e(TAG, "exploit exception", e);
            report("\nexception: " + e + "\n");
        } finally {
            mMain.post(this::updateRunAvailability);
        }
    }
}
