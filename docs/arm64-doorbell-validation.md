# ARM64 Doorbell Runtime Validation

## Overview

XSC implements a comprehensive runtime validation system for ARM64 doorbell mechanisms. **Never trust hardware** - this implementation validates everything before using it.

## Philosophy

Hardware doorbells promise low-latency wake-ups, but they can fail in subtle ways:
- Wrong CPU delivery (cross-cluster penalties)
- Inconsistent coalescing behavior
- High wake-from-idle latency
- Spurious interrupts
- Memory ordering violations

Rather than assuming doorbells work, XSC validates them at boot and continuously monitors them at runtime. If any check fails, the system automatically falls back to adaptive polling with no loss of functionality.

## State Machine

```
CANDIDATE       Initial discovery of doorbell hardware
    ↓
PREFLIGHT       Basic safety checks (MMIO, IRQ, CPU affinity)
    ↓
VALIDATING      Running comprehensive test suite
    ↓
STEADY          Promoted to production with watchdog monitoring
    ↓ (on failure)
FAILED          Rolled back to adaptive polling
```

## Validation Tests

### 1. Preflight Checks
- MMIO region size validation (4 bytes - 64 KB)
- IRQ number validity
- Target CPU online status
- NUMA/cluster locality

### 2. Self-Test (10 doorbells)
- Basic IRQ delivery
- Latency measurement
- CPU affinity verification
- Timeout detection (2ms)

### 3. Memory Ordering Test (1,000 iterations)
- Unique pattern writes
- Checksum validation
- Release barrier verification
- Ensures payload visible before doorbell

### 4. Soak Test (100,000 doorbells)
- Randomized intervals (10-50µs)
- Sustained reliability
- P99 latency calculation
- Wrong CPU detection

### 5. Power State Test (50 cycles)
- Wake-from-idle latency
- Deep C-state validation
- 100ms idle between tests
- Ensures acceptable wake latency

### 6. Coalescing Detection
- Rapid burst of 10 doorbells
- IRQ count analysis
- Detects hardware coalescing
- Switches mode if detected

## Thresholds (Tunable via sysfs)

| Metric | Default | Description |
|--------|---------|-------------|
| max_latency_ns | 500,000 | Maximum wake latency (500µs) |
| p99_latency_ns | 150,000 | 99th percentile target (150µs) |
| max_spurious_pct | 1 | Maximum spurious IRQ rate (1%) |
| min_effectiveness_pct | 95 | Minimum useful IRQ rate (95%) |

## Runtime Watchdog

Runs every 10 seconds, monitoring:
- Spurious IRQ rate drift
- Wrong CPU delivery increase
- Latency degradation (>2x threshold)
- Effectiveness drop

After 3 consecutive failures, automatically rolls back to polling.

## sysfs Interface

```
/sys/kernel/xsc/doorbell/
├── mode              (read/write: DISABLED, COALESCED, ENABLED)
├── status            (read-only: CANDIDATE, PREFLIGHT, VALIDATING, STEADY, FAILED)
├── irq               (read-only: IRQ number)
├── cpu               (read-only: Target CPU)
├── total_irqs        (read-only: Total IRQs delivered)
├── useful_irqs       (read-only: IRQs with actual work)
├── spurious          (read-only: Spurious IRQ count)
├── wrong_cpu         (read-only: Wrong CPU delivery count)
├── min_ns            (read-only: Minimum latency in nanoseconds)
├── max_ns            (read-only: Maximum latency in nanoseconds)
├── avg_ns            (read-only: Average latency in nanoseconds)
├── p99_us            (read-only: P99 latency in microseconds)
├── max_us            (read-only: Max latency in microseconds)
├── effectiveness     (read-only: Useful IRQ percentage)
├── fail_reason       (read-only: Last failure reason if FAILED)
├── name              (read-only: Device name)
└── watchdog_failures (read-only: Watchdog failure count)
```

## Userspace Probe Tool

```bash
# Basic validation with default thresholds
xsc-doorbell-probe

# Custom thresholds for specific ARM SoC
xsc-doorbell-probe --cpu 0 --bursts 100000 \
                    --interval-us 20 \
                    --p99 150us --max 500us

# Verbose output
xsc-doorbell-probe --verbose

# Exit codes:
#   0 = Doorbell validated and enabled
#   1 = Validation failed (fell back to polling)
#   2 = Invalid arguments
#   3 = Permission denied (requires root/CAP_SYS_ADMIN)
```

## CI/CD Integration

```bash
#!/bin/bash
# Example CI test script

if xsc-doorbell-probe --cpu 0 --p99 100us --max 300us; then
    echo "PASS: Doorbell validated for ARM Cortex-A76"
else
    echo "WARN: Doorbell validation failed, using polling"
    # Non-fatal - polling is a valid fallback
fi
```

## Security Hardening

- Root/CAP_SYS_ADMIN required for probe tool
- MMIO size limits (4 bytes - 64 KB)
- Timeout on all operations (no infinite waits)
- Automatic rollback on anomalies
- No trust in hardware behavior

## Platform-Specific Notes

### ARM Cortex-A Series
- Typical latency: 50-150ns (WFE/SEV)
- GICv3 latency: 500ns-2µs
- Watch for cross-cluster penalties on big.LITTLE

### AWS Graviton2/3
- GICv3 doorbells
- Expect 200-500ns latency
- Watch for NUMA effects

### Apple Silicon
- Custom interrupt controller
- May see coalescing behavior
- Test with actual workload

## Fallback Behavior

If validation fails:
1. Doorbell state → FAILED
2. Mode → DISABLED (adaptive polling)
3. No loss of functionality
4. Slight latency increase (polling overhead)
5. Logged to kernel ring buffer

Polling is **always safe** - doorbells are purely an optimization.

## Debugging

```bash
# Check current status
cat /sys/kernel/xsc/doorbell/status
cat /sys/kernel/xsc/doorbell/fail_reason

# Monitor statistics
watch -n 1 'cat /sys/kernel/xsc/doorbell/{total_irqs,useful_irqs,spurious,avg_ns}'

# Force disable for testing
echo DISABLED > /sys/kernel/xsc/doorbell/mode

# Re-enable
echo ENABLED > /sys/kernel/xsc/doorbell/mode

# Check kernel log
dmesg | grep xsc_doorbell
```

## Implementation Files

```
kernel-patches/drivers/xsc/
├── xsc_doorbell.h              Header with data structures
├── xsc_doorbell.c              Core validation logic
├── xsc_doorbell_sysfs.c        sysfs interface
├── xsc_doorbell_tests.c        Extended test suite
├── xsc_doorbell_watchdog.c     Runtime monitoring
├── Makefile.doorbell           Build system
└── Kconfig.doorbell            Configuration

tools/
├── xsc-doorbell-probe.c        Userspace probe tool
└── Makefile                    Tool build system
```

## References

- ARM GIC Architecture Specification
- ARM ARM (Application Reference Manual)
- Linux kernel IRQ subsystem
- Memory ordering semantics (acquire/release)

## Author

XSC Project - Never trust, always verify.
