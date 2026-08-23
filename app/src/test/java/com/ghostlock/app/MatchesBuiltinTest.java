package com.ghostlock.app;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.json.JSONObject;
import org.junit.Test;

import java.util.Map;

/**
 * Every layout scalar in the built-in tables is load-bearing: an imported
 * entry that differs in exactly one of them is a different waiter layout or
 * slab stride and must survive the import and export filters instead of
 * being dropped as identical.
 */
public class MatchesBuiltinTest {

    /**
     * First registered release whose built-in map carries both layout
     * scalars. Entries are built from the generated map itself so the test
     * tracks the tables without hardcoding offsets.
     */
    private static Map.Entry<String, Map<String, Long>> builtinWithLayoutScalars() {
        for (Map.Entry<String, Map<String, Long>> e : SupportedKernels.BUILTIN.entrySet()) {
            if (e.getValue().containsKey("compact_waiter")
                    && e.getValue().containsKey("mm_struct_sz")) {
                return e;
            }
        }
        throw new AssertionError("no built-in table carries compact_waiter and mm_struct_sz");
    }

    private static JSONObject entryFor(String release, Map<String, Long> builtin) {
        try {
            JSONObject entry = new JSONObject();
            entry.put("release", release);
            // symbols and struct_fields stay absent; the filter ignores
            // missing objects, so these cases isolate the scalar comparison
            for (String key : new String[]{"pselect_waiter_shift", "compact_waiter", "mm_struct_sz"}) {
                Long value = builtin.get(key);
                if (value != null) {
                    entry.put(key, value.longValue());
                }
            }
            return entry;
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    @Test
    public void identicalEntryMatches() {
        Map.Entry<String, Map<String, Long>> builtin = builtinWithLayoutScalars();
        assertTrue(MainActivity.matchesBuiltin(entryFor(builtin.getKey(), builtin.getValue())));
    }

    @Test
    public void differingCompactWaiterCountsAsDifferent() throws Exception {
        Map.Entry<String, Map<String, Long>> builtin = builtinWithLayoutScalars();
        JSONObject entry = entryFor(builtin.getKey(), builtin.getValue());
        // 0 is the rb_node waiter layout a stale 6.6 table would carry
        entry.put("compact_waiter", entry.optLong("compact_waiter") ^ 1);
        assertFalse(MainActivity.matchesBuiltin(entry));
    }

    @Test
    public void differingMmStructSzCountsAsDifferent() throws Exception {
        Map.Entry<String, Map<String, Long>> builtin = builtinWithLayoutScalars();
        JSONObject entry = entryFor(builtin.getKey(), builtin.getValue());
        // +0x100 is the BTF stride the device SLUB layout corrects away
        entry.put("mm_struct_sz", entry.optLong("mm_struct_sz") + 0x100);
        assertFalse(MainActivity.matchesBuiltin(entry));
    }

    @Test
    public void nullLayoutScalarsStayIgnored() throws Exception {
        Map.Entry<String, Map<String, Long>> builtin = builtinWithLayoutScalars();
        JSONObject entry = entryFor(builtin.getKey(), builtin.getValue());
        entry.put("compact_waiter", JSONObject.NULL);
        entry.put("mm_struct_sz", JSONObject.NULL);
        assertTrue(MainActivity.matchesBuiltin(entry));
    }

    @Test
    public void unknownReleaseNeverMatches() {
        Map.Entry<String, Map<String, Long>> builtin = builtinWithLayoutScalars();
        assertFalse(MainActivity.matchesBuiltin(entryFor("not-a-kernel", builtin.getValue())));
    }
}
