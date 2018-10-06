/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the SMC FDC37C669 Super I/O Chip.
 *
 * Version:	@(#)sio_fdc37c669.c	1.0.8	2018/09/19
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
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


static int fdc37c669_locked;
static int fdc37c669_rw_locked = 0;
static int fdc37c669_curreg = 0;
static uint8_t fdc37c669_regs[42];
static uint8_t tries;
static fdc_t *fdc37c669_fdc;


static uint16_t make_port(uint8_t reg)
{
	uint16_t p = 0;

	uint16_t mask = 0;

	switch(reg)
	{
		case 0x20:
		case 0x21:
		case 0x22:
			mask = 0xfc;
			break;
		case 0x23:
			mask = 0xff;
			break;
		case 0x24:
		case 0x25:
			mask = 0xfe;
			break;
	}

	p = ((uint16_t) (fdc37c669_regs[reg] & mask)) << 2;
	if (reg == 0x22)
	{
		p |= 6;
	}

	return p;
}

void fdc37c669_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint8_t max = 42;

	if (index)
	{
		if ((val == 0x55) && !fdc37c669_locked)
		{
			if (tries)
			{
				fdc37c669_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c669_locked)
			{
				if (val < max)  fdc37c669_curreg = val;
				if (val == 0xaa)  fdc37c669_locked = 0;
			}
			else
			{
				if (tries)
					tries = 0;
			}
		}
	}
	else
	{
		if (fdc37c669_locked)
		{
			if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return;
			if ((fdc37c669_curreg >= 0x26) && (fdc37c669_curreg <= 0x27))  return;
			if (fdc37c669_curreg == 0x29)  return;
			valxor = val ^ fdc37c669_regs[fdc37c669_curreg];
			fdc37c669_regs[fdc37c669_curreg] = val;
			goto process_value;
		}
	}
	return;

process_value:
	switch(fdc37c669_curreg)
	{
		case 0:
#if 0
			if (valxor & 3)
			{
				ide_pri_disable();
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable();
				break;
			}
#endif
			if (valxor & 8)
			{
				fdc_remove(fdc37c669_fdc);
				if ((fdc37c669_regs[0] & 8) && (fdc37c669_regs[0x20] & 0xc0))  fdc_set_base(fdc37c669_fdc, make_port(0x20));
			}
			break;
		case 1:
			if (valxor & 4)
			{
//FIXME:				parallel_remove(1);
				if ((fdc37c669_regs[1] & 4) && (fdc37c669_regs[0x23] >= 0x40)) 
				{
					parallel_setup(1, make_port(0x23));
				}
			}
			if (valxor & 7)
			{
				fdc37c669_rw_locked = (val & 8) ? 0 : 1;
			}
			break;
		case 2:
			if (valxor & 8)
			{
#if 0
				serial_remove(1);
#endif
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			if (valxor & 0x80)
			{
#if 0
				serial_remove(2);
#endif
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			break;
		case 3:
			if (valxor & 2)  fdc_update_enh_mode(fdc37c669_fdc, (val & 2) ? 1 : 0);
			break;
		case 5:
			if (valxor & 0x18)  fdc_update_densel_force(fdc37c669_fdc, (val & 0x18) >> 3);
			if (valxor & 0x20)  fdc_set_swap(fdc37c669_fdc, (val & 0x20) >> 5);
			break;
		case 0xB:
			if (valxor & 3)  fdc_update_rwc(fdc37c669_fdc, 0, val & 3);
			if (valxor & 0xC)  fdc_update_rwc(fdc37c669_fdc, 1, (val & 0xC) >> 2);
			break;
		case 0x20:
			if (valxor & 0xfc)
			{
				fdc_remove(fdc37c669_fdc);
				if ((fdc37c669_regs[0] & 8) && (fdc37c669_regs[0x20] & 0xc0))  fdc_set_base(fdc37c669_fdc, make_port(0x20));
			}
			break;
		case 0x21:
		case 0x22:
#if 0
			if (valxor & 0xfc)
			{
				ide_pri_disable();
				switch (fdc37c669_curreg)
				{
					case 0x21:
						ide_set_base(0, make_port(0x21));
						break;
					case 0x22:
						ide_set_side(0, make_port(0x22));
						break;
				}
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable();
			}
#endif
			break;
		case 0x23:
			if (valxor)
			{
//FIXME:				parallel_remove(1);
				if ((fdc37c669_regs[1] & 4) && (fdc37c669_regs[0x23] >= 0x40)) 
				{
					parallel_setup(1, make_port(0x23));
				}
			}
			break;
		case 0x24:
			if (valxor & 0xfe)
			{
#if 0
				serial_remove(1);
#endif
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			break;
		case 0x25:
			if (valxor & 0xfe)
			{
#if 0
				serial_remove(2);
#endif
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			break;
		case 0x28:
			if (valxor & 0xf)
			{
#if 0
				serial_remove(2);
#endif
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			if (valxor & 0xf0)
			{
#if 0
				serial_remove(1);
#endif
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			break;
	}
}

uint8_t fdc37c669_read(uint16_t port, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;

	if (!fdc37c669_locked)
	{
		return 0xFF;
	}

	if (index)
		return fdc37c669_curreg;
	else
	{
		if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return 0xff;
		return fdc37c669_regs[fdc37c669_curreg];
	}
}

void fdc37c669_reset(void)
{
	fdc_reset(fdc37c669_fdc);

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

	memset(fdc37c669_regs, 0, 42);
	fdc37c669_regs[0] = 0x28;
	fdc37c669_regs[1] = 0x9C;
	fdc37c669_regs[2] = 0x88;
	fdc37c669_regs[3] = 0x78;
	fdc37c669_regs[4] = 0;
	fdc37c669_regs[5] = 0;
	fdc37c669_regs[6] = 0xFF;
	fdc37c669_regs[7] = 0;
	fdc37c669_regs[8] = 0;
	fdc37c669_regs[9] = 0;
	fdc37c669_regs[0xA] = 0;
	fdc37c669_regs[0xB] = 0;
	fdc37c669_regs[0xC] = 0;
	fdc37c669_regs[0xD] = 3;
	fdc37c669_regs[0xE] = 2;
	fdc37c669_regs[0x1E] = 0x80;	/* Gameport controller. */
	fdc37c669_regs[0x20] = (0x3f0 >> 2) & 0xfc;
	fdc37c669_regs[0x21] = (0x1f0 >> 2) & 0xfc;
	fdc37c669_regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
	fdc37c669_regs[0x23] = (0x378 >> 2);
	fdc37c669_regs[0x24] = (0x3f8 >> 2) & 0xfe;
	fdc37c669_regs[0x25] = (0x2f8 >> 2) & 0xfe;
	fdc37c669_regs[0x26] = (2 << 4) | 3;
	fdc37c669_regs[0x27] = (6 << 4) | 7;
	fdc37c669_regs[0x28] = (4 << 4) | 3;

        fdc37c669_locked = 0;
        fdc37c669_rw_locked = 0;
}

void fdc37c669_init()
{
        fdc37c669_fdc = (fdc_t *)device_add(&fdc_at_smc_device);

        io_sethandler(0x3f0, 0x0002, fdc37c669_read, NULL, NULL, fdc37c669_write, NULL, NULL,  NULL);

	fdc37c669_reset();
}
