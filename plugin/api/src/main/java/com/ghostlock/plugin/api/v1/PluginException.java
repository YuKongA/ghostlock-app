package com.ghostlock.plugin.api.v1;

import java.nio.charset.StandardCharsets;
import java.util.Objects;

/** Checked failure with a bounded, display-safe detail. */
public final class PluginException extends Exception {
    private final PluginFailureCode code;
    private final String detail;

    public PluginException(PluginFailureCode code, String detail) {
        super(validateDetail(detail));
        this.code = Objects.requireNonNull(code, "code");
        this.detail = detail;
    }

    public PluginFailureCode code() {
        return code;
    }

    public String detail() {
        return detail;
    }

    private static String validateDetail(String value) {
        Objects.requireNonNull(value, "detail");
        for (int offset = 0; offset < value.length(); ) {
            char unit = value.charAt(offset);
            if (Character.isSurrogate(unit)) {
                if (!Character.isHighSurrogate(unit)
                        || offset + 1 >= value.length()
                        || !Character.isLowSurrogate(value.charAt(offset + 1))) {
                    throw new IllegalArgumentException("detail contains an unpaired surrogate");
                }
            }
            int codePoint = value.codePointAt(offset);
            int type = Character.getType(codePoint);
            if (Character.isISOControl(codePoint)
                    || type == Character.LINE_SEPARATOR
                    || type == Character.PARAGRAPH_SEPARATOR) {
                throw new IllegalArgumentException("detail must be single-line printable text");
            }
            offset += Character.charCount(codePoint);
        }
        if (value.getBytes(StandardCharsets.UTF_8).length > 4096) {
            throw new IllegalArgumentException("detail exceeds 4096 UTF-8 bytes");
        }
        return value;
    }
}
