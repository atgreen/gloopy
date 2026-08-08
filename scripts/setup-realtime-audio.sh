#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# setup-realtime-audio.sh — prepare a Linux desktop for glitch-free (xrun-free) playback.
#
# Does the three things every low-latency Linux DAW needs and that a stock desktop is missing:
#   1. Real-time priority — lets audio/sampler threads run SCHED_FIFO/RR so the kernel can't
#      preempt them. Needs BOTH a 'realtime' group + /etc/security/limits.d drop-in (which reaches
#      TTY / SSH / su logins via pam_limits) AND systemd DefaultLimitRTPRIO drop-ins — because
#      pam_limits is NOT in the GDM/Wayland login stack, so limits.d alone never reaches the
#      graphical session or anything launched from it (Gloopy included). This is the biggest lever.
#   2. CPU governor = performance — no frequency-scaling latency spikes mid-callback.
#   3. A roomier PipeWire buffer — more slack per callback, so a decaying reverb/delay tail or a
#      brief CPU blip doesn't under-run.
#
# Idempotent and safe to re-run. Fedora-focused (Gloopy's reference distro) but works on most
# systemd + PipeWire systems. Uses sudo only for the system-wide bits; PipeWire is per-user.
#
#   scripts/setup-realtime-audio.sh          # apply everything
#   scripts/setup-realtime-audio.sh --check  # just report the current state
#
# After it runs you MUST reboot — the systemd manager defaults that govern the graphical session are
# read at boot, so a re-login alone is not enough for GUI-launched apps to pick up the RT priority.

set -euo pipefail

QUANTUM="${GLOOPY_PW_QUANTUM:-1024}"   # PipeWire buffer in frames (1024 @ 48k ≈ 21 ms); override via env
GROUP=realtime
LIMITS=/etc/security/limits.d/95-realtime.conf
SYSD_SYSTEM=/etc/systemd/system.conf.d/90-realtime.conf   # governs system services
SYSD_USER=/etc/systemd/user.conf.d/90-realtime.conf       # governs the graphical/user session (GUI-launched apps)

bold=$'\e[1m'; grn=$'\e[32m'; ylw=$'\e[33m'; red=$'\e[31m'; dim=$'\e[2m'; rst=$'\e[0m'
say()  { printf '%s\n' "${bold}==>${rst} $*"; }
ok()   { printf '    %s\n' "${grn}✓${rst} $*"; }
warn() { printf '    %s\n' "${ylw}!${rst} $*"; }
info() { printf '    %s\n' "${dim}$*${rst}"; }

[ "$(uname -s)" = "Linux" ] || { echo "This script is for Linux only." >&2; exit 1; }
[ "$(id -u)" -ne 0 ] || { echo "Run as your normal user, not root — it will sudo where needed." >&2; exit 1; }

# ---------------------------------------------------------------------------------------------
report() {
    say "Current state"
    local rt gov q
    rt="$(ulimit -r 2>/dev/null || echo 0)"
    if [ "$rt" -ge 1 ]; then ok "RT priority allowed for this session (ulimit -r = $rt)"
    else warn "RT priority NOT allowed in this session (ulimit -r = $rt) — needs a re-login after setup"; fi
    id -nG | tr ' ' '\n' | grep -qx "$GROUP" && ok "in the '$GROUP' group" || warn "not in the '$GROUP' group yet"
    [ -f "$LIMITS" ] && ok "pam limits drop-in present ($LIMITS — for TTY/SSH logins)" || warn "no pam limits drop-in"
    # The graphical session gets its limits from systemd, not pam_limits — this is what governs
    # Gloopy when launched from the desktop, and the real reason 'ulimit -r' can read 0 in a GUI shell.
    local urt
    urt="$(systemctl --user show -p DefaultLimitRTPRIO --value 2>/dev/null || echo 0)"
    if [ "${urt:-0}" -ge 1 ] 2>/dev/null; then ok "systemd user DefaultLimitRTPRIO = $urt (governs GUI-launched apps)"
    else warn "systemd user DefaultLimitRTPRIO = ${urt:-0} — GUI-launched apps get NO RT budget (need the drop-in + reboot)"; fi
    { [ -f "$SYSD_SYSTEM" ] && [ -f "$SYSD_USER" ]; } && ok "systemd rtprio drop-ins present" || warn "systemd rtprio drop-ins missing ($SYSD_USER)"
    gov="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
    [ "$gov" = performance ] && ok "CPU governor = performance" || warn "CPU governor = $gov (want 'performance')"
    if command -v pw-metadata >/dev/null 2>&1; then
        q="$(pw-metadata -n settings 2>/dev/null | grep -oE "clock.force-quantum' value:'[0-9]+" | grep -oE '[0-9]+$' | head -1)"
        [ -n "${q:-}" ] && [ "$q" != 0 ] && ok "PipeWire forced quantum = $q frames" || warn "PipeWire quantum not forced (dynamic)"
    else warn "pw-metadata not found (is this PipeWire?)"; fi
}

