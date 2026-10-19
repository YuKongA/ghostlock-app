#!/system/bin/sh
HOME_DIR=/data/local/tmp
LOG="$HOME_DIR/.ghostlock_lkm.log"
FINISH_DIR=/data/user/0/com.ghostlock.app/files/finish
FINISH_LOG="$HOME_DIR/.ghostlock_finish.log"
FINISH_EXIT="$HOME_DIR/.ghostlock_finish.exit"
FINISH_READY="$HOME_DIR/.ghostlock_finish.ready"
PARAMS=/data/media/0/Android/data/com.ghostlock.app/files/finish/finish.params
log() { echo "$*" >>"$LOG"; echo "$*" >>"$FINISH_LOG"; }
echo "[*] finish root start uid=$(id -u)" >"$LOG"; chmod 644 "$LOG" 2>/dev/null
ROOT_PID_FILE="$HOME_DIR/.ghostlock_root.pid"
if [ -f "$ROOT_PID_FILE" ]; then
  OLD_ROOT=$(cat "$ROOT_PID_FILE" 2>/dev/null)
  if [ -n "$OLD_ROOT" ] && [ "$OLD_ROOT" != "$$" ] && [ -d "/proc/$OLD_ROOT" ]; then
    kill -9 "$OLD_ROOT" 2>/dev/null || true
  fi
fi
echo "$$" >"$ROOT_PID_FILE"
chmod 644 "$ROOT_PID_FILE" 2>/dev/null

rm -f /data/local/tmp/.ghostlock_lkm_ok /data/local/tmp/.ghostlock_lkm_fail /dev/dfm0 /dev/dfm1 2>/dev/null
rm -f "$FINISH_EXIT" "$FINISH_LOG" "$FINISH_READY"
chmod 666 /dev/glk 2>/dev/null; chcon u:object_r:null_device:s0 /dev/glk 2>/dev/null
echo "$$" >"$FINISH_READY"; chmod 644 "$FINISH_READY" 2>/dev/null
touch /dev/dfm0 /data/local/tmp/.ghostlock_lkm_ok
log '[*] finish root ready'
while [ ! -f "$FINISH_DIR/root-request" ]; do sleep 0.2; done
rm -f "$FINISH_DIR/root-request"
MANAGER=""; MANAGER_ENABLED=0; MANAGER_BINARY=""
APP_ENABLED=0; APP_PACKAGE=""
COMMAND_ENABLED=0; COMMAND_MODE=0; COMMAND_B64=""; SCRIPT_PATH=""
LKM_ENABLED=0; LKM_PATH=""; RESTORE_SELINUX=0; SOFT_REBOOT=0
if [ -f "$PARAMS" ]; then
  while IFS='=' read -r key value; do
    case "$key" in
      manager) MANAGER="$value";; manager_enabled) MANAGER_ENABLED="$value";;
      manager_binary) MANAGER_BINARY="$value";; app_enabled) APP_ENABLED="$value";;
      app_package) APP_PACKAGE="$value";; command_enabled) COMMAND_ENABLED="$value";;
      command_mode) COMMAND_MODE="$value";; command_b64) COMMAND_B64="$value";;
      script_path) SCRIPT_PATH="$value";; lkm_enabled) LKM_ENABLED="$value";;
      lkm_path) LKM_PATH="$value";; restore_selinux) RESTORE_SELINUX="$value";;
      soft_reboot) SOFT_REBOOT="$value";;
    esac
  done <"$PARAMS"
else
  log '[!] finish.params missing'
