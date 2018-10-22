/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the WD76C10 system controller.
 *
 * Version:	@(#)m_at_wd76c10.c	1.0.9	2018/09/19
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
#include "../emu.h"
#include "../io.h"
#include "../mem.h"
#include "../device.h"
#include "../devices/ports/serial.h"
#include "../devices/input/keyboard.h"
#include "../devices/floppy/fdd.h"
#include "../devices/floppy/fdc.h"
#include "machine.h"


/* Defined in the Video module. */
extern const device_t paradise_wd90c11_megapc_device;


static uint16_t wd76c10_0092;
static uint16_t wd76c10_2072;
static uint16_t wd76c10_2872;
static uint16_t wd76c10_5872;


static fdc_t *wd76c10_fdc;


static uint16_t
wd76c10_read(uint16_t port, void *priv)
{
    switch (port) {
	case 0x0092:
		return wd76c10_0092;

	case 0x2072:
		return wd76c10_2072;

	case 0x2872:
		return wd76c10_2872;

	case 0x5872:
		return wd76c10_5872;
    }

    return(0);
}


static void
wd76c10_write(uint16_t port, uint16_t val, void *priv)
{
    switch (port) {
	case 0x0092:
		wd76c10_0092 = val;

		mem_a20_alt = val & 2;
		mem_a20_recalc();
		break;

	case 0x2072:
		wd76c10_2072 = val;

#if 0
		serial_remove(1);
#endif
		if (! (val & 0x10)) {
			switch ((val >> 5) & 7) {
				case 1: serial_setup(1, 0x3f8, 4); break;
				case 2: serial_setup(1, 0x2f8, 4); break;
				case 3: serial_setup(1, 0x3e8, 4); break;
				case 4: serial_setup(1, 0x2e8, 4); break;
				default:
#if 0
					serial_remove(1);
#endif
					break;
			}
		}
#if 0
		serial_remove(2);
#endif
		if (! (val & 0x01)) {
			switch ((val >> 1) & 7) {
				case 1: serial_setup(2, 0x3f8, 3); break;
				case 2: serial_setup(2, 0x2f8, 3); break;
				case 3: serial_setup(2, 0x3e8, 3); break;
				case 4: serial_setup(2, 0x2e8, 3); break;
				default:
#if 0
					serial_remove(1);
#endif
					break;
			}
		}
		break;

	case 0x2872:
		wd76c10_2872 = val;

		fdc_remove(wd76c10_fdc);
		if (! (val & 1))
			fdc_set_base(wd76c10_fdc, 0x03f0);
		break;

	case 0x5872:
		wd76c10_5872 = val;
		break;
    }
}


static uint8_t
wd76c10_readb(uint16_t port, void *priv)
{
    if (port & 1)
	return(wd76c10_read(port & ~1, priv) >> 8);

    return(wd76c10_read(port, priv) & 0xff);
}


static void
wd76c10_writeb(uint16_t port, uint8_t val, void *priv)
{
    uint16_t temp = wd76c10_read(port, priv);

    if (port & 1)
	wd76c10_write(port & ~1, (temp & 0x00ff) | (val << 8), priv);
      else
	wd76c10_write(port     , (temp & 0xff00) | val, priv);
}


static void
wd76c10_init(void)
{
    io_sethandler(0x0092, 2,
		  wd76c10_readb, wd76c10_read, NULL,
		  wd76c10_writeb, wd76c10_write, NULL, NULL);
    io_sethandler(0x2072, 2,
		  wd76c10_readb, wd76c10_read, NULL,
		  wd76c10_writeb, wd76c10_write, NULL, NULL);
    io_sethandler(0x2872, 2,
		  wd76c10_readb, wd76c10_read, NULL,
		  wd76c10_writeb, wd76c10_write, NULL, NULL);
    io_sethandler(0x5872, 2,
		  wd76c10_readb, wd76c10_read, NULL,
		  wd76c10_writeb, wd76c10_write, NULL, NULL);
}


void
machine_at_wd76c10_init(const machine_t *model, void *arg)
{
    machine_at_common_ide_init(model, arg);

    device_add(&keyboard_ps2_quadtel_device);
    wd76c10_fdc = (fdc_t *)device_add(&fdc_at_device);

    wd76c10_init();

    device_add(&paradise_wd90c11_megapc_device);
}
