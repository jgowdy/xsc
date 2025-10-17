# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the eXtended Syscall (XSC) driver
#

obj-$(CONFIG_XSC) += xsc.o
xsc-y := xsc_core.o xsc_consume_fs.o xsc_consume_net.o xsc_consume_timer.o xsc_consume_sync.o xsc_consume_exec.o

# XSC Syscall Mode Enforcement (binary allowlist and mode management)
xsc-y += xsc_mode.o

# XSC Wait Mechanism (optimal waiting for completion)
xsc-y += xsc_wait.o xsc_wait_sysfs.o xsc_wait_watchdog.o
xsc-$(CONFIG_X86_64) += xsc_wait_x86.o
xsc-$(CONFIG_ARM64) += xsc_wait_arm64.o xsc_wait_arm64_gic.o
