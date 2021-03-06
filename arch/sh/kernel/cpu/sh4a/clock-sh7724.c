/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7724.c
 *
 * SH7724 clock framework support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <asm/clock.h>
#include <asm/hwblk.h>
#include <cpu/sh7724.h>

/* SH7724 registers */
#define FRQCRA		0xa4150000
#define FRQCRB		0xa4150004
#define VCLKCR		0xa4150048
#define FCLKACR		0xa4150008
#define FCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define SPUCLKCR	0xa415003c
#define FLLFRQ		0xa4150050
#define LSTATS		0xa4150060

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
struct clk extal_clk = {
	.rate		= 33333333,
};

/* The fll multiplies the 32khz r_clk, may be used instead of extal */
static unsigned long fll_recalc(struct clk *clk)
{
	unsigned long mult = 0;
	unsigned long div = 1;

	if (__raw_readl(PLLCR) & 0x1000)
		mult = __raw_readl(FLLFRQ) & 0x3ff;

	if (__raw_readl(FLLFRQ) & 0x4000)
		div = 2;

	return (clk->parent->rate * mult) / div;
}

static struct clk_ops fll_clk_ops = {
	.recalc		= fll_recalc,
};

static struct clk fll_clk = {
	.ops		= &fll_clk_ops,
	.parent		= &r_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLCR) & 0x4000)
		mult = (((__raw_readl(FRQCRA) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
}

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
};

/* A fixed divide-by-3 block use by the div6 clocks */
static unsigned long div3_recalc(struct clk *clk)
{
	return clk->parent->rate / 3;
}

static struct clk_ops div3_clk_ops = {
	.recalc		= div3_recalc,
};

static struct clk div3_clk = {
	.ops		= &div3_clk_ops,
	.parent		= &pll_clk,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&fll_clk,
	&pll_clk,
	&div3_clk,
};

static void div4_kick(struct clk *clk)
{
	unsigned long value;

	/* set KICK bit in FRQCRA to update hardware setting */
	value = __raw_readl(FRQCRA);
	value |= (1 << 31);
	__raw_writel(value, FRQCRA);
}

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 0, 24, 32, 36, 48, 0, 72 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
	.kick = div4_kick,
};

