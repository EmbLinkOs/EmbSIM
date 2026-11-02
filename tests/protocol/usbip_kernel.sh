#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The session's USB/IP server against the kernel's own client: vhci-hcd and
# the usbip tool attach the NUCLEO's ST-LINK, and stlink-tools' st-info
# finds the STM32F446 behind it through the computer's USB stack.
# Skips (exit 0) where vhci-hcd, usbip or st-info cannot be had (it needs
# root for the module and the attach); fails when the probe was attached
# and st-info does not see the part.
#
#     usbip_kernel.sh EMBSIM
set -u
EMBSIM=$1
skip() { echo "SKIP: $1"; exit 0; }
sudo -n modprobe vhci-hcd 2>/dev/null || skip "vhci-hcd cannot be loaded"
command -v usbip >/dev/null 2>&1 || skip "no usbip tool"
command -v st-info >/dev/null 2>&1 || skip "no st-info (stlink-tools)"
"$EMBSIM" serve examples/nucleo-f446re-blink-serial/project.json --port 0 --usbip 3240 --run &
pid=$!
trap 'sudo -n usbip detach -p 0 >/dev/null 2>&1; kill $pid 2>/dev/null' EXIT
sleep 3
list=$(usbip list -r 127.0.0.1 2>&1)
echo "$list"
bus=$(echo "$list" | sed -n 's/^ *\(1-[0-9][0-9]*\):.*0483:374b.*/\1/p' | head -n 1)
[ -n "$bus" ] || { echo "FAIL: the ST-LINK is not in usbip's list"; exit 1; }
sudo -n usbip attach -r 127.0.0.1 -b "$bus" || skip "usbip attach refused (no vhci port?)"
sleep 3
out=$(sudo -n st-info --probe 2>&1)
echo "$out"
echo "$out" | grep -qi "chipid: *0x0*421" || { echo "FAIL: st-info does not see the STM32F446 (chip id 0x421)"; exit 1; }
echo "usbip kernel: ok"