fi
if [ "$MANAGER_ENABLED" = "1" ]; then
  log "[*] manager=$MANAGER"
  if [ "$MANAGER" = "FolkPatch" ]; then
    APD=/data/adb/apd
    if [ ! -x "$APD" ]; then
      APK=$(pm path me.yuki.folk 2>/dev/null | sed 's/package://')
      SRC="${APK%/*}/lib/arm64/libapd.so"
      if [ -r "$SRC" ]; then
        cp "$SRC" /data/local/tmp/ghostlock-apd 2>/dev/null || true
        chmod 755 /data/local/tmp/ghostlock-apd 2>/dev/null || true
        [ -x /data/local/tmp/ghostlock-apd ] && APD=/data/local/tmp/ghostlock-apd
      fi
    fi
    if [ -x "$APD" ] && [ -n "$LKM_PATH" ]; then
      "$APD" late-load --module "$LKM_PATH" --package-name me.yuki.folk >>"$FINISH_LOG" 2>&1 || log '[!] apd late-load failed'
    else
      log '[!] apd or kernelpatch.ko missing'
    fi
    mkdir -p /data/adb/ap 2>/dev/null || true
    : > /data/adb/ap/jailbreak 2>/dev/null || true
    am force-stop me.yuki.folk 2>/dev/null || true
    monkey -p me.yuki.folk -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
  else
    BIN=$(find /data/app -path '*/me.weishu.kernelsu*/lib/arm64/libksud.so' 2>/dev/null | head -1)
    if [ -z "$BIN" ]; then BIN="$MANAGER_BINARY"; fi
    if [ -n "$BIN" ] && [ -f "$BIN" ]; then
      cp "$BIN" /data/local/tmp/ghostlock-manager 2>/dev/null || true
      chmod 755 /data/local/tmp/ghostlock-manager 2>/dev/null || true
      [ -x /data/local/tmp/ghostlock-manager ] && BIN=/data/local/tmp/ghostlock-manager
    fi
    log "[*] manager bin=$BIN"
    KVER=$(uname -r | cut -d. -f1-2)
    AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)
    KMI="$AVER-$KVER"
    if [ -x "$BIN" ]; then
      if [ -x /system/bin/su ]; then
        log '[*] manager already loaded'
      else
        if command -v timeout >/dev/null 2>&1; then
          timeout 12 "$BIN" late-load --allow-shell --kmi "$KMI" >>"$FINISH_LOG" 2>&1 || log '[!] manager late-load failed'
        else
          "$BIN" late-load --allow-shell --kmi "$KMI" >>"$FINISH_LOG" 2>&1 || log '[!] manager late-load failed'
        fi
        ( "$BIN" post-fs-data >>"$FINISH_LOG" 2>&1 & )
        ( setsid "$BIN" services >>"$FINISH_LOG" 2>&1 & ) 2>/dev/null || ( "$BIN" services >>"$FINISH_LOG" 2>&1 & )
        ( "$BIN" boot-completed >>"$FINISH_LOG" 2>&1 & )
        sleep 1
      fi
    else
      log '[!] manager loader missing'
    fi
  fi
fi
if [ "$LKM_ENABLED" = "1" ] && [ "$MANAGER" != "FolkPatch" ] && [ -n "$LKM_PATH" ]; then
  insmod "$LKM_PATH" >>"$FINISH_LOG" 2>&1 || log '[!] lkm insmod failed'
fi
if [ "$COMMAND_ENABLED" = "1" ]; then
  if [ "$COMMAND_MODE" = "1" ] && [ -n "$SCRIPT_PATH" ]; then
    sh "$SCRIPT_PATH" >>"$FINISH_LOG" 2>&1 || log '[!] script failed'
  elif [ -n "$COMMAND_B64" ]; then
    printf '%s' "$COMMAND_B64" | base64 -d | sh >>"$FINISH_LOG" 2>&1 || log '[!] command failed'
  fi
fi
if [ "$APP_ENABLED" = "1" ] && [ -n "$APP_PACKAGE" ]; then
  monkey -p "$APP_PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
fi
if [ "$RESTORE_SELINUX" = "1" ]; then
  log '[*] restore selinux enforcing'; setenforce 1 2>/dev/null || echo 1 > /sys/fs/selinux/enforce
fi
if [ "$SOFT_REBOOT" = "1" ]; then log '[*] soft reboot zygote'; setprop ctl.restart zygote; fi
log '[*] finish root done'
echo 0 >"$FINISH_EXIT"; chmod 644 "$FINISH_LOG" "$FINISH_EXIT" 2>/dev/null
exit 0
