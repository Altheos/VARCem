/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the SMC FDC37C663 and FDC37C665 Super
 *		I/O Chips.
 *
 * Version:	@(#)sio_fdc37c66x.c	1.0.8	2018/09/19
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include <wchar.h>
#include "../../emu.h"
#include "../../io.h"
#include "../../device.h"
#include "../system/pci.h"
#include "../ports/parallel.h"
#include "../ports/serial.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "sio.h"


static uint8_t fdc37c66x_lock[2];
static int fdc37c66x_curreg;
static uint8_t fdc37c66x_regs[16];
static int com3_addr, com4_addr;
static fdc_t *fdc37c66x_fdc;


static void
write_lock(uint8_t val)
{
    if (val == 0x55 && fdc37c66x_lock[1] == 0x55)
	fdc_3f1_enable(fdc37c66x_fdc, 0);
    if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55 && val != 0x55)
	fdc_3f1_enable(fdc37c66x_fdc, 1);

    fdc37c66x_lock[0] = fdc37c66x_lock[1];
    fdc37c66x_lock[1] = val;
}


static void
ide_handler(void)
{
#if 0
    uint16_t or_value = 0;

    if ((romset == ROM_440FX) ||
	(romset == ROM_R418) ||
	(romset == ROM_MB500N)) return;

    ide_pri_disable();

    if (fdc37c66x_regs[0] & 1) {
	if (fdc37c66x_regs[5] & 2)
		or_value = 0;
	  else
		or_value = 0x800;
	ide_set_base(0, 0x170 | or_value);
	ide_set_side(0, 0x376 | or_value);
	ide_pri_enable();
    }
#endif
}


static void
set_com34_addr(void)
{
    switch (fdc37c66x_regs[1] & 0x60) {
	case 0x00:
		com3_addr = 0x338;
		com4_addr = 0x238;
		break;

	case 0x20:
		com3_addr = 0x3e8;
		com4_addr = 0x2e8;
		break;

	case 0x40:
		com3_addr = 0x3e8;
		com4_addr = 0x2e0;
		break;

	case 0x60:
		com3_addr = 0x220;
		com4_addr = 0x228;
		break;
    }
}


static void
set_serial1_addr(void)
{
    if (fdc37c66x_regs[2] & 4) {
	switch (fdc37c66x_regs[2] & 3) {
		case 0:
			serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
			break;

		case 1:
			serial_setup(1, SERIAL2_ADDR, SERIAL2_IRQ);
			break;

		case 2:
			serial_setup(1, com3_addr, 4);
			break;

		case 3:
			serial_setup(1, com4_addr, 3);
			break;
	}
    }
}


static void
set_serial2_addr(void)
{
    if (fdc37c66x_regs[2] & 0x40) {
	switch (fdc37c66x_regs[2] & 0x30) {
		case 0:
			serial_setup(2, SERIAL1_ADDR, SERIAL1_IRQ);
			break;

		case 1:
			serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
			break;

		case 2:
			serial_setup(2, com3_addr, 4);
			break;

		case 3:
			serial_setup(2, com4_addr, 3);
			break;
	}
    }
}


static void
lpt1_handler(void)
{
//FIXME:	parallel_remove(1);

    switch (fdc37c66x_regs[1] & 3) {
	case 1:
		parallel_setup(1, 0x3bc);
		break;

	case 2:
		parallel_setup(1, 0x378);
		break;

	case 3:
		parallel_setup(1, 0x278);
		break;
    }
}


static void
fdc37c66x_write(uint16_t port, uint8_t val, void *priv)
{
    uint8_t valxor = 0;

    if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55) {
	if (port == 0x3f0) {
		if (val == 0xaa)
			write_lock(val);
		else
			fdc37c66x_curreg = val;
#if 0
		if (fdc37c66x_curreg != 0)
			fdc37c66x_curreg = val & 0xf;
		else {
			/* Hardcode the IDE to AT type. */
			fdc37c66x_curreg = (val & 0xf) | 2;
		}
#endif
	} else {
		if (fdc37c66x_curreg > 15)
			return;

		valxor = val ^ fdc37c66x_regs[fdc37c66x_curreg];
		fdc37c66x_regs[fdc37c66x_curreg] = val;

		switch(fdc37c66x_curreg) {
			case 0:
				if (valxor & 1)
					ide_handler();
				break;

			case 1:
				if (valxor & 3)
					lpt1_handler();

				if (valxor & 0x60) {
#if 0
	                                serial_remove(1);
#endif
					set_com34_addr();
					set_serial1_addr();
					set_serial2_addr();
				}
				break;

			case 2:
				if (valxor & 7) {
#if 0
	                                serial_remove(1);
#endif
					set_serial1_addr();
				}
				if (valxor & 0x70) {
#if 0
					serial_remove(2);
#endif
					set_serial2_addr();
				}
				break;

			case 3:
				if (valxor & 2)
					fdc_update_enh_mode(fdc37c66x_fdc, (fdc37c66x_regs[3] & 2) ? 1 : 0);
				break;

			case 5:
				if (valxor & 2)
					ide_handler();

				if (valxor & 0x18)
					fdc_update_densel_force(fdc37c66x_fdc, (fdc37c66x_regs[5] & 0x18) >> 3);

				if (valxor & 0x20)
					fdc_set_swap(fdc37c66x_fdc, (fdc37c66x_regs[5] & 0x20) >> 5);
				break;
		}
	}
    } else {
	if (port == 0x3f0)
		write_lock(val);
    }
}


static uint8_t
fdc37c66x_read(uint16_t port, void *priv)
{
    if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55) {
	if (port == 0x3f1)
		return fdc37c66x_regs[fdc37c66x_curreg];
    }

    return 0xff;
}


static void
fdc37c66x_reset(void)
{
    com3_addr = 0x338;
    com4_addr = 0x238;

#if 0
    serial_remove(1);
#endif
    serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);

#if 0
    serial_remove(2);
#endif
    serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);

//FIXME:	parallel_remove(1);
//FIXME:	parallel_remove(2);
    parallel_setup(1, 0x378);

    fdc_reset(fdc37c66x_fdc);
        
    memset(fdc37c66x_lock, 0, 2);
    memset(fdc37c66x_regs, 0, 16);
    fdc37c66x_regs[0x0] = 0x3a;
    fdc37c66x_regs[0x1] = 0x9f;
    fdc37c66x_regs[0x2] = 0xdc;
    fdc37c66x_regs[0x3] = 0x78;
    fdc37c66x_regs[0x6] = 0xff;
    fdc37c66x_regs[0xe] = 0x01;
}


static void
fdc37c663_reset(void)
{
    fdc37c66x_reset();

    fdc37c66x_regs[0xd] = 0x63;
}


static void
fdc37c665_reset(void)
{
    fdc37c66x_reset();

    fdc37c66x_regs[0xd] = 0x65;
}


void
fdc37c663_init(void)
{
    fdc37c66x_fdc = (fdc_t *)device_add(&fdc_at_smc_device);

    io_sethandler(0x03f0, 2,
		  fdc37c66x_read,NULL,NULL, fdc37c66x_write,NULL,NULL, NULL);

    fdc37c663_reset();
}


void
fdc37c665_init(void)
{
    fdc37c66x_fdc = (fdc_t *)device_add(&fdc_at_smc_device);

    io_sethandler(0x03f0, 2,
		  fdc37c66x_read,NULL,NULL, fdc37c66x_write,NULL,NULL, NULL);

    fdc37c665_reset();
}