enum { DIV4_I, DIV4_SH, DIV4_B, DIV4_P, DIV4_M1, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCRA, 20, 0x2f7d, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(FRQCRA, 12, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCRA, 8, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4(FRQCRA, 0, 0x2f7c, 0),
	[DIV4_M1] = DIV4(FRQCRB, 4, 0x2f7c, CLK_ENABLE_ON_INIT),
};

enum { DIV6_V, DIV6_FA, DIV6_FB, DIV6_I, DIV6_S, DIV6_NR };

struct clk div6_clks[DIV6_NR] = {
	[DIV6_V] = SH_CLK_DIV6(&div3_clk, VCLKCR, 0),
	[DIV6_FA] = SH_CLK_DIV6(&div3_clk, FCLKACR, 0),
	[DIV6_FB] = SH_CLK_DIV6(&div3_clk, FCLKBCR, 0),
	[DIV6_I] = SH_CLK_DIV6(&div3_clk, IRDACLKCR, 0),
	[DIV6_S] = SH_CLK_DIV6(&div3_clk, SPUCLKCR, CLK_ENABLE_ON_INIT),
};

static struct clk mstp_clks[HWBLK_NR] = {
	SH_HWBLK_CLK(HWBLK_TLB, &div4_clks[DIV4_I], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_IC, &div4_clks[DIV4_I], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_OC, &div4_clks[DIV4_I], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_RSMEM, &div4_clks[DIV4_B], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_ILMEM, &div4_clks[DIV4_I], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_L2C, &div4_clks[DIV4_SH], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_FPU, &div4_clks[DIV4_I], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_INTC, &div4_clks[DIV4_P], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_DMAC0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_SHYWAY, &div4_clks[DIV4_SH], CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK(HWBLK_HUDI, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_UBC, &div4_clks[DIV4_I], 0),
	SH_HWBLK_CLK(HWBLK_TMU0, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_CMT, &r_clk, 0),
	SH_HWBLK_CLK(HWBLK_RWDT, &r_clk, 0),
	SH_HWBLK_CLK(HWBLK_DMAC1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_TMU1, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_SCIF0, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_SCIF1, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_SCIF2, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_SCIF3, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_SCIF4, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_SCIF5, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_MSIOF0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_MSIOF1, &div4_clks[DIV4_B], 0),

	SH_HWBLK_CLK(HWBLK_KEYSC, &r_clk, 0),
	SH_HWBLK_CLK(HWBLK_RTC, &r_clk, 0),
	SH_HWBLK_CLK(HWBLK_IIC0, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_IIC1, &div4_clks[DIV4_P], 0),

	SH_HWBLK_CLK(HWBLK_MMC, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_ETHER, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_ATAPI, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_TPU, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_IRDA, &div4_clks[DIV4_P], 0),
	SH_HWBLK_CLK(HWBLK_TSIF, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_USB1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_USB0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_2DG, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_SDHI0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_SDHI1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_VEU1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_CEU1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_BEU1, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_2DDMAC, &div4_clks[DIV4_SH], 0),
	SH_HWBLK_CLK(HWBLK_SPU, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_JPU, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_VOU, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_BEU0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_CEU0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_VEU0, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_VPU, &div4_clks[DIV4_B], 0),
	SH_HWBLK_CLK(HWBLK_LCDC, &div4_clks[DIV4_B], 0),
};

#define CLKDEV_CON_ID(_id, _clk) { .con_id = _id, .clk = _clk }

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("fll_clk", &fll_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),
	CLKDEV_CON_ID("div3_clk", &div3_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("vpu_clk", &div4_clks[DIV4_M1]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("video_clk", &div6_clks[DIV6_V]),
	CLKDEV_CON_ID("fsia_clk", &div6_clks[DIV6_FA]),
	CLKDEV_CON_ID("fsib_clk", &div6_clks[DIV6_FB]),
	CLKDEV_CON_ID("irda_clk", &div6_clks[DIV6_I]),
	CLKDEV_CON_ID("spu_clk", &div6_clks[DIV6_S]),

	/* MSTP clocks */
	CLKDEV_CON_ID("tlb0", &mstp_clks[HWBLK_TLB]),
	CLKDEV_CON_ID("ic0", &mstp_clks[HWBLK_IC]),
	CLKDEV_CON_ID("oc0", &mstp_clks[HWBLK_OC]),
	CLKDEV_CON_ID("rs0", &mstp_clks[HWBLK_RSMEM]),
	CLKDEV_CON_ID("ilmem0", &mstp_clks[HWBLK_ILMEM]),
	CLKDEV_CON_ID("l2c0", &mstp_clks[HWBLK_L2C]),
	CLKDEV_CON_ID("fpu0", &mstp_clks[HWBLK_FPU]),
	CLKDEV_CON_ID("intc0", &mstp_clks[HWBLK_INTC]),
	CLKDEV_CON_ID("dmac0", &mstp_clks[HWBLK_DMAC0]),
	CLKDEV_CON_ID("sh0", &mstp_clks[HWBLK_SHYWAY]),
	CLKDEV_CON_ID("hudi0", &mstp_clks[HWBLK_HUDI]),
	CLKDEV_CON_ID("ubc0", &mstp_clks[HWBLK_UBC]),
	{
		/* TMU0 */
		.dev_id		= "sh_tmu.0",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU0],
	}, {
		/* TMU1 */
		.dev_id		= "sh_tmu.1",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU0],
	}, {
		/* TMU2 */
		.dev_id		= "sh_tmu.2",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU0],
	}, {
		/* TMU3 */
		.dev_id		= "sh_tmu.3",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU1],
	},
	CLKDEV_CON_ID("cmt_fck", &mstp_clks[HWBLK_CMT]),
	CLKDEV_CON_ID("rwdt0", &mstp_clks[HWBLK_RWDT]),
	CLKDEV_CON_ID("dmac1", &mstp_clks[HWBLK_DMAC1]),
	{
		/* TMU4 */
		.dev_id		= "sh_tmu.4",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU1],
	}, {
		/* TMU5 */
		.dev_id		= "sh_tmu.5",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[HWBLK_TMU1],
	}, {
		/* SCIF0 */
		.dev_id		= "sh-sci.0",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF0],
	}, {
		/* SCIF1 */
		.dev_id		= "sh-sci.1",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF1],
	}, {
		/* SCIF2 */
		.dev_id		= "sh-sci.2",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF2],
	}, {
		/* SCIF3 */
		.dev_id		= "sh-sci.3",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF3],
	}, {
		/* SCIF4 */
		.dev_id		= "sh-sci.4",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF4],
	}, {
		/* SCIF5 */
		.dev_id		= "sh-sci.5",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[HWBLK_SCIF5],
	},
	CLKDEV_CON_ID("msiof0", &mstp_clks[HWBLK_MSIOF0]),
	CLKDEV_CON_ID("msiof1", &mstp_clks[HWBLK_MSIOF1]),
	CLKDEV_CON_ID("keysc0", &mstp_clks[HWBLK_KEYSC]),
	CLKDEV_CON_ID("rtc0", &mstp_clks[HWBLK_RTC]),
	CLKDEV_CON_ID("i2c0", &mstp_clks[HWBLK_IIC0]),
	CLKDEV_CON_ID("i2c1", &mstp_clks[HWBLK_IIC1]),
	CLKDEV_CON_ID("mmc0", &mstp_clks[HWBLK_MMC]),
	CLKDEV_CON_ID("eth0", &mstp_clks[HWBLK_ETHER]),
	CLKDEV_CON_ID("atapi0", &mstp_clks[HWBLK_ATAPI]),
	CLKDEV_CON_ID("tpu0", &mstp_clks[HWBLK_TPU]),
	CLKDEV_CON_ID("irda0", &mstp_clks[HWBLK_IRDA]),
	CLKDEV_CON_ID("tsif0", &mstp_clks[HWBLK_TSIF]),
	CLKDEV_CON_ID("usb1", &mstp_clks[HWBLK_USB1]),
	CLKDEV_CON_ID("usb0", &mstp_clks[HWBLK_USB0]),
	CLKDEV_CON_ID("2dg0", &mstp_clks[HWBLK_2DG]),
	CLKDEV_CON_ID("sdhi0", &mstp_clks[HWBLK_SDHI0]),
	CLKDEV_CON_ID("sdhi1", &mstp_clks[HWBLK_SDHI1]),
	CLKDEV_CON_ID("veu1", &mstp_clks[HWBLK_VEU1]),
	CLKDEV_CON_ID("ceu1", &mstp_clks[HWBLK_CEU1]),
	CLKDEV_CON_ID("beu1", &mstp_clks[HWBLK_BEU1]),
	CLKDEV_CON_ID("2ddmac0", &mstp_clks[HWBLK_2DDMAC]),
	CLKDEV_CON_ID("spu0", &mstp_clks[HWBLK_SPU]),
	CLKDEV_CON_ID("jpu0", &mstp_clks[HWBLK_JPU]),
	CLKDEV_CON_ID("vou0", &mstp_clks[HWBLK_VOU]),
	CLKDEV_CON_ID("beu0", &mstp_clks[HWBLK_BEU0]),
	CLKDEV_CON_ID("ceu0", &mstp_clks[HWBLK_CEU0]),
	CLKDEV_CON_ID("veu0", &mstp_clks[HWBLK_VEU0]),
	CLKDEV_CON_ID("vpu0", &mstp_clks[HWBLK_VPU]),
	CLKDEV_CON_ID("lcdc0", &mstp_clks[HWBLK_LCDC]),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	/* autodetect extal or fll configuration */
	if (__raw_readl(PLLCR) & 0x1000)
		pll_clk.parent = &fll_clk;
	else
		pll_clk.parent = &extal_clk;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_hwblk_clk_register(mstp_clks, HWBLK_NR);

	return ret;
}
