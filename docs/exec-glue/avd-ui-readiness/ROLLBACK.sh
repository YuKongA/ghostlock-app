#!/bin/sh
set -eu
TARGET=${1:-app/src/main/kotlin/com/ghostlock/app/backend/43499glued/Backend43499Glued.kt}
python3 - "$TARGET" <<'PYROLLBACK'
from pathlib import Path
import sys
target = Path(sys.argv[1])
data = target.read_bytes()
block = b'''    /**
     * Provides the readiness reason already declared by this backend to the home status card.
     *
     * @return Ready for a valid profile, otherwise the first declared profile requirement.
     * @note Readiness remains the sole source of prerequisite state.
     */
    override fun statusTextRes(): Int = when (val current = readiness()) {
        is BackendReadiness.Ready -> R.string.backend_readiness_ready
        is BackendReadiness.NotReady -> current.reasons.firstOrNull()?.messageRes
            ?: R.string.backend_readiness_undeclared
    }

'''
if data.count(block) != 1:
    raise SystemExit("ROLLBACK_REFUSED: expected exactly one statusTextRes block")
target.write_bytes(data.replace(block, b"", 1))
print("ROLLBACK_APPLIED")
PYROLLBACK
