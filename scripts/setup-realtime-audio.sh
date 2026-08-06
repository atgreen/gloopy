#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# setup-realtime-audio.sh — prepare a Linux desktop for glitch-free (xrun-free) playback.
#
# Does the three things every low-latency Linux DAW needs and that a stock desktop is missing:
#   1. Real-time priority — lets the audio thread run SCHED_FIFO so the kernel can't preempt it
#      (a 'realtime' group + a /etc/security/limits.d drop-in). This is the biggest lever.
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
# After it runs you MUST log out and back in (or reboot) for the RT priority + group to apply.

set -euo pipefail

QUANTUM="${GLOOPY_PW_QUANTUM:-1024}"   # PipeWire buffer in frames (1024 @ 48k ≈ 21 ms); override via env
GROUP=realtime
LIMITS=/etc/security/limits.d/95-realtime.conf

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
    [ -f "$LIMITS" ] && ok "limits drop-in present ($LIMITS)" || warn "no limits drop-in"
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
# Managed by Gloopy scripts/setup-realtime-audio.sh — real-time audio limits.
@$GROUP   -   rtprio      95
@$GROUP   -   memlock     unlimited
@$GROUP   -   nice       -19
EOF
ok "wrote $LIMITS (rtprio 95, memlock unlimited)"

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
info "• LOG OUT and back in (or reboot) — the RT priority + group only take effect on a fresh login."
info "• Then re-run:  scripts/setup-realtime-audio.sh --check   → 'ulimit -r' should read 95."
info "• Launch Gloopy and watch the CPU meter in the status bar; it should stay out of the red."
