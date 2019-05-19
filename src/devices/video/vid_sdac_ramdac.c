/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		87C716 'SDAC' true colour RAMDAC emulation.
 *
 * Version:	@(#)vid_sdac_ramdac.c	1.0.9	2019/05/17
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../../emu.h"
#include "../../timer.h"
#include "../../mem.h"
#include "../../device.h"
#include "../../plat.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_sdac_ramdac.h"


static void
sdac_control_write(sdac_ramdac_t *dev, svga_t *svga, uint8_t val)
{
    dev->command = val;

    switch (val >> 4) {
	case 0x2:
	case 0x3:
	case 0xa:
		svga->bpp = 15;
		break;
	case 0x4:
	case 0xe:
		svga->bpp = 24;
		break;
	case 0x5:
	case 0x6:
	case 0xc:
		svga->bpp = 16;
		break;
	case 0x7:
		svga->bpp = 32;
		break;
	case 0x0:
	case 0x1:
	default:
		svga->bpp = 8;
		break;
    }
}


static void
sdac_reg_write(sdac_ramdac_t *dev, int reg, uint8_t val)
{
    if ((reg >= 2 && reg <= 7) || (reg == 0xa) || (reg == 0xe)) {
	if (!dev->reg_ff)
		dev->regs[reg] = (dev->regs[reg] & 0xff00) | val;
	else
		dev->regs[reg] = (dev->regs[reg] & 0x00ff) | (val << 8);
    }
    dev->reg_ff = !dev->reg_ff;
    if (!dev->reg_ff)
	dev->windex++;
}


static uint8_t
sdac_reg_read(sdac_ramdac_t *dev, int reg)
{
    uint8_t temp;

    if (!dev->reg_ff)
	temp = dev->regs[reg] & 0xff;
    else
	temp = dev->regs[reg] >> 8;
    dev->reg_ff = !dev->reg_ff;
    if (!dev->reg_ff)
	dev->rindex++;

    return temp;
}


void
sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, sdac_ramdac_t *dev, svga_t *svga)
{
    uint8_t rs = (addr & 0x03);
    rs |= (!!rs2 << 8);

    switch (rs) {
	case 0x02:
		if (dev->magic_count == 4)
			sdac_control_write(dev, svga, val);
		/*FALLTHROUGH*/
	case 0x00:
	case 0x01:
	case 0x03:
		dev->magic_count = 0;
		break;
	case 0x04:
		dev->windex = val;
		dev->reg_ff = 0;
		break;
	case 0x05:
		sdac_reg_write(dev, dev->windex & 0xff, val);
		break;
	case 0x06:
		sdac_control_write(dev, svga, val);
		break;
	case 0x07:
		dev->rindex = val;
		dev->reg_ff = 0;
		break;
    }

    svga_out(addr, val, svga);
}


uint8_t
sdac_ramdac_in(uint16_t addr, int rs2, sdac_ramdac_t *dev, svga_t *svga)
{
    uint8_t temp = 0xff;
    uint8_t rs = (addr & 0x03);
    rs |= (!!rs2 << 8);

    switch (rs) {
	case 0x02:
		if (dev->magic_count < 5)
			dev->magic_count++;
		if (dev->magic_count == 4)
			temp = 0x70; /*SDAC ID*/
		else if (dev->magic_count == 5) {
			temp = dev->command;
			dev->magic_count = 0;
		} else
			temp = svga_in(addr, svga);
		break;
	case 0x00:
	case 0x01:
	case 0x03:
		dev->magic_count=0;
		temp = svga_in(addr, svga);
		break;
	case 0x04:
		temp = dev->windex;
		break;
	case 0x05:
		temp = sdac_reg_read(dev, dev->rindex & 0xff);
		break;
	case 0x06:
		temp = dev->command;
		break;
	case 0x07:
		temp = dev->rindex;
		break;
    }

    return temp;
}


float
sdac_getclock(int clock, void *priv)
{
    sdac_ramdac_t *dev = (sdac_ramdac_t *)priv;
    float t;
    int m, n1, n2;

    if (clock == 0)
	return 25175000.0;
    if (clock == 1)
	return 28322000.0;

    clock ^= 1; /*Clocks 2 and 3 seem to be reversed*/
    m  =  (dev->regs[clock] & 0x7f) + 2;
    n1 = ((dev->regs[clock] >>  8) & 0x1f) + 2;
    n2 = ((dev->regs[clock] >> 13) & 0x07);
    n2 = (1 << n2);
    t = (14318184.0f * (float)m) / (float)(n1 * n2);

    return t;
}


static void *
sdac_init(const device_t *info, UNUSED(void *parent))
{
    sdac_ramdac_t *dev = (sdac_ramdac_t *)mem_alloc(sizeof(sdac_ramdac_t));
    memset(dev, 0x00, sizeof(sdac_ramdac_t));

    dev->regs[0] = 0x6128;
    dev->regs[1] = 0x623d;

    return dev;
}


static void
sdac_close(void *priv)
{
    sdac_ramdac_t *dev = (sdac_ramdac_t *)priv;

    if (dev != NULL)
	free(dev);
}


const device_t sdac_ramdac_device = {
    "S3 SDAC 86c716 RAMDAC",
    0, 0, NULL,
    sdac_init, sdac_close, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};
