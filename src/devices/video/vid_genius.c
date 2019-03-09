/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		MDSI Genius VHR emulation.
 *
 *		I'm at something of a disadvantage writing this emulation:
 *		I don't have an MDSI Genius card, nor do I have the BIOS
 *		extension (VHRBIOS.SYS) that came with it. What I do have
 *		are the GEM and Windows 1.04 drivers, plus a driver for a
 *		later MCA version of the card. The latter can be found at 
 *		<http://files.mpoli.fi/hardware/DISPLAY/GENIUS/> and is
 *		necessary if you want the Windows driver to work.
 *
 *		This emulation appears to work correctly with:
 *		  The MCA drivers GMC_ANSI.SYS and INS_ANSI.SYS
 *		  The GEM driver SDGEN9.VGA
 *		  The Windows 1.04 driver GENIUS.DRV
 *
 *		As far as I can see, the card uses a fixed resolution of
 *		728x1008 pixels. It has the following modes of operation:
 * 
 *		> MDA-compatible:      80x25 text, each character 9x15 pixels.
 *		> CGA-compatible:      640x200 mono graphics
 *		> Dual:                MDA text in the top half, CGA graphics
 *					in the bottom
 *		> Native text:         80x66 text, each character 9x15 pixels.
 *		> Native graphics:     728x1008 mono graphics.
 *
 *		Under the covers, this seems to translate to:
 *		> Text framebuffer.     At B000:0000, 16k. Displayed if
 *					enable bit is set in the MDA control
 *					register.
 *		> Graphics framebuffer. In native modes goes from A000:0000
 *					to A000:FFFF and B800:0000 to
 *					B800:FFFF. In CGA-compatible mode
 *					only the section at B800:0000 to
 *					B800:7FFF is visible. Displayed if
 *					enable bit is set in the CGA control
 *					register.
 * 
 *		Two card-specific registers control text and graphics display:
 * 
 *		03B0: Control register.
 *		  Bit 0: Map all graphics framebuffer into memory.
 *		  Bit 2: Unknown. Set by GMC /M; cleared by mode set or GMC /T.
 *		  Bit 4: Set for CGA-compatible graphics, clear for native graphics.
 *		  Bit 5: Set for black on white, clear for white on black.
 *
 *		03B1: Character height register.
 *		  Bits 0-1: Character cell height (0 => 15, 1 => 14, 2 => 13, 3 => 12)
 *		  Bit  4:   Set to double character cell height (scanlines are doubled)
 *		  Bit  7:   Unknown, seems to be set for all modes except 80x66
 *
 *		Not having the card also means I don't have its font.
 *		According to the card brochure the font is an 8x12 bitmap
 *		in a 9x15 character cell. I therefore generated it by taking
 *		the MDA font, increasing graphics to 16 pixels in height and
 *		reducing the height of characters so they fit in an 8x12 cell
 *		if necessary.
 *
 * Version:	@(#)vid_genius.c	1.0.10	2019/03/07
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *              John Elliott, <jce@seasip.info>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2016-2018 John Elliott.
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
#include "../../io.h"
#include "../../mem.h"
#include "../../rom.h"
#include "../../timer.h"
#include "../../device.h"
#include "../../plat.h"
#include "../system/pit.h"
#include "video.h"


#define FONT_ROM_PATH	L"video/mdsi/genius/8x12.bin"


#define GENIUS_XSIZE	728
#define GENIUS_YSIZE	1008


typedef struct {
    const char	*name;

    mem_map_t	mapping;

    uint8_t	mda_crtc[32];	/* The 'CRTC' as the host PC sees it */
    int		mda_crtcreg;	/* Current CRTC register */
    uint8_t	genius_control;	/* Native control register 
				 * I think bit 0 enables the full 
				 * framebuffer. 
				 */
    uint8_t	genius_charh;	/* Native character height register: 
				 * 00h => chars are 15 pixels high 
				 * 81h => chars are 14 pixels high
				 * 83h => chars are 12 pixels high 
				 * 90h => chars are 30 pixels high [15 x 2]
				 * 93h => chars are 24 pixels high [12 x 2]
				 */
    uint8_t	genius_mode;	/* Current mode (see list at top of file) */
    uint8_t	cga_ctrl;	/* Emulated CGA control register */
    uint8_t	mda_ctrl;	/* Emulated MDA control register */
    uint8_t	cga_colour;	/* Emulated CGA colour register (ignored) */

    uint8_t	mda_stat;	/* MDA status (IN 0x3BA) */
    uint8_t	cga_stat;	/* CGA status (IN 0x3DA) */

    int		font;		/* Current font, 0 or 1 */
    int		enabled;	/* Display enabled, 0 or 1 */
    int		detach;		/* Detach cursor, 0 or 1 */

    int64_t	dispontime, dispofftime;
    int64_t	vidtime;
	
    int		linepos, displine;
    int		vc;
    int		dispon, blink;
    int64_t	vsynctime;

    uint32_t	pal[4];

    uint32_t	cols[256][2][2];

    uint8_t	fontdat[256][16];

    uint8_t	*vram;
} genius_t;


static void
recalc_timings(genius_t *dev)
{
    double _dispontime, _dispofftime;
    double disptime;

    disptime = 0x31;
    _dispontime = 0x28;
    _dispofftime = disptime - _dispontime;
    _dispontime  *= MDACONST;
    _dispofftime *= MDACONST;

    dev->dispontime  = (int64_t)(_dispontime  * (1LL << TIMER_SHIFT));
    dev->dispofftime = (int64_t)(_dispofftime * (1LL << TIMER_SHIFT));
}


static void
genius_out(uint16_t port, uint8_t val, void *priv)
{
    genius_t *dev = (genius_t *)priv;

    switch (port) {
	case 0x3b0: 	/* Command / control register */
		dev->genius_control = val;
		if (val & 1)
			mem_map_set_addr(&dev->mapping, 0xa0000, 0x28000);
		else
			mem_map_set_addr(&dev->mapping, 0xb0000, 0x10000);
		break;

	case 0x3b1:
		dev->genius_charh = val;
		break;

	/* Emulated CRTC, register select */
	case 0x3b2: case 0x3b4: case 0x3b6:
	case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
		dev->mda_crtcreg = val & 31;
		break;

	/* Emulated CRTC, value */
	case 0x3b3: case 0x3b5: case 0x3b7:
	case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		dev->mda_crtc[dev->mda_crtcreg] = val;
		recalc_timings(dev);
		return;

	/* Emulated MDA control register */
	case 0x3b8: 
	     	dev->mda_ctrl = val;
	      	return;

	/* Emulated CGA control register */
	case 0x3d8:
	     	dev->cga_ctrl = val;
	      	return;

	/* Emulated CGA colour register */
	case 0x3d9:
	       	dev->cga_colour = val;
	      	return;
    }
}


static uint8_t
genius_in(uint16_t port, void *priv)
{
    genius_t *dev = (genius_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
	case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
		ret = dev->mda_crtcreg;
		break;

	case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
	case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		ret = dev->mda_crtc[dev->mda_crtcreg];
		break;

	case 0x3b8: 
		ret = dev->mda_ctrl;
		break;

	case 0x3d9:
		ret = dev->cga_colour;
		break;

	case 0x3ba: 
		ret = dev->mda_stat;
		break;

	case 0x3d8:
		ret = dev->cga_ctrl;
		break;

	case 0x3da:
		ret = dev->cga_stat;
		break;

	default:
		break;
    }

    return ret;
}


static void
genius_write(uint32_t addr, uint8_t val, void *priv)
{
    genius_t *dev = (genius_t *)priv;

    if (dev->genius_control & 1) {
	addr = addr % 0x28000;
    } else {
	/* If hi-res memory is disabled, only visible in the B000 segment */
	addr = (addr & 0xffff) + 0x10000;
    }

    dev->vram[addr] = val;
}


static uint8_t
genius_read(uint32_t addr, void *priv)
{
    genius_t *dev = (genius_t *)priv;

    if (dev->genius_control & 1) {
		addr = addr % 0x28000;
    } else {
	/* If hi-res memory is disabled, only visible in the B000 segment */
	addr = (addr & 0xffff) + 0x10000;
    }

    return dev->vram[addr];
}


/* Draw a single line of the screen in either text mode */
static void
text_line(genius_t *dev, uint8_t background)
{
    int x;
    int w  = 80;	/* 80 characters across */
    int cw = 9;		/* Each character is 9 pixels wide */
    uint8_t chr, attr;
    uint8_t bitmap[2];
    int blink, c, row;
    int drawcursor, cursorline;
    uint16_t addr;
    uint8_t sc;
    int charh;
    uint16_t ma = (dev->mda_crtc[13] | (dev->mda_crtc[12] << 8)) & 0x3fff;
    uint16_t ca = (dev->mda_crtc[15] | (dev->mda_crtc[14] << 8)) & 0x3fff;
    uint8_t *framebuf = dev->vram + 0x10000;
    uint32_t col;

    /* Character height is 12-15 */
    charh = 15 - (dev->genius_charh & 3);
    if (dev->genius_charh & 0x10) {
	row = ((dev->displine >> 1) / charh);	
	sc  = ((dev->displine >> 1) % charh);	
    } else {
	row = (dev->displine / charh);	
	sc  = (dev->displine % charh);	
    }
    addr = ((ma & ~1) + row * w) * 2;

    ma += (row * w);
	
    if ((dev->mda_crtc[10] & 0x60) == 0x20) {
	cursorline = 0;
    } else {
	cursorline = ((dev->mda_crtc[10] & 0x1F) <= sc) &&
		     ((dev->mda_crtc[11] & 0x1F) >= sc);
    }

    for (x = 0; x < w; x++) {
	chr  = framebuf[(addr + 2 * x) & 0x3FFF];
	attr = framebuf[(addr + 2 * x + 1) & 0x3FFF];
	drawcursor = ((ma == ca) && cursorline && dev->enabled &&
		(dev->mda_ctrl & 8));

	switch (dev->mda_crtc[10] & 0x60) {
		case 0x00: drawcursor = drawcursor && (dev->blink & 16); break;
		case 0x60: drawcursor = drawcursor && (dev->blink & 32); break;
	}
	blink = ((dev->blink & 16) && 
		(dev->mda_ctrl & 0x20) && 
		(attr & 0x80) && !drawcursor);

	if (dev->mda_ctrl & 0x20) attr &= 0x7F;

	/* MDA underline */
	if (sc == charh && ((attr & 7) == 1)) {
		col = dev->cols[attr][blink][1];

		if (dev->genius_control & 0x20)
			col ^= 0xffffff;

		for (c = 0; c < cw; c++) {
			if (col != background) 
				screen->line[dev->displine][(x * cw) + c].val = col;
		}
	} else	{
		/* Draw 8 pixels of character */
		bitmap[0] = dev->fontdat[chr][sc];
		for (c = 0; c < 8; c++) {
			col = dev->cols[attr][blink][(bitmap[0] & (1 << (c ^ 7))) ? 1 : 0];
			if (!(dev->enabled) || !(dev->mda_ctrl & 8))
				col = dev->cols[0][0][0];

			if (dev->genius_control & 0x20)
				col ^= 0xffffff;

			if (col != background)
				screen->line[dev->displine][(x * cw) + c].val = col;
		}

		/* The ninth pixel column... */
		if ((chr & ~0x1f) == 0xc0) {
			/* Echo column 8 for the graphics chars */
			col = screen->line[dev->displine][(x * cw) + 7].val;
			if (col != background)
				screen->line[dev->displine][(x * cw) + 8].val = col;
		} else {
			/* Otherwise fill with background */	
			col = dev->cols[attr][blink][0];
			if (dev->genius_control & 0x20)
				col ^= 0xffffff;

			if (col != background)
				if (col != background)
                                        screen->line[dev->displine][(x * cw) + 8].val = col;
		}
		if (drawcursor) {
			for (c = 0; c < cw; c++)
                               	screen->line[dev->displine][(x * cw) + c].val ^= dev->cols[attr][0][1];
		}
		++ma;
	}
    }
}


/* Draw a line in the CGA 640x200 mode */
static void
cga_line(genius_t *dev)
{
    uint32_t dat;
    uint32_t ink;
    uint32_t addr;
    int x, c;

    ink = (dev->genius_control & 0x20) ? dev->pal[0] : dev->pal[3];

    /* We draw the CGA at row 600 */
    if (dev->displine < 600) return;

    addr = 0x18000 + 80 * ((dev->displine - 600) >> 2);
    if ((dev->displine - 600) & 2)
	addr += 0x2000;

    for (x = 0; x < 80; x++) {
	dat =  dev->vram[addr];
	addr++;

	for (c = 0; c < 8; c++) {
		if (dat & 0x80)
			screen->line[dev->displine][x*8 + c].val = ink;
		dat = dat << 1;
	}
    }
}


/* Draw a line in the native high-resolution mode */
static void
hires_line(genius_t *dev)
{
    uint32_t dat;
    uint32_t ink;
    uint32_t addr;
    int x, c;

    ink = (dev->genius_control & 0x20) ? dev->pal[0] : dev->pal[3];

    /* The first 512 lines live at A0000 */
    if (dev->displine < 512) {
	addr = 128 * dev->displine;
    } else {
	/* The second 496 live at B8000 */
	addr = 0x18000 + 128 * (dev->displine - 512);
    }

    for (x = 0; x < 91; x++) {
	dat =  dev->vram[addr];
	addr++;

	for (c = 0; c < 8; c++) {
		if (dat & 0x80)
			screen->line[dev->displine][x*8 + c].val = ink;
		dat = dat << 1;
	}
    }
}


static void
genius_poll(void *priv)
{
    genius_t *dev = (genius_t *)priv;
    uint8_t background;
    int x;

    if (!dev->linepos) {
	dev->vidtime += dev->dispofftime;
	dev->cga_stat |= 1;
	dev->mda_stat |= 1;
	dev->linepos = 1;
	if (dev->dispon) {
		if (dev->genius_control & 0x20)
			background = dev->pal[3];
		else
			background = dev->pal[0];

		if (dev->displine == 0)
			video_blit_wait_buffer();

		/* Start off with a blank line */
		for (x = 0; x < GENIUS_XSIZE; x++)
			screen->line[dev->displine][x].val = background;

		/* If graphics display enabled, draw graphics on top
		 * of the blanked line */
		if (dev->cga_ctrl & 8) {
			if (dev->genius_control & 8)
				cga_line(dev);
			else
				hires_line(dev);
		}

		/* If MDA display is enabled, draw MDA text on top
		 * of the lot */
		if (dev->mda_ctrl & 8)
			text_line(dev, background);
	}
	dev->displine++;

	/* Hardcode a fixed refresh rate and VSYNC timing */
	if (dev->displine == 1008) {
		/* Start of VSYNC */
		dev->cga_stat |= 8;
		dev->dispon = 0;
	}

	if (dev->displine == 1040) {
		/* End of VSYNC */
		dev->displine = 0;
		dev->cga_stat &= ~8;
		dev->dispon = 1;
	}
    } else {
	if (dev->dispon) {
		dev->cga_stat &= ~1;
		dev->mda_stat &= ~1;
	}
	dev->vidtime += dev->dispontime;
	dev->linepos = 0;

	if (dev->displine == 1008) {
/* Hardcode GENIUS_XSIZE * GENIUS_YSIZE window size */
		if (GENIUS_XSIZE != xsize || GENIUS_YSIZE != ysize) {
			xsize = GENIUS_XSIZE;
			ysize = GENIUS_YSIZE;
			if (xsize < 64) xsize = 656;
			if (ysize < 32) ysize = 200;
			set_screen_size(xsize, ysize);

			if (video_force_resize_get())
				video_force_resize_set(0);
		}

		video_blit_start(0, 0, 0, 0, ysize, xsize, ysize);
		frames++;

		/* Fixed 728x1008 resolution */
		video_res_x = GENIUS_XSIZE;
		video_res_y = GENIUS_YSIZE;
		video_bpp = 1;
		dev->blink++;
	}
    }
}


static int
load_font(genius_t *dev, const wchar_t *s)
{
    FILE *fp;
    int c;

    fp = plat_fopen(rom_path(s), L"rb");
    if (fp == NULL) {
	ERRLOG("%s: cannot load font '%ls'\n", dev->name, s);
	return(0);
    }

    for (c = 0; c < 256; c++)
	(void)fread(&dev->fontdat[c][0], 1, 16, fp);

    (void)fclose(fp);

    return(1);
}


static void *
genius_init(const device_t *info)
{
    genius_t *dev;
    int c;

    dev = (genius_t *)mem_alloc(sizeof(genius_t));
    memset(dev, 0x00, sizeof(genius_t));
    dev->name = info->name;

    if (! load_font(dev, FONT_ROM_PATH)) {
	free(dev);
	return(NULL);
    }

    /* 160k video RAM */
    dev->vram = (uint8_t *)mem_alloc(0x28000);

    timer_add(genius_poll, &dev->vidtime, TIMER_ALWAYS_ENABLED, dev);

    /* Occupy memory between 0xB0000 and 0xBFFFF (moves to 0xA0000 in
     * high-resolution modes)  */
    mem_map_add(&dev->mapping, 0xb0000, 0x10000,
		genius_read,NULL,NULL, genius_write,NULL,NULL,
		NULL, MEM_MAPPING_EXTERNAL, dev);

    /* Respond to both MDA and CGA I/O ports */
    io_sethandler(0x03b0, 0x000C,
		  genius_in,NULL,NULL, genius_out,NULL,NULL, dev);
    io_sethandler(0x03d0, 0x0010,
		  genius_in,NULL,NULL, genius_out,NULL,NULL, dev);

    dev->pal[0] = makecol(0x00, 0x00, 0x00);
    dev->pal[1] = makecol(0x55, 0x55, 0x55);
    dev->pal[2] = makecol(0xaa, 0xaa, 0xaa);
    dev->pal[3] = makecol(0xff, 0xff, 0xff);

    /* MDA attributes */
    /* I don't know if the Genius's MDA emulation actually does 
     * emulate bright / non-bright. For the time being pretend it does. */
    for (c = 0; c < 256; c++) {
	dev->cols[c][0][0] = dev->cols[c][1][0] =
			dev->cols[c][1][1] = dev->pal[0];
	if (c & 0x08)
		dev->cols[c][0][1] = dev->pal[3];
	  else
		dev->cols[c][0][1] = dev->pal[2];
    }
    dev->cols[0x70][0][1] = dev->pal[0];
    dev->cols[0x70][0][0] = dev->cols[0x70][1][0] =
		dev->cols[0x70][1][1] = dev->pal[3];
    dev->cols[0xF0][0][1] = dev->pal[0];
    dev->cols[0xF0][0][0] = dev->cols[0xF0][1][0] =
		dev->cols[0xF0][1][1] = dev->pal[3];
    dev->cols[0x78][0][1] = dev->pal[2];
    dev->cols[0x78][0][0] = dev->cols[0x78][1][0] =
		dev->cols[0x78][1][1] = dev->pal[3];
    dev->cols[0xF8][0][1] = dev->pal[2];
    dev->cols[0xF8][0][0] = dev->cols[0xF8][1][0] =
		dev->cols[0xF8][1][1] = dev->pal[3];
    dev->cols[0x00][0][1] = dev->cols[0x00][1][1] = dev->pal[0];
    dev->cols[0x08][0][1] = dev->cols[0x08][1][1] = dev->pal[0];
    dev->cols[0x80][0][1] = dev->cols[0x80][1][1] = dev->pal[0];
    dev->cols[0x88][0][1] = dev->cols[0x88][1][1] = dev->pal[0];

    /* Start off in 80x25 text mode */
    dev->cga_stat   = 0xF4;
    dev->genius_mode = 2;
    dev->enabled    = 1;
    dev->genius_charh = 0x90; /* Native character height register */

    video_inform(VID_TYPE_MDA, info->vid_timing);

    return dev;
}


static void
genius_close(void *priv)
{
    genius_t *dev = (genius_t *)priv;

    free(dev->vram);
    free(dev);
}


static int
genius_available(void)
{
    return rom_present(FONT_ROM_PATH);
}


static void
speed_changed(void *priv)
{
    genius_t *dev = (genius_t *)priv;

    recalc_timings(dev);
}


static const video_timings_t genius_timings = { VID_ISA,8,16,32,8,16,32 };

const device_t genius_device = {
    "Genius VHR",
    DEVICE_ISA, 0,
    genius_init, genius_close, NULL,
    genius_available,
    speed_changed,
    NULL,
    &genius_timings,
    NULL
};
