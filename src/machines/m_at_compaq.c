/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Emulation of various Compaq PC's.
 *
 * Version:	@(#)m_at_compaq.c	1.0.9	2019/02/16
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		TheCollector1995, <mariogplayer@gmail.com>
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
#include <wchar.h>
#include "../emu.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../devices/floppy/fdd.h"
#include "../devices/floppy/fdc.h"
#include "../devices/disk/hdc.h"
#include "../devices/disk/hdc_ide.h"
#include "machine.h"


/* Compaq Deskpro 386 remaps RAM from 0xA0000-0xFFFFF to 0xFA0000-0xFFFFFF */
static mem_map_t	ram_mapping;


static uint8_t
read_ram(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(ram[addr]);
}


static uint16_t
read_ramw(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(*(uint16_t *)&ram[addr]);
}


static uint32_t
read_raml(uint32_t addr, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return(*(uint32_t *)&ram[addr]);
}


static void
write_ram(uint32_t addr, uint8_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}


static void
write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}


static void
write_raml(uint32_t addr, uint32_t val, void *priv)
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_raml_page(addr, val, &pages[addr >> 12]);
}


static void
compaq_common_init(const machine_t *model, void *arg, int type)
{
    m_at_init(model, arg);

    mem_remap_top(384);

    device_add(&fdc_at_device);

    mem_map_add(&ram_mapping, 0xfa0000, 0x60000,
                read_ram, read_ramw, read_raml,
                write_ram, write_ramw, write_raml,
                0xa0000+ram, MEM_MAPPING_INTERNAL, NULL);

    switch(type) {
	case 0:			/* Portable 286 */
		break;

	case 2:			/* Portable II */
		break;

#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
	case 3:			/* Portable III */
		m_olim24_video_init();
		break;

	case 3+386:		/* Portable III/386 */
		m_olim24_video_init();
		if (hdc_type == 1)
			device_add(&ide_isa_device);
		break;
#endif

#if defined(DEV_BRANCH) && defined(USE_DESKPRO386)
	case 4+386:		/* Deskpro 386 */
		if (hdc_type == 1)
			device_add(&ide_isa_device);
		break;
#endif
    }
}


void
m_at_compaq_p1_init(const machine_t *model, void *arg)
{
    compaq_common_init(model, arg, 0);
}


void
m_at_compaq_p2_init(const machine_t *model, void *arg)
{
    compaq_common_init(model, arg, 2);
}


void
m_at_compaq_p3_init(const machine_t *model, void *arg)
{
    compaq_common_init(model, arg, 3);
}


void
m_at_compaq_p3_386_init(const machine_t *model, void *arg)
{
    compaq_common_init(model, arg, 3+386);
}
