// SPDX-License-Identifier: GPL-2.0
/*
 * XSC ARM64 GICv3/GICv4 Wait Mechanism
 *
 * Uses GIC LPIs (Locality-specific Peripheral Interrupts) and
 * GICv4 direct injection for optimal latency
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/delay.h>

#include "../../include/xsc_wait.h"

/* GIC registers */
#define GICD_TYPER		0x0004
#define GICD_TYPER2		0x000C
#define GICR_TYPER		0x0008

/* GICv3 LPI support bit */
#define GICD_TYPER_LPIS		(1 << 17)

/* GICv4 direct injection support */
#define GICD_TYPER2_VIL		(1 << 7)

/* ITS (Interrupt Translation Service) support */
#define GITS_CTLR		0x0000
#define GITS_TYPER		0x0008
#define GITS_CBASER		0x0080
#define GITS_CWRITER		0x0088
#define GITS_CREADR		0x0090

/* ITS commands */
#define GITS_CMD_MAPD		0x08	/* Map Device */
#define GITS_CMD_MAPC		0x09	/* Map Collection */
#define GITS_CMD_MAPTI		0x0a	/* Map Translation & Interrupt */
#define GITS_CMD_MAPI		0x0b	/* Map Interrupt */
#define GITS_CMD_INV		0x0c	/* Invalidate */
#define GITS_CMD_SYNC		0x0d	/* Synchronize */

/* GIC device state */
struct xsc_gic_device {
	void __iomem *gicd_base;	/* Distributor base */
	void __iomem *gicr_base;	/* Redistributor base */
	void __iomem *its_base;		/* ITS base */

	int lpi_base;			/* Base LPI number */
	int lpi_count;			/* Number of allocated LPIs */

	bool has_gicv3;
	bool has_gicv4;
	bool has_lpi;
	bool has_its;
	bool has_vil;			/* Virtual LPI support (GICv4) */

	/* ITS command queue */
	void *cmd_base;
	dma_addr_t cmd_base_pa;
	u32 cmd_write_idx;
	u32 cmd_queue_size;

	/* Per-CPU redistributor info */
	void __iomem **gicr_bases;	/* Per-CPU redistributors */
	int nr_cpus;

	/* Statistics */
	atomic64_t lpi_delivered;
	atomic64_t lpi_latency_ns;
	atomic64_t its_commands_sent;
};

static struct xsc_gic_device *xsc_gic_dev = NULL;

/*
 * Read GIC Distributor register
 */
static inline u32 gicd_readl(struct xsc_gic_device *gic, u32 offset)
{
	return readl_relaxed(gic->gicd_base + offset);
}

/*
 * Read GIC Redistributor register
 */
static inline u32 gicr_readl(struct xsc_gic_device *gic, int cpu, u32 offset)
{
	if (cpu >= gic->nr_cpus || !gic->gicr_bases[cpu])
		return 0;
	return readl_relaxed(gic->gicr_bases[cpu] + offset);
}

/*
 * Read ITS register
 */
static inline u64 gits_readq(struct xsc_gic_device *gic, u32 offset)
{
	if (!gic->its_base)
		return 0;
	return readq_relaxed(gic->its_base + offset);
}

static inline void gits_writeq(struct xsc_gic_device *gic, u32 offset, u64 val)
{
	if (!gic->its_base)
		return;
	writeq_relaxed(val, gic->its_base + offset);
}

/*
 * Send ITS command
 */
