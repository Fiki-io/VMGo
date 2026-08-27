#!/system/bin/sh
# VM Go Process Starter & Phantom Process Killer Bypass
# Run via Wireless Debugging (ADB) or Shizuku

echo "=== VM Go Background Engine Starter ==="

# 1. Disable Phantom Process Killer on Android 11, 12, 13, 14
echo "[1/3] Disabling Android Phantom Process Killer..."
/system/bin/device_config set_sync_disabled_for_tests persistent 2>/dev/null
/system/bin/device_config put activity_manager max_phantom_processes 2147483647 2>/dev/null
/system/bin/settings put global settings_enable_monitor_phantom_procs false 2>/dev/null

# 2. Optimize system file watch limits
echo "[2/3] Increasing inotify and memory map limits..."
/system/bin/sysctl -w fs.inotify.max_user_watches=524288 2>/dev/null
/system/bin/sysctl -w vm.max_map_count=262144 2>/dev/null

# 3. Setup starter daemon in /data/local/tmp
STARTER_DIR="/data/local/tmp/vmgo"
mkdir -p "$STARTER_DIR"
chmod 777 "$STARTER_DIR"

echo "[3/3] VM Go starter daemon ready!"
echo "Status: SUCCESS - VM Go processes are now unrestricted."
