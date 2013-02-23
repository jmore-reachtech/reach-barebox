/*
 * reachfec.c - Reach command to read/write MAC address
 *
 * Copyright (c) 2012 Jeff Horn <jhorn@reachtech.com>, Reach Technology
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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

#include <common.h>
#include <command.h>
#include <fs.h>
#include <linux/stat.h>
#include <errno.h>
#include <malloc.h>
#include <getopt.h>
#include <stringlist.h>
#include <mach/iim.h>
#include <net.h>
#include <i2c/i2c.h>
#include <io.h>
#include <mach/imx-regs.h>
#include <mach/clock-imx51_53.h>

#define PMIC_I2C_BUS 0
#define PMIC_I2C_ADDR 0x48
#define PMIC_I2C_VDD_FUSE_REG 0x3A
#define PMIC_I2C_VDD_FUSE_DEFAULT 0x45
#define PMIC_I2C_VDD_FUSE_BLOW 0x67

static int set_vdd_fuse(int val)
{
	int ret 					= 0;
	struct i2c_adapter *adapter = NULL;
	struct i2c_client client;
	u8 fuse[1] 					= {0};
	int count 					= 1;


	adapter = i2c_get_adapter(PMIC_I2C_BUS);
	if (!adapter) {
		printf("i2c bus %d not found\n", PMIC_I2C_BUS);
		return -ENODEV;
	}

	client.adapter = adapter;
	client.addr = PMIC_I2C_ADDR;

	fuse[0] = val;
	ret = i2c_write_reg(&client, PMIC_I2C_VDD_FUSE_REG, fuse, count);
	if (ret == count) {
		printf("0x%02x \n", fuse[0]);
		ret = 0;
	} else {
		printf("error ret=%d\n",ret);
	}

	return ret;
}

static int display_vdd_fuse(void)
{
	int ret 					= 0;
	struct i2c_adapter *adapter = NULL;
	struct i2c_client client;
	u8 fuse[1]					= {0};
	int count 					= 1;


	adapter = i2c_get_adapter(PMIC_I2C_BUS);
	if (!adapter) {
		printf("i2c bus %d not found\n", PMIC_I2C_BUS);
		return -ENODEV;
	}

	client.adapter = adapter;
	client.addr = PMIC_I2C_ADDR;

	ret = i2c_read_reg(&client, PMIC_I2C_VDD_FUSE_REG, fuse, count);
	if (ret == count) {
		printf("0x%02x \n", fuse[0]);
		ret = 0;
	} else {
		printf("error ret=%d\n",ret);
	}

	return ret;
}

static int do_fec_read(void)
{
	int ret;
	char buf[6]	= {0};

	ret = imx_iim_read(1,9,buf,6);
	printf("read mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
			buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

	return 0;
}

static int do_fec_write(char *ether_addr)
{
	int ret;
	u8 ether[6]		= {0};
	u32 reg;
	char buf[6]		= {0};

	ret = string_to_ethaddr(ether_addr,ether);
	ret = imx_iim_read(1,9,buf,6);
	if(buf[0] != 0x0) {
		printf("mac already set: %02x:%02x:%02x:%02x:%02x:%02x\n",
				buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
		return 0;
	}

	if(ret) {
		printf("invalid MAC address\n");
		return -1;
	}

	ret = is_valid_ether_addr(ether);
	if(!ret) {
		printf("invalid MAC address\n");
		return -1;
	}

	printf("writing mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
			ether[0],ether[1],ether[2],ether[3],ether[4],ether[5]);

	/* set the efuse_prog_supply_gate bit */
	reg = readl(MX53_CCM_BASE_ADDR + MX5_CCM_CGPR);
	printf("efuse gate reg=0x%08X\n",reg);
	reg |= 0x000000010;
	writel(reg,(MX53_CCM_BASE_ADDR + MX5_CCM_CGPR));

	set_vdd_fuse(PMIC_I2C_VDD_FUSE_BLOW);
	udelay(10000);
	ret = imx_iim_write(1, 9, ether, 6);
	if (ret != 6) {
		printf("error writing to iim\n");
		return -EINVAL;
	}
	set_vdd_fuse(PMIC_I2C_VDD_FUSE_DEFAULT);
	udelay(10000);

	/* clear the efuse_prog_supply_gate bit */
	reg = readl(MX53_CCM_BASE_ADDR + MX5_CCM_CGPR);
	printf("efuse gate reg=0x%08X\n",reg);
	reg &= ~(0x000000010);
	writel(reg,(MX53_CCM_BASE_ADDR + MX5_CCM_CGPR));

	/* re-read the mac */
	ret = imx_iim_read(1,9,buf,6);
	printf("wrote mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
					buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

	return 0;
}

static int do_reach_fec(int argc, char *argv[])
{
	char *mac       = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "w:r?")) > 0) {
		switch (opt) {
		case 'r':
			do_fec_read();
			break;
		case 'w':
			mac = optarg;
			do_fec_write(optarg);
			break;
		case '?':
			printf("usage \n");
			return 0;
		}
	}

	return 0;
}

BAREBOX_CMD_HELP_START(reach_fec)
BAREBOX_CMD_HELP_USAGE("reach_fec [OPTIONS] \n")
BAREBOX_CMD_HELP_SHORT("Read/Write the MAC address\n")
BAREBOX_CMD_HELP_OPT  ("read",  "display the MAC address\n")
BAREBOX_CMD_HELP_OPT  ("burn <0x##:0x##:0x##:0x##:0x##:0x##>",  "burn the MAC address\n")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(reach_fec)
	.cmd		= do_reach_fec,
	.usage		= "read/write the MAC address",
	BAREBOX_CMD_HELP(cmd_reach_fec_help)
BAREBOX_CMD_END
