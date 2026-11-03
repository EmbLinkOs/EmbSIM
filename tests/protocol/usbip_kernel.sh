#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The session's USB/IP server against the kernel's own client: vhci-hcd and
# the usbip tool attach the bench's USB devices, and the tools that open a
# real probe drive them through the computer's USB stack:
#   - the NUCLEO's ST-LINK, where stlink-tools' st-info finds the STM32F446;
#   - a Raspberry Pi Debug Probe on a Pico, where OpenOCD (interface/
#     cmsis-dap.cfg, target/rp2040.cfg) wakes the RP2040's multi-drop wire,
#     halts both cores and reads the flash.
# Each part skips (exit 0) where vhci-hcd, usbip or its tool cannot be had
# (the module and the attach need root); a part fails when its probe was
# attached and the tool does not see the target.
#
#     usbip_kernel.sh EMBSIM
set -u
EMBSIM=$1
skip() { echo "SKIP: $1"; }
fail=0
pid=
cleanup() {
    sudo -n usbip detach -p 0 >/dev/null 2>&1
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
    pid=
}
trap cleanup EXIT

if ! sudo -n modprobe vhci-hcd 2>/dev/null; then
    skip "vhci-hcd cannot be loaded"
    exit 0
fi
if ! command -v usbip >/dev/null 2>&1; then
    skip "no usbip tool"
    exit 0
fi

# serve PROJECT on USB/IP's PORT (usbip's default 3240) and attach the
# device VID:PID; sets $pid
attach() {
    "$EMBSIM" serve "$1" --port 0 --usbip "$2" --run &
    pid=$!
    sleep 3
    list=$(usbip list -r 127.0.0.1 2>&1)
    echo "$list"
    bus=$(echo "$list" | sed -n "s/^ *\(1-[0-9][0-9]*\):.*($3).*/\1/p" | head -n 1)
    [ -n "$bus" ] || { echo "FAIL: $3 is not in usbip's list"; return 1; }
    sudo -n usbip attach -r 127.0.0.1 -b "$bus" || { skip "usbip attach refused (no vhci port?)"; return 2; }
    sleep 3
    return 0
}

# the ST-LINK and st-info
if command -v st-info >/dev/null 2>&1; then
    attach examples/nucleo-f446re-blink-serial/project.json 3240 0483:374b
    r=$?
    if [ $r -eq 0 ]; then
        out=$(sudo -n st-info --probe 2>&1)
        echo "$out"
        if echo "$out" | grep -qi "chipid: *0x0*421"; then
            echo "usbip kernel: st-info sees the STM32F446 through the ST-LINK"
        else
            echo "FAIL: st-info does not see the STM32F446 (chip id 0x421)"
            fail=1
        fi
    elif [ $r -eq 1 ]; then
        fail=1
    fi
    cleanup
    sleep 1
else
    skip "no st-info (stlink-tools)"
fi

# the Debug Probe and OpenOCD
if command -v openocd >/dev/null 2>&1; then
    attach examples/pico-debug-probe/project.json 3240 2e8a:000c
    r=$?
    if [ $r -eq 0 ]; then
        out=$(sudo -n timeout 60 openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
            -c "adapter speed 1000" -c "init" \
            -c "targets rp2040.core0" -c "halt" -c "reg pc" -c "mdw 0x10000100 2" \
            -c "targets rp2040.core1" -c "halt" -c "reg pc" -c "resume" \
            -c "targets rp2040.core0" -c "resume" -c "shutdown" 2>&1)
        echo "$out"
        if echo "$out" | grep -qi "SWD DPIDR 0x0bc12477" &&
           echo "$out" | grep -qi "0x10000100: 20040000 100001ad" &&
           echo "$out" | grep -qi "rp2040.core1.*halted"; then
            echo "usbip kernel: OpenOCD drives the Pico's two cores through the Debug Probe"
        else
            echo "FAIL: OpenOCD did not halt the RP2040 and read its flash through the Debug Probe"
            fail=1
        fi
    elif [ $r -eq 1 ]; then
        fail=1
    fi
    cleanup
else
    skip "no openocd"
fi

[ $fail -eq 0 ] && echo "usbip kernel: ok"
exit $fail