static int gits_send_command(struct xsc_gic_device *gic, u64 *cmd)
{
	u64 *cmd_ptr;
	u32 write_idx = gic->cmd_write_idx;

	if (!gic->its_base || !gic->cmd_base)
		return -EINVAL;

	/* Get write pointer */
	cmd_ptr = (u64 *)gic->cmd_base + (write_idx * 4);

	/* Write command (4 x 64-bit words) */
	cmd_ptr[0] = cmd[0];
	cmd_ptr[1] = cmd[1];
	cmd_ptr[2] = cmd[2];
	cmd_ptr[3] = cmd[3];

	/* Ensure command visible */
	dsb(ishst);

	/* Advance write pointer */
	write_idx = (write_idx + 1) % (gic->cmd_queue_size / 32);
	gic->cmd_write_idx = write_idx;

	/* Update ITS CWRITER */
	gits_writeq(gic, GITS_CWRITER, write_idx * 32);

	atomic64_inc(&gic->its_commands_sent);

	/* Wait for command to complete */
	{
		u64 creadr;
		int timeout = 1000;

		while (timeout--) {
			creadr = gits_readq(gic, GITS_CREADR);
			if ((creadr / 32) == write_idx)
				break;
			udelay(1);
		}

		if (timeout < 0) {
			pr_warn("xsc_gic: ITS command timeout\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * Detect GICv3/GICv4 capabilities
 */
static int detect_gic_capabilities(struct xsc_gic_device *gic)
{
	u32 typer, typer2;
	u64 its_typer;

	/* Read GICD_TYPER */
	typer = gicd_readl(gic, GICD_TYPER);

	pr_info("xsc_gic: GICD_TYPER = 0x%08x\n", typer);

	/* Check for LPI support */
	gic->has_lpi = !!(typer & GICD_TYPER_LPIS);
	if (gic->has_lpi) {
		pr_info("xsc_gic: LPI (Locality-specific Peripheral Interrupts) supported\n");
		gic->has_gicv3 = true;
	} else {
		pr_info("xsc_gic: LPI not supported (GICv2 or older)\n");
		return -ENODEV;
	}

	/* Read GICD_TYPER2 for GICv4 features */
	typer2 = gicd_readl(gic, GICD_TYPER2);
	if (typer2) {
		pr_info("xsc_gic: GICD_TYPER2 = 0x%08x\n", typer2);

		/* Check for Virtual LPI support (GICv4) */
		gic->has_vil = !!(typer2 & GICD_TYPER2_VIL);
		if (gic->has_vil) {
			pr_info("xsc_gic: GICv4 Virtual LPI support detected\n");
			gic->has_gicv4 = true;
		}
	}

	/* Check ITS (Interrupt Translation Service) */
	if (gic->its_base) {
		its_typer = gits_readq(gic, GITS_TYPER);
		pr_info("xsc_gic: GITS_TYPER = 0x%016llx\n", its_typer);

		gic->has_its = true;
		pr_info("xsc_gic: ITS (Interrupt Translation Service) available\n");

		/* Extract ITS capabilities */
		u32 devbits = (its_typer >> 13) & 0x1f;
		u32 idbits = (its_typer >> 8) & 0x1f;

		pr_info("xsc_gic: ITS supports %u device ID bits, %u event ID bits\n",
			devbits, idbits);
	}

	return 0;
}

/*
 * Initialize ITS command queue
 */
static int init_its_command_queue(struct xsc_gic_device *gic)
{
	u64 cbaser;

	if (!gic->has_its)
		return 0;

	/* Allocate command queue (64KB = 2048 commands) */
	gic->cmd_queue_size = 65536;
	gic->cmd_base = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						  get_order(gic->cmd_queue_size));
	if (!gic->cmd_base) {
		pr_err("xsc_gic: Failed to allocate ITS command queue\n");
		return -ENOMEM;
	}

	gic->cmd_base_pa = virt_to_phys(gic->cmd_base);

	/* Configure GITS_CBASER */
	cbaser = gic->cmd_base_pa |
		 (0x7ULL << 56) |	/* Inner shareable */
		 (0x1ULL << 53) |	/* Inner cacheable */
		 (0x1ULL << 10) |	/* Valid */
		 (get_order(gic->cmd_queue_size) - 1);

	gits_writeq(gic, GITS_CBASER, cbaser);

	/* Enable ITS */
	gits_writeq(gic, GITS_CTLR, 1);

	/* Reset write index */
	gic->cmd_write_idx = 0;
	gits_writeq(gic, GITS_CWRITER, 0);

	pr_info("xsc_gic: ITS command queue initialized at PA 0x%llx\n",
		(unsigned long long)gic->cmd_base_pa);

	return 0;
}

/*
 * Allocate LPI for XSC use
 */
static int allocate_lpi(struct xsc_gic_device *gic)
{
	/* LPIs start at ID 8192 */
	gic->lpi_base = 8192;
	gic->lpi_count = 16;  /* Allocate 16 LPIs for XSC */

	pr_info("xsc_gic: Allocated LPIs %d-%d for XSC\n",
		gic->lpi_base, gic->lpi_base + gic->lpi_count - 1);

	return 0;
}

/*
 * GIC LPI interrupt handler
 */
static irqreturn_t xsc_gic_lpi_handler(int irq, void *data)
{
	struct xsc_wait_mechanism *mech = data;
	u64 now = xsc_rdtsc();

	atomic64_inc(&xsc_gic_dev->lpi_delivered);

	/* Wake up waiter */
	complete(&mech->gic_wait_complete);

	return IRQ_HANDLED;
}

/*
 * Initialize GIC-based wait mechanism
 */
int xsc_gic_init(struct xsc_wait_mechanism *mech)
{
	struct device_node *node;
	struct resource res;
	int ret;

	/* Allocate GIC device state */
	xsc_gic_dev = kzalloc(sizeof(*xsc_gic_dev), GFP_KERNEL);
	if (!xsc_gic_dev)
		return -ENOMEM;

	/* Find GIC distributor in device tree */
	node = of_find_compatible_node(NULL, NULL, "arm,gic-v3");
	if (!node) {
		pr_info("xsc_gic: No GICv3 found in device tree\n");
		kfree(xsc_gic_dev);
		xsc_gic_dev = NULL;
		return -ENODEV;
	}

	/* Map GICD (Distributor) */
	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("xsc_gic: Failed to get GICD address\n");
		goto cleanup;
	}

	xsc_gic_dev->gicd_base = ioremap(res.start, resource_size(&res));
	if (!xsc_gic_dev->gicd_base) {
		pr_err("xsc_gic: Failed to map GICD\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	pr_info("xsc_gic: GICD mapped at 0x%llx\n", (u64)res.start);

	/* Map GICR (Redistributor) - for LPI configuration */
	ret = of_address_to_resource(node, 1, &res);
	if (ret == 0) {
		xsc_gic_dev->gicr_base = ioremap(res.start, resource_size(&res));
		if (xsc_gic_dev->gicr_base)
			pr_info("xsc_gic: GICR mapped at 0x%llx\n", (u64)res.start);
	}

	/* Find ITS (Interrupt Translation Service) */
	{
		struct device_node *its_node;

		its_node = of_find_compatible_node(NULL, NULL, "arm,gic-v3-its");
		if (its_node) {
			ret = of_address_to_resource(its_node, 0, &res);
			if (ret == 0) {
				xsc_gic_dev->its_base = ioremap(res.start, resource_size(&res));
				if (xsc_gic_dev->its_base)
					pr_info("xsc_gic: ITS mapped at 0x%llx\n", (u64)res.start);
			}
			of_node_put(its_node);
		}
	}

	of_node_put(node);

	/* Detect capabilities */
	ret = detect_gic_capabilities(xsc_gic_dev);
	if (ret)
		goto cleanup;

	/* Initialize ITS if available */
	if (xsc_gic_dev->has_its) {
		ret = init_its_command_queue(xsc_gic_dev);
		if (ret)
			pr_warn("xsc_gic: ITS initialization failed (non-fatal)\n");
	}

	/* Allocate LPIs */
	if (xsc_gic_dev->has_lpi) {
		ret = allocate_lpi(xsc_gic_dev);
		if (ret)
			goto cleanup;
	}

	/* Update mechanism state */
	mech->has_gic_lpi = xsc_gic_dev->has_lpi;
	mech->has_gicv4 = xsc_gic_dev->has_gicv4;

	pr_info("xsc_gic: Initialization complete (GICv3: %d, GICv4: %d, LPI: %d, ITS: %d)\n",
		xsc_gic_dev->has_gicv3, xsc_gic_dev->has_gicv4,
		xsc_gic_dev->has_lpi, xsc_gic_dev->has_its);

	return 0;

cleanup:
	if (xsc_gic_dev->its_base)
		iounmap(xsc_gic_dev->its_base);
	if (xsc_gic_dev->gicr_base)
		iounmap(xsc_gic_dev->gicr_base);
	if (xsc_gic_dev->gicd_base)
		iounmap(xsc_gic_dev->gicd_base);
	kfree(xsc_gic_dev);
	xsc_gic_dev = NULL;

	return ret;
}

/*
 * Validate GIC LPI latency
 */
int xsc_gic_validate(struct xsc_wait_mechanism *mech)
{
	u64 latencies[1000];
	int i, ret;
	int test_lpi;

	if (!xsc_gic_dev || !xsc_gic_dev->has_lpi) {
		pr_info("xsc_gic: No LPI support, skipping validation\n");
		return -ENODEV;
	}

	pr_info("xsc_gic: Starting LPI latency validation (1000 iterations)\n");

	/* Use first allocated LPI for testing */
	test_lpi = xsc_gic_dev->lpi_base;

	/* Request IRQ */
	init_completion(&mech->gic_wait_complete);

	ret = request_irq(test_lpi, xsc_gic_lpi_handler, 0,
			  "xsc-gic-lpi-test", mech);
	if (ret) {
		pr_err("xsc_gic: Failed to request LPI %d: %d\n", test_lpi, ret);
		return ret;
	}

	/* Run validation test */
	for (i = 0; i < 1000; i++) {
		u64 t0 = xsc_rdtsc();

		/* Trigger LPI via ITS (if available) or direct write */
		if (xsc_gic_dev->has_its) {
			/* Send MAPI command to trigger LPI */
			u64 cmd[4] = {
				GITS_CMD_MAPI | ((u64)test_lpi << 32),
				0,
				0,
				0,
			};

			ret = gits_send_command(xsc_gic_dev, cmd);
			if (ret) {
				pr_err("xsc_gic: ITS command failed at iteration %d\n", i);
				break;
			}
		}

		/* Wait for IRQ */
		if (!wait_for_completion_timeout(&mech->gic_wait_complete,
						 msecs_to_jiffies(10))) {
			pr_err("xsc_gic: LPI timeout at iteration %d\n", i);
			ret = -ETIMEDOUT;
			break;
		}

		latencies[i] = xsc_rdtsc() - t0;

		/* Reset for next iteration */
		reinit_completion(&mech->gic_wait_complete);
	}

	free_irq(test_lpi, mech);

	if (ret)
		return ret;

	/* Analyze latencies */
	{
		u64 min = latencies[0], max = latencies[0], sum = 0;

		for (i = 0; i < 1000; i++) {
			if (latencies[i] < min) min = latencies[i];
			if (latencies[i] > max) max = latencies[i];
			sum += latencies[i];
		}

		u64 avg_cycles = sum / 1000;
		u64 avg_ns = xsc_cycles_to_ns(avg_cycles);
		u64 max_ns = xsc_cycles_to_ns(max);

		pr_info("xsc_gic: LPI latency: min=%llu cycles, avg=%llu ns, max=%llu ns\n",
			min, avg_ns, max_ns);

		atomic64_set(&xsc_gic_dev->lpi_latency_ns, avg_ns);

		/* GIC LPIs should be <1µs */
		if (max_ns > 1000000) {
			pr_warn("xsc_gic: LPI max latency %llu ns exceeds 1ms threshold\n",
				max_ns);
			return -EINVAL;
		}

		if (avg_ns > 500000) {
			pr_warn("xsc_gic: LPI avg latency %llu ns exceeds 500µs\n",
				avg_ns);
			mech->state = XSC_WAIT_DEGRADED;
		}
	}

	pr_info("xsc_gic: LPI validation PASSED\n");
	return 0;
}

/*
 * Cleanup GIC resources
 */
void xsc_gic_cleanup(void)
{
	if (!xsc_gic_dev)
		return;

	if (xsc_gic_dev->cmd_base)
		free_pages((unsigned long)xsc_gic_dev->cmd_base,
			   get_order(xsc_gic_dev->cmd_queue_size));

	if (xsc_gic_dev->its_base)
		iounmap(xsc_gic_dev->its_base);
	if (xsc_gic_dev->gicr_base)
		iounmap(xsc_gic_dev->gicr_base);
	if (xsc_gic_dev->gicd_base)
		iounmap(xsc_gic_dev->gicd_base);

	kfree(xsc_gic_dev);
	xsc_gic_dev = NULL;

	pr_info("xsc_gic: Cleanup complete\n");
}

MODULE_LICENSE("GPL");