if [ "${1:-}" = "--check" ]; then report; exit 0; fi

# ---------------------------------------------------------------------------------------------
say "1/3  Real-time priority"
if getent group "$GROUP" >/dev/null; then ok "group '$GROUP' exists"
else sudo groupadd -r "$GROUP"; ok "created group '$GROUP'"; fi
if id -nG "$USER" | tr ' ' '\n' | grep -qx "$GROUP"; then ok "'$USER' already in '$GROUP'"
else sudo usermod -aG "$GROUP" "$USER"; ok "added '$USER' to '$GROUP' (applies after re-login)"; fi
sudo tee "$LIMITS" >/dev/null <<EOF
# Managed by Gloopy scripts/setup-realtime-audio.sh — real-time audio limits (pam_limits path:
# reaches TTY / SSH / su logins; NOT the graphical session — see the systemd drop-ins below).
@$GROUP   -   rtprio      95
@$GROUP   -   memlock     unlimited
@$GROUP   -   nice       -19
EOF
ok "wrote $LIMITS (rtprio 95, memlock unlimited)"

# The graphical session (GDM/Wayland) and everything it launches — Gloopy, terminals — take their
# limits from the systemd manager, not pam_limits, so the file above never reaches them. Raise the
# systemd default so GUI-launched apps actually get an RT budget (this is what lets sfizz's sample
# loaders and JUCE's audio thread run SCHED_RR/FIFO instead of failing with 'Cannot set scheduling').
for f in "$SYSD_SYSTEM" "$SYSD_USER"; do
    sudo mkdir -p "$(dirname "$f")"
    sudo tee "$f" >/dev/null <<EOF
# Managed by Gloopy scripts/setup-realtime-audio.sh — RT budget for the systemd-managed session.
[Manager]
DefaultLimitRTPRIO=95
DefaultLimitMEMLOCK=infinity
EOF
done
ok "wrote systemd drop-ins ($SYSD_SYSTEM, $SYSD_USER) — DefaultLimitRTPRIO=95"
warn "these are read at boot: a REBOOT (not just re-login) is required for the graphical session"

# ---------------------------------------------------------------------------------------------
say "2/3  CPU governor → performance"
if command -v tuned-adm >/dev/null 2>&1; then
    sudo systemctl enable --now tuned >/dev/null 2>&1 || true
    sudo tuned-adm profile latency-performance && ok "tuned profile: latency-performance (persistent)"
elif command -v cpupower >/dev/null 2>&1; then
    sudo cpupower frequency-set -g performance >/dev/null && ok "cpupower governor: performance"
    warn "not persistent across reboot — install 'tuned' (sudo dnf install tuned) for that"
else
    for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance | sudo tee "$g" >/dev/null; done
    ok "governor set via sysfs"
    warn "not persistent across reboot — install 'tuned' or 'kernel-tools' (cpupower) for that"
fi

# ---------------------------------------------------------------------------------------------
say "3/3  PipeWire buffer (quantum = $QUANTUM frames)"
if command -v pw-metadata >/dev/null 2>&1; then
    pw-metadata -n settings 0 clock.force-quantum "$QUANTUM" >/dev/null 2>&1 \
        && ok "forced quantum to $QUANTUM for this session" \
        || warn "couldn't set the live quantum (is PipeWire running?)"
    mkdir -p "$HOME/.config/pipewire/pipewire.conf.d"
    cat > "$HOME/.config/pipewire/pipewire.conf.d/99-gloopy-lowlatency.conf" <<EOF
# Managed by Gloopy scripts/setup-realtime-audio.sh — roomier buffer for glitch-free playback.
context.properties = {
    default.clock.quantum      = $QUANTUM
    default.clock.min-quantum  = 256
    default.clock.max-quantum  = 8192
}
EOF
    ok "wrote persistent config (~/.config/pipewire/pipewire.conf.d/99-gloopy-lowlatency.conf)"
    info "restart PipeWire to load it: systemctl --user restart pipewire pipewire-pulse wireplumber"
else
    warn "pw-metadata not found — skipping (not a PipeWire system?). Raise the buffer in your audio server instead."
fi

# ---------------------------------------------------------------------------------------------
echo
report
echo
say "Next steps"
info "• REBOOT — the systemd manager defaults that govern the graphical session are read at boot,"
info "  so a re-login alone won't give GUI-launched apps their RT budget."
info "• Then re-run:  scripts/setup-realtime-audio.sh --check   → 'systemd user DefaultLimitRTPRIO'"
info "  should read 95, and 'ulimit -r' in a fresh GUI terminal should too."
info "• Launch Gloopy and watch the CPU meter in the status bar; it should stay out of the red."
info "  (The '[sfizz] Cannot set current thread scheduling parameters' messages will also be gone.)"
