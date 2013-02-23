/*
 * Copyright (C) 2007 Sascha Hauer, Pengutronix
 * Copyright (C) 2011 Marc Kleine-Budde <mkl@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <environment.h>
#include <fcntl.h>
#include <fec.h>
#include <fs.h>
#include <init.h>
#include <nand.h>
#include <net.h>
#include <partition.h>
#include <sizes.h>

#include <generated/mach-types.h>

#include <mach/imx-regs.h>
#include <mach/iomux-mx53.h>
#include <mach/devices-imx53.h>
#include <mach/generic.h>
#include <mach/gpio.h>
#include <mach/imx-nand.h>
#include <mach/iim.h>
#include <mach/imx5.h>

#include <i2c/i2c.h>
#include <mfd/mc34708.h>

#include <asm/armlinux.h>
#include <io.h>
#include <asm/mmu.h>

static struct fec_platform_data fec_info = {
	.xcv_type = RMII,
};

static struct pad_desc sellwood_pads[] = {
	/* UART1 */
	MX53_PAD_CSI0_DAT10__UART1_TXD_MUX,
	MX53_PAD_CSI0_DAT11__UART1_RXD_MUX,

	/* UART2 */
	MX53_PAD_PATA_DMARQ__UART2_TXD_MUX,
	MX53_PAD_PATA_BUFFER_EN__UART2_RXD_MUX,

	/* UART3 */
	MX53_PAD_PATA_CS_0__UART3_TXD_MUX,
	MX53_PAD_PATA_CS_1__UART3_RXD_MUX,

	/* FEC */
	MX53_PAD_FEC_MDC__FEC_MDC,
	MX53_PAD_FEC_MDIO__FEC_MDIO,
	MX53_PAD_FEC_REF_CLK__FEC_TX_CLK,
	MX53_PAD_FEC_RX_ER__FEC_RX_ER,
	MX53_PAD_FEC_CRS_DV__FEC_RX_DV,
	MX53_PAD_FEC_RXD1__FEC_RDATA_1,
	MX53_PAD_FEC_RXD0__FEC_RDATA_0,
	MX53_PAD_FEC_TX_EN__FEC_TX_EN,
	MX53_PAD_FEC_TXD1__FEC_TDATA_1,
	MX53_PAD_FEC_TXD0__FEC_TDATA_0,
	/* FEC_nRST */
	MX53_PAD_PATA_DA_0__GPIO7_6,
	/* FEC_nINT */
	MX53_PAD_PATA_DATA4__GPIO2_4,
	
	/* SD1 */
	MX53_PAD_SD1_CMD__ESDHC1_CMD,
	MX53_PAD_SD1_CLK__ESDHC1_CLK,
	MX53_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX53_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX53_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX53_PAD_SD1_DATA3__ESDHC1_DAT3,
	
	/* SD2 */
	MX53_PAD_SD2_CMD__ESDHC2_CMD,
	MX53_PAD_SD2_CLK__ESDHC2_CLK,
	MX53_PAD_SD2_DATA0__ESDHC2_DAT0,
	MX53_PAD_SD2_DATA1__ESDHC2_DAT1,
	MX53_PAD_SD2_DATA2__ESDHC2_DAT2,
	MX53_PAD_SD2_DATA3__ESDHC2_DAT3,

	/* I2C1 */
	MX53_PAD_CSI0_DAT8__I2C1_SDA,
	MX53_PAD_CSI0_DAT9__I2C1_SCL,
};

static struct i2c_board_info i2c_devices[] = {
	{
		I2C_BOARD_INFO("mc34708-i2c", 0x08),
	},
};

/*
 * Revision to be passed to kernel. The kernel provided
 * by freescale relies on this.
 *
 * C --> CPU type
 * S --> Silicon revision
 * B --> Board rev
 *
 * 31    20     16     12    8      4     0
 *        | Cmaj | Cmin | B  | Smaj | Smin|
 *
 * e.g 0x00053120 --> i.MX35, Cpu silicon rev 2.0, Board rev 2
*/
static unsigned int sellwood_system_rev = 0x00053000;

static void set_silicon_rev( int rev)
{
	sellwood_system_rev = sellwood_system_rev | (rev & 0xFF);
}

static void set_board_rev(int rev)
{
	sellwood_system_rev =  (sellwood_system_rev & ~(0xF << 8)) | (rev & 0xF) << 8;
}

static int sellwood_mem_init(void)
{
	arm_add_mem_device("ram0", 0x70000000, SZ_512M);

	return 0;
}
mem_initcall(sellwood_mem_init);

#define SELLWOOD_FEC_PHY_RST		IMX_GPIO_NR(7, 6)

static void sellwood_fec_reset(void)
{
	gpio_direction_output(SELLWOOD_FEC_PHY_RST, 0);
	mdelay(1);
	gpio_set_value(SELLWOOD_FEC_PHY_RST, 1);
}

static struct esdhc_platform_data sellwood_sd1_data = {
	.cd_type = ESDHC_CD_NONE,
	.wp_type = ESDHC_WP_NONE,
};

static struct esdhc_platform_data sellwood_sd2_data = {
	.cd_type = ESDHC_CD_NONE,
	.wp_type = ESDHC_WP_NONE,
};

static int sellwood_devices_init(void)
{
	imx53_iim_register_fec_ethaddr();
	imx53_add_fec(&fec_info);
	imx53_add_mmc0(&sellwood_sd1_data);
	imx53_add_mmc1(&sellwood_sd2_data);
	i2c_register_board_info(0, i2c_devices, ARRAY_SIZE(i2c_devices));
	imx53_add_i2c0(NULL);

	sellwood_fec_reset();

	set_silicon_rev(imx_silicon_revision());

	armlinux_set_bootparams((void *)0x70000100);
	armlinux_set_architecture(MACH_TYPE_MX53_SELLWOOD);

	return 0;
}

device_initcall(sellwood_devices_init);

static int sellwood_part_init(void)
{
	devfs_add_partition("disk0", 0x00000, 0x40000, PARTITION_FIXED, "self0");
	devfs_add_partition("disk0", 0x40000, 0x20000, PARTITION_FIXED, "env0");

	return 0;
}
late_initcall(sellwood_part_init);

static int sellwood_console_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(sellwood_pads, ARRAY_SIZE(sellwood_pads));

	imx53_init_lowlevel(1000);

#if defined CONFIG_MX53_SELLWOOD_DEBUG_UART0
	imx53_add_uart0();
#endif	
#if defined CONFIG_MX53_SELLWOOD_DEBUG_UART1
	imx53_add_uart1();
#endif
#if defined CONFIG_MX53_SELLWOOD_DEBUG_UART2
	imx53_add_uart2();
#endif

	return 0;
}

console_initcall(sellwood_console_init);

static int sellwood_pmic_init(void)
{
	struct mc34708 *mc34708;
	int rev;

	mc34708 = mc34708_get();
	if (!mc34708) {
		/* so we have a DA9053 based board */
		printf("MCIMX53-START board 1.0\n");
		armlinux_set_revision(sellwood_system_rev);
		return 0;
	}

	/* get the board revision from fuse */
	rev = readl(MX53_IIM_BASE_ADDR + 0x878);
	set_board_rev(rev);
	printf("MCIMX53-START-R board 1.0 rev %c\n", (rev == 1) ? 'A' : 'B' );
	armlinux_set_revision(sellwood_system_rev);

	return 0;
}

late_initcall(sellwood_pmic_init);
