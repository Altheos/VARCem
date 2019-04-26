/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Driver for the IBM PC-XT Fixed Disk controller.
 *
 *		The original controller shipped by IBM was made by Xebec, and
 *		several variations had been made:
 *
 *		#1	Original, single drive (ST412), 10MB, 2 heads.
 *		#2	Update, single drive (ST412) but with option for a
 *			switch block that can be used to 'set' the actual
 *			drive type. Four switches are defined, where switches
 *			1 and 2 define drive0, and switches 3 and 4 drive1.
 *
 *			  0  ON  ON	306  2  0
 *			  1  ON  OFF	375  8  0
 *			  2  OFF ON	306  6  256
 *			  3  OFF OFF	306  4  0
 *
 *			The latter option is the default, in use on boards
 *			without the switch block option.
 *
 *		#3	Another updated board, mostly to accomodate the new
 *			20MB disk now being shipped. The controller can have
 *			up to 2 drives, the type of which is set using the
 *			switch block:
 *
 *			     SW1 SW2	CYLS HD SPT WPC
 *			  0  ON  ON	306  4  17  0
 *			  1  ON  OFF	612  4  17  0	(type 16)
 *			  2  OFF ON    	615  4  17  300	(Seagate ST-225, 2)
 *			  3  OFF OFF	306  8  17  128 (IBM WD25, 13)
 *
 *		Examples of #3 are IBM/Xebec, WD10004A-WX1 and ST11R.
 *
 *		Since all controllers (including the ones made by DTC) use
 *		(mostly) the same API, we keep them all in this module.
 *
 * Version:	@(#)hdc_st506_xt.c	1.0.19	2019/04/25
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
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
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define dbglog hdc_log
#include "../../emu.h"
#include "../../io.h"
#include "../../mem.h"
#include "../../rom.h"
#include "../../timer.h"
#include "../../device.h"
#include "../../ui/ui.h"
#include "../../plat.h"
#include "../system/dma.h"
#include "../system/pic.h"
#include "hdc.h"
#include "hdd.h"


#define XEBEC_BIOS_FILE		L"disk/st506/ibm_xebec_62x0822_1985.bin"
#define DTC_BIOS_FILE		L"disk/st506/dtc_cxd21a.bin"
#define ST11_BIOS_FILE_OLD	L"disk/st506/st11_bios_vers_1.7.bin"
#define ST11_BIOS_FILE_NEW	L"disk/st506/st11_bios_vers_2.0.bin"
#define WD1002A_WX1_BIOS_FILE	L"disk/st506/wd1002a_wx1-62-000094-032.bin"
#define WD1002A_27X_BIOS_FILE	L"disk/st506/wd1002a_27x-62-000215-060.bin"


#define ST506_TIME		(250LL * TIMER_USEC)
#define ST506_TIME_MS		(1000LL * TIMER_USEC)

/* MFM and RLL use different sectors/track. */
#define SECTOR_SIZE		512
#define MFM_SECTORS		17
#define RLL_SECTORS		26


/* Status register. */
#define STAT_REQ		0x01		/* controller ready */
#define STAT_IO			0x02		/* input, data to host */
#define STAT_CD			0x04		/* command mode (else data) */
#define STAT_BSY		0x08		/* controller is busy */
#define STAT_DRQ		0x10		/* controller needs DMA */
#define STAT_IRQ		0x20		/* interrupt, we have info */

/* DMA/IRQ enable register. */
#define DMA_ENA			0x01		/* DMA operation enabled */
#define IRQ_ENA			0x02		/* IRQ operation enabled */

/* Error codes in sense report. */
#define ERR_BV			0x80
#define ERR_TYPE_MASK		0x30
#define ERR_TYPE_SHIFT		4
# define ERR_TYPE_DRIVE		0x00
# define ERR_TYPE_CONTROLLER	0x01
# define ERR_TYPE_COMMAND	0x02
# define ERR_TYPE_MISC		0x03

/* No, um, errors.. */
#define ERR_NONE		0x00

/* Group 0: drive errors. */
#define ERR_NO_SEEK		0x02		/* no seek_complete */
#define ERR_WR_FAULT		0x03		/* write fault */
#define ERR_NOT_READY		0x04		/* drive not ready */
#define ERR_NO_TRACK0		0x06		/* track 0 not found */
#define ERR_STILL_SEEKING	0x08		/* drive is still seeking */
#define ERR_NOT_AVAILABLE	0x09		/* drive not available */

/* Group 1: controller errors. */
#define ERR_ID_FAULT		0x10		/* could not read ID field */
#define ERR_UNC_ERR		0x11		/* uncorrectable data */
#define ERR_SECTOR_ADDR		0x12		/* sector address */
#define ERR_DATA_ADDR		0x13		/* data mark not found */
#define ERR_TARGET_SECTOR	0x14		/* target sector not found */
#define ERR_SEEK_ERROR		0x15		/* seek error- cyl not found */
#define ERR_CORR_ERR		0x18		/* correctable data */
#define ERR_BAD_TRACK		0x19		/* track is flagged as bad */
#define ERR_ALT_TRACK_FLAGGED	0x1c		/* alt trk not flagged as alt */
#define ERR_ALT_TRACK_ACCESS	0x1e 		/* illegal access to alt trk */
#define ERR_NO_RECOVERY		0x1f		/* recovery mode not avail */

/* Group 2: command errors. */
#define ERR_BAD_COMMAND		0x20		/* invalid command */
#define ERR_ILLEGAL_ADDR	0x21		/* address beyond disk size */
#define ERR_BAD_PARAMETER	0x22		/* invalid command parameter */

/* Group 3: misc errors. */
#define ERR_BAD_RAM		0x30		/* controller has bad RAM */
#define ERR_BAD_ROM		0x31		/* ROM failed checksum test */
#define ERR_CRC_FAIL		0x32		/* CRC circuit failed test */

/* Controller commands. */
#define CMD_TEST_DRIVE_READY	0x00
#define CMD_RECALIBRATE		0x01
/* reserved			0x02 */
#define CMD_STATUS		0x03
#define CMD_FORMAT_DRIVE	0x04
#define CMD_VERIFY		0x05
#define CMD_FORMAT_TRACK	0x06
#define CMD_FORMAT_BAD_TRACK	0x07
#define CMD_READ		0x08
#define CMD_REASSIGN		0x09
#define CMD_WRITE		0x0a
#define CMD_SEEK		0x0b
#define CMD_SPECIFY		0x0c
#define CMD_READ_ECC_BURST_LEN	0x0d
#define CMD_READ_BUFFER		0x0e
#define CMD_WRITE_BUFFER	0x0f
#define CMD_ALT_TRACK		0x11
#define CMD_INQUIRY_ST11	0x12		/* ST-11 BIOS */
#define CMD_RAM_DIAGNOSTIC	0xe0
/* reserved			0xe1 */
/* reserved			0xe2 */
#define CMD_DRIVE_DIAGNOSTIC	0xe3
#define CMD_CTRLR_DIAGNOSTIC	0xe4
#define CMD_READ_LONG		0xe5
#define CMD_WRITE_LONG		0xe6

#define CMD_FORMAT_ST11		0xf6		/* ST-11 BIOS */
#define CMD_GET_GEOMETRY_ST11	0xf8		/* ST-11 BIOS */
#define CMD_SET_GEOMETRY_ST11	0xfa		/* ST-11 BIOS */
#define CMD_WRITE_GEOMETRY_ST11	0xfc		/* ST-11 BIOS 2.0 */

#define CMD_GET_DRIVE_PARAMS_DTC 0xfb		/* DTC */
#define CMD_SET_STEP_RATE_DTC	0xfc		/* DTC */
#define CMD_SET_GEOMETRY_DTC	0xfe		/* DTC */
#define CMD_GET_GEOMETRY_DTC	0xff		/* DTC */


enum {
    STATE_IDLE,
    STATE_RECEIVE_COMMAND,
    STATE_START_COMMAND,
    STATE_RECEIVE_DATA,
    STATE_RECEIVED_DATA,
    STATE_SEND_DATA,
    STATE_SENT_DATA,
    STATE_COMPLETION_BYTE,
    STATE_DONE
};


typedef struct {
    int8_t	present;
    uint8_t	hdd_num;

    uint8_t	interleave;		/* default interleave */
    char	pad;

    uint16_t	cylinder;		/* current cylinder */

    uint8_t	spt,			/* physical parameters */
		hpc;
    uint16_t	tracks;

    uint8_t	cfg_spt,		/* configured parameters */
		cfg_hpc;
    uint16_t	cfg_cyl;
} drive_t;


typedef struct {
    uint8_t	type;			/* controller type */

    uint8_t	spt;			/* sectors-per-track for controller */

    uint16_t	base;			/* controller configuration */
    int8_t	irq,
		dma;
    uint8_t	switches;
    uint8_t	misc;
    uint32_t	bios_addr,
		bios_size,
		bios_ram;
    rom_t	bios_rom;

    int		state;			/* operational data */
    uint8_t	irq_dma;
    uint8_t	error;
    uint8_t	status;
    int8_t	cyl_off;		/* for ST-11, cylinder0 offset */
    int64_t	callback;

    uint8_t	command[6];		/* current command request */
    int		drive_sel;
    int		sector,
		head,
		cylinder,
		count;
    uint8_t	compl;			/* current request completion code */

    int		buff_pos,		/* pointers to the RAM buffer */
		buff_cnt;

    drive_t	drives[ST506_NUM];	/* the attached drives */
    uint8_t	scratch[64];		/* ST-11 scratchpad RAM */
    uint8_t	buff[SECTOR_SIZE + 4];	/* sector buffer RAM (+ ECC bytes) */
} hdc_t;


/* Supported drives table for the Xebec controller. */
static const struct {
    uint16_t	tracks;
    uint8_t	hpc;
    uint8_t	spt;
} hd_types[4] = {
    { 306, 4, MFM_SECTORS },	/* type 0	*/
    { 612, 4, MFM_SECTORS },	/* type 16	*/
    { 615, 4, MFM_SECTORS },	/* type 2	*/
    { 306, 8, MFM_SECTORS } 	/* type 13	*/
};


static void
st506_complete(hdc_t *dev)
{
    dev->status = STAT_REQ | STAT_CD | STAT_IO | STAT_BSY;
    dev->state = STATE_COMPLETION_BYTE;

    if (dev->irq_dma & IRQ_ENA) {
	dev->status |= STAT_IRQ;
	picint(1 << dev->irq);
    }
}


static void
st506_error(hdc_t *dev, uint8_t err)
{
    dev->compl |= 0x02;
    dev->error = err;
}


static int
get_sector(hdc_t *dev, drive_t *drive, off64_t *addr)
{
    if (! drive->present) {
	/* No need to log this. */
	dev->error = ERR_NOT_READY;		// AVAILABLE
	return(0);
    }

    if (drive->cylinder != dev->cylinder) {
	DEBUG("ST506: get_sector: wrong cylinder\n");
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }

    if (dev->head >= drive->cfg_hpc) {
	DEBUG("ST506: get_sector: past end of configured heads\n");
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }
    if (dev->sector >= drive->cfg_spt) {
	DEBUG("ST506: get_sector: past end of configured sectors\n");
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }

    *addr = ((((off64_t)dev->cylinder * drive->cfg_hpc) + dev->head) * drive->cfg_spt) + dev->sector;
	
    return(1);
}


static void
next_sector(hdc_t *dev, drive_t *drive)
{
    if (++dev->sector >= drive->cfg_spt) {
	dev->sector = 0;
	if (++dev->head >= drive->cfg_hpc) {
		dev->head = 0;
		if (++drive->cylinder >= drive->cfg_cyl) {
			/*
			 * This really is an error, we cannot move
			 * past the end of the drive, which should
			 * result in an ERR_ILLEGAL_ADDR.  --FvK
			 */
			drive->cylinder = drive->cfg_cyl - 1;
		} else
			dev->cylinder++;
	}
    }
}


/* Extract the CHS info from a command block. */
static int
get_chs(hdc_t *dev, drive_t *drive)
{
    dev->cylinder = dev->command[3] | ((dev->command[2] & 0xc0) << 2);
    dev->head = dev->command[1] & 0x1f;
    dev->sector = dev->command[2] & 0x1f;	/* 0x3f on some */
    dev->count = dev->command[4];
    dev->cylinder += dev->cyl_off;		/* for ST-11 */

    if (dev->cylinder >= drive->cfg_cyl) {
	/*
	 * This really is an error, we cannot move
	 * past the end of the drive, which should
	 * result in an ERR_ILLEGAL_ADDR.  --FvK
	 */
	drive->cylinder = drive->cfg_cyl - 1;
	return(0);
    }

    drive->cylinder = dev->cylinder;

    return(1);
}


static void
st506_callback(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    drive_t *drive;
    off64_t addr;
    uint32_t capac;
    int val;

    /* Cancel the timer for now. */
    dev->callback = 0LL;

    /* Get the drive info. Note that the API supports up to 8 drives! */
    dev->drive_sel = (dev->command[1] >> 5) & 0x07;
    drive = &dev->drives[dev->drive_sel];

    /* Preset the completion byte to "No error" and the selected drive. */
    dev->compl = (dev->drive_sel << 5) | ERR_NONE;

    switch (dev->command[0]) {
	case CMD_TEST_DRIVE_READY:
		DEBUG("ST506: TEST_READY(%i) = %i\n",
			dev->drive_sel, drive->present);
		if (! drive->present)
			st506_error(dev, ERR_NOT_READY);	//AVAILABLE
		st506_complete(dev);
		break;

	case CMD_RECALIBRATE:
		switch (dev->state) {
			case STATE_START_COMMAND:
				DEBUG("ST506: RECALIBRATE(%i) [%i]\n",
					dev->drive_sel, drive->present);
				if (! drive->present) {
					st506_error(dev, ERR_NOT_READY);
					st506_complete(dev);	//AVAILABLE
					break;
				}

				/* Wait 20msec. */
				dev->callback = ST506_TIME_MS * 20;
				dev->cylinder = dev->cyl_off;
				drive->cylinder = dev->cylinder;
				dev->state = STATE_DONE;

				break;

			case STATE_DONE:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_STATUS:
		switch (dev->state) {
			case STATE_START_COMMAND:
				DEBUG("ST506: STATUS\n");
				dev->buff_pos = 0;
				dev->buff_cnt = 4;
				dev->buff[0] = ERR_BV | dev->error;
				dev->error = 0;

				/* Give address of last operation. */
				dev->buff[1] = (dev->drive_sel ? 0x20 : 0) |
					       dev->head;
				dev->buff[2] = ((dev->cylinder & 0x0300) >> 2) |
					       dev->sector;
				dev->buff[3] = (dev->cylinder & 0xff);

				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_FORMAT_DRIVE:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: FORMAT_DRIVE(%i) interleave=%i\n",
					dev->drive_sel, dev->command[4]);
				hdd_active(drive->hdd_num, 1);
				dev->callback = ST506_TIME;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:	/* wrong, but works */
				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* Capacity of this drive. */
				capac = (drive->tracks - 1) * drive->hpc * drive->spt;
				hdd_image_zero(drive->hdd_num, addr, capac);

				/* Wait 20msec per cylinder. */
				dev->callback = ST506_TIME_MS * 20 * drive->cfg_cyl;

				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				hdd_active(drive->hdd_num, 0);
				st506_complete(dev);
				break;
		}
		break;

	case CMD_VERIFY:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: VERIFY(%i, %i/%i/%i, %i)\n",
					dev->drive_sel, dev->cylinder,
					dev->head, dev->sector, dev->count);
				hdd_active(drive->hdd_num, 1);
				dev->callback = ST506_TIME;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				if (dev->count-- == 0) {
					hdd_active(drive->hdd_num, 0);
					st506_complete(dev);
				}

				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				next_sector(dev, drive);

				dev->callback = ST506_TIME;
				break;
		}
		break;

	case CMD_FORMAT_TRACK:
	case CMD_FORMAT_BAD_TRACK:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: FORMAT_%sTRACK(%i, %i/%i)\n",
					(dev->command[0] == CMD_FORMAT_BAD_TRACK) ? "BAD_" : "",
					dev->drive_sel, dev->cylinder, dev->head);
				hdd_active(drive->hdd_num, 1);
				dev->callback = ST506_TIME;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:	/* wrong, but works */
				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				hdd_image_zero(drive->hdd_num,
					       addr, drive->cfg_spt);

				/* Wait 20msec per cylinder. */
				dev->callback = ST506_TIME_MS * 20;

				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				hdd_active(drive->hdd_num, 0);
				st506_complete(dev);
				break;
		}
		break;			       

	case CMD_READ:
#if 0
	case CMD_READ_LONG:
#endif
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: READ%s(%i, %i/%i/%i, %i)\n",
					(dev->command[0] == CMD_READ_LONG) ? "_LONG" : "",
					dev->drive_sel, dev->cylinder,
					dev->head, dev->sector, dev->count);

				if (! get_sector(dev, drive, &addr)) {
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}
				hdd_active(drive->hdd_num, 1);

				/* Read data from the image. */
				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->buff);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				if (dev->command[0] == CMD_READ_LONG)
					dev->buff_cnt += 4;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				dev->status &= ~STAT_DRQ;
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_write(dev->dma, dev->buff[dev->buff_pos]);
					if (val == DMA_NODATA) {
						ERRLOG("ST506: CMD_READ out of data!\n");
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->callback = ST506_TIME;
					dev->state = STATE_SENT_DATA;
				}
				break;

			case STATE_SENT_DATA:
				if (--dev->count == 0) {
					hdd_active(drive->hdd_num, 0);
					st506_complete(dev);
					break;
				}

				next_sector(dev, drive);

				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* Read data from the image. */
				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->buff);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_SEND_DATA;
				break;
		}
		break;

	case CMD_WRITE:
#if 0
	case CMD_WRITE_LONG:
#endif
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: WRITE%s(%i, %i/%i/%i, %i)\n",
					(dev->command[0] == CMD_WRITE_LONG) ? "_LONG" : "",
					dev->drive_sel, dev->cylinder,
					dev->head, dev->sector, dev->count);

				if (! get_sector(dev, drive, &addr)) {
//					st506_error(dev, dev->error);
					st506_error(dev, ERR_BAD_PARAMETER);
					st506_complete(dev);
					return;
				}

				hdd_active(drive->hdd_num, 1);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				if (dev->command[0] == CMD_WRITE_LONG)
					dev->buff_cnt += 4;
				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVE_DATA:
				dev->status &= ~STAT_DRQ;
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
						ERRLOG("ST506: CMD_WRITE out of data!\n");
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->buff[dev->buff_pos] = val & 0xff;

					dev->callback = ST506_TIME;
					dev->state = STATE_RECEIVED_DATA;
				}
				break;

			case STATE_RECEIVED_DATA:
				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* Write data to image. */
				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *)dev->buff);

				if (--dev->count == 0) {
					hdd_active(drive->hdd_num, 0);
					st506_complete(dev);
					break;
				}

				next_sector(dev, drive);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_RECEIVE_DATA;
				break;
		}
		break;

	case CMD_SEEK:
		if (drive->present) {
			val = get_chs(dev, drive);
			DEBUG("ST506: SEEK(%i, %i) [%i]\n",
				dev->drive_sel, drive->cylinder, val);
			if (! val)
				st506_error(dev, ERR_SEEK_ERROR);
		} else
			st506_error(dev, ERR_NOT_READY);	//AVAILABLE
		st506_complete(dev);
		break;

	case CMD_SPECIFY:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = 8;
				dev->status = STAT_BSY | STAT_REQ;
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVED_DATA:
				drive->cfg_cyl = dev->buff[1] | (dev->buff[0] << 8);
				drive->cfg_hpc = dev->buff[2];
				DEBUG("ST506: drive%i: cyls=%i, heads=%i\n",
					dev->drive_sel, drive->cfg_cyl, drive->cfg_hpc);
				st506_complete(dev);
				break;
		}
		break;

	case CMD_READ_ECC_BURST_LEN:
		switch (dev->state) {
			case STATE_START_COMMAND:
				DEBUG("ST506: READ_ECC_BURST_LEN\n");
				dev->buff_pos = 0;
				dev->buff_cnt = 1;
				dev->buff[0] = 0;	/* 0 bits */
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_READ_BUFFER:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				DEBUG("ST506: READ_BUFFER (%i)\n",
						dev->buff_cnt);

				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				dev->status &= ~STAT_DRQ;
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_write(dev->dma, dev->buff[dev->buff_pos]);
					if (val == DMA_NODATA) {
						ERRLOG("ST506: CMD_READ_BUFFER out of data!\n");
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->callback = ST506_TIME;
					dev->state = STATE_SENT_DATA;
				}
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_WRITE_BUFFER:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				DEBUG("ST506: WRITE_BUFFER (%i)\n",
						dev->buff_cnt);

				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					dev->callback = ST506_TIME;
					dev->status |= STAT_DRQ;
				}
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVE_DATA:
				dev->status &= ~STAT_DRQ;
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
						ERRLOG("ST506: CMD_WRITE_BUFFER out of data!\n");
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->buff[dev->buff_pos] = val & 0xff;

					dev->state = STATE_RECEIVED_DATA;
					dev->callback = ST506_TIME;
				}
				break;

			case STATE_RECEIVED_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_INQUIRY_ST11:
		if (dev->type == 11 || dev->type == 12) switch (dev->state) {
			case STATE_START_COMMAND:
				DEBUG("ST506: INQUIRY (type=%i)\n", dev->type);
				dev->buff_pos = 0;
				dev->buff_cnt = 2;
				dev->buff[0] = 0x80;		/* "ST-11" */
				dev->buff[1] = dev->misc;	/* revision */
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_RAM_DIAGNOSTIC:
		DEBUG("ST506: RAM_DIAG\n");
		st506_complete(dev);
		break;

	case CMD_CTRLR_DIAGNOSTIC:
		DEBUG("ST506: CTRLR_DIAG\n");
		st506_complete(dev);
		break;

	case CMD_FORMAT_ST11:
		if (dev->type == 11 || dev->type == 12) switch (dev->state) {
			/* Apparently, ONLY the reserved cylinder (-1) .. */
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				DEBUG("ST506: FORMAT_ST11(%i) parm=%i\n",
					dev->drive_sel, dev->command[1]);
				hdd_active(drive->hdd_num, 1);
				dev->callback = ST506_TIME;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:	/* wrong, but works */
				if (! get_sector(dev, drive, &addr)) {
					hdd_active(drive->hdd_num, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				hdd_image_zero(drive->hdd_num,
					       addr, drive->cfg_spt);

				/* Wait 20msec per cylinder. */
				dev->callback = ST506_TIME_MS * 20;

				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				hdd_active(drive->hdd_num, 0);
				st506_complete(dev);
				break;
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_GET_GEOMETRY_ST11:
		if (dev->type == 11 || dev->type == 12) switch (dev->state) {
			/*
			 *  [0]  = 0xda;		// magic
			 *  [1]  = 0xbe;		// magic
			 *  [2]  = cyl_hi
			 *  [3]  = cyl_lo
			 *  [4]  = heads
			 *  [5]  = sectors
			 *  [6]  = interleave
			 *  [7]  = 00			// ??
			 *  [8]  = 01			// ??
			 *  [9]  = 03			// ??
			 *  [10] = landing_hi
			 *  [11] = landing_lo
			 *  [12] = 'SEAGATESTxxxxxx'	// drive model
			 *  [29] .. = 00
			 *  [41] = 02			// ??
			 *
			 * This is the data block written to cylinder 0
			 * somewhere by the ST-11 BIOS, and which we
			 * read back here, and then send to the caller.
			 *
			 * We do not yet know where this block is, so
			 * for now, we just fake it. Of course, since
			 * we do not with real drives, we could simply
			 * write it anywhere on that cylinder, but OK..
			 */
			case STATE_START_COMMAND:
				INFO("ST506: GET GEO (%i) parm=%i\n",
					dev->drive_sel, dev->command[2]);

				/* Read data from image. */
				hdd_image_read(drive->hdd_num, 0, 1,
					       (uint8_t *)dev->buff);
#ifdef x_DEBUG
{
  char temp[20480];
  hexdump_p(temp, dev->buff, 512);
  INFO("ST506: sector buffer:\n%s\n", temp);
}
#endif
				/* Send geometry data. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;

#if 0
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->buff[0] = 0xda;
				dev->buff[1] = 0xbe;
				dev->buff[2] = (drive->tracks >> 8) & 0xff;
				dev->buff[3] = drive->tracks & 0xff;
				dev->buff[4] = drive->hpc;
				dev->buff[5] = drive->spt;
				dev->buff[6] = drive->interleave;
				dev->buff[7] = 0x00;
				dev->buff[8] = 0x01;
				dev->buff[9] = 0x03;
				dev->buff[10] = (drive->tracks >> 8) & 0xff;
				dev->buff[11] = drive->tracks & 0xff;
				memcpy(&dev->buff[12], "SEAGATESTxxxxxx", 15);
				dev->buff[41] = 0x02;
#endif
				dev->state = STATE_SEND_DATA;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_SET_GEOMETRY_ST11:
		if (dev->type == 11 || dev->type == 12) switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = 512;	// 42
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->state = STATE_RECEIVE_DATA;
				dev->status = STAT_BSY | STAT_REQ;
				break;

			case STATE_RECEIVED_DATA:
				/*
				 * See above as well.
				 *
				 * This is the data block written to cylinder 0
				 * somewhere by the ST-11 BIOS, and which we
				 * read back here, and then send to the caller.
				 *
				 * We do not yet know where this block is, so
				 * for now, we just fake it. Of course, since
				 * we do not with real drives, we could simply
				 * write it anywhere on that cylinder, but OK..
				 */
				INFO("ST506: SET GEO (%i) parm=%i\n",
					dev->drive_sel, dev->command[2]);
				INFO("ST506: [ %02x %02x %02x %02x %02x %02x %02x %02x ]\n",
					dev->buff[0], dev->buff[1],
					dev->buff[2], dev->buff[3],
					dev->buff[4], dev->buff[5],
					dev->buff[6], dev->buff[7]);
				st506_complete(dev);
				break;
		} else if (dev->type == 1) {
			/* DTC sends this.. */
			st506_complete(dev);
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_SET_STEP_RATE_DTC:
		if (dev->type == 1) {
			/* For DTC, we are done. */
			st506_complete(dev);
		} else if (dev->type == 11 || dev->type == 12) {
			/*
			 * For Seagate ST-11, this is WriteGeometry.
			 *
			 * By the time this command is sent, it will have
			 * formatted the first track, so it should be good,
			 * and our sector buffer contains the magic data
			 * (see above) we need to write to it.
			 */
			INFO("ST506: WRITE GEO (%i) parm=%i\n",
				dev->drive_sel, dev->command[2]);
			INFO("ST506: [ %02x %02x %02x %02x %02x %02x %02x %02x ]\n",
				dev->buff[0], dev->buff[1], dev->buff[2],
				dev->buff[3], dev->buff[4], dev->buff[5],
				dev->buff[6], dev->buff[7]);

			/* Write data to image. */
			hdd_image_write(drive->hdd_num, 0, 1,
					(uint8_t *)dev->buff);
#ifdef x_DEBUG
{
  char temp[20480];
  hexdump_p(temp, dev->buff, 512);
  INFO("ST506: sector buffer:\n%s\n", temp);
}
#endif

			st506_complete(dev);
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_GET_DRIVE_PARAMS_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = 4;
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->buff[0] = drive->tracks & 0xff;
				dev->buff[1] = ((drive->tracks >> 2) & 0xc0) |
						dev->spt;
				dev->buff[2] = drive->hpc - 1;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_SET_GEOMETRY_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				val = dev->command[1] & 0x01;
				DEBUG("ST506: DTC_GET_GEOMETRY(%i) %i\n",
						dev->drive_sel, val);
				dev->buff_pos = 0;
				dev->buff_cnt = 16;
				dev->status = STAT_BSY | STAT_REQ;
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVED_DATA:
				/* FIXME: ignore the results. */
				st506_complete(dev);
			break;
		}
		break;

	case CMD_GET_GEOMETRY_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				val = dev->command[1] & 0x01;
				DEBUG("ST506: DTC_GET_GEOMETRY(%i) %i\n",
						dev->drive_sel, val);
				dev->buff_pos = 0;
				dev->buff_cnt = 16;
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->buff[4] = drive->tracks & 0xff;
				dev->buff[5] = (drive->tracks >> 8) & 0xff;
				dev->buff[10] = drive->hpc;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	default:
		ERRLOG("ST506: unknown command:\n");
		ERRLOG("ST506: %02x %02x %02x %02x %02x %02x\n",
			dev->command[0], dev->command[1], dev->command[2],
			dev->command[3], dev->command[4], dev->command[5]);
		st506_error(dev, ERR_BAD_COMMAND);
		st506_complete(dev);
    }
}


/* Read from one of the registers. */
static uint8_t
st506_read(uint16_t port, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 3) {
	case 0:		/* read data */
		dev->status &= ~STAT_IRQ;
		switch (dev->state) {
			case STATE_COMPLETION_BYTE:
				ret = dev->compl;
				dev->status = 0x00;
				dev->state = STATE_IDLE;
				break;
			
			case STATE_SEND_DATA:
				ret = dev->buff[dev->buff_pos++];
				if (dev->buff_pos == dev->buff_cnt) {
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_SENT_DATA;
					dev->callback = ST506_TIME;
				}
				break;
		}
		break;

	case 1:		/* read status */
		ret = dev->status;
		break;

	case 2:		/* read option jumpers */
		ret = dev->switches;
		break;
    }
    DBGLOG(1, "ST506: read(%04x) = %02x\n", port, ret);

    return(ret);
}


/* Write to one of the registers. */
static void
st506_write(uint16_t port, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;

    DBGLOG(1, "ST506: write(%04x, %02x)\n", port, val);
    switch (port & 3) {
	case 0:		/* write data */
		switch (dev->state) {
			case STATE_RECEIVE_COMMAND:	/* command data */
				dev->command[dev->buff_pos++] = val;
				if (dev->buff_pos == dev->buff_cnt) {
					/* We have a new command. */
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_START_COMMAND;
					dev->callback = ST506_TIME;
				}
				break;

			case STATE_RECEIVE_DATA:	/* data */
				dev->buff[dev->buff_pos++] = val;
				if (dev->buff_pos == dev->buff_cnt) {
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_RECEIVED_DATA;
					dev->callback = ST506_TIME;
				}
				break;
		}
		break;

	case 1:		/* controller reset */
		dev->status = 0x00;
		break;

	case 2:		/* generate controller-select-pulse */
		dev->status = STAT_BSY | STAT_CD | STAT_REQ;
		dev->buff_pos = 0;
		dev->buff_cnt = sizeof(dev->command);
		dev->state = STATE_RECEIVE_COMMAND;
		break;

	case 3:		/* DMA/IRQ enable register */
		dev->irq_dma = val;
		break;
    }
}


/* Write to ROM (or scratchpad RAM.) */
static void
mem_write(uint32_t addr, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint32_t ptr, mask = 0;

    addr &= dev->bios_rom.mask;

    switch(dev->type) {
	case 11:	/* ST-11M */
	case 12:	/* ST-11R */
		mask = 0x1fff;	/* ST-11 decodes RAM on each 8K block */
		break;

	default:
		break;
    }

    ptr = (dev->bios_rom.mask & mask) - dev->bios_ram;
    if (mask && ((addr & mask) > ptr) &&
		((addr & mask) <= (ptr + dev->bios_ram))) {
	dev->scratch[addr & (dev->bios_ram - 1)] = val;
    }
}


static uint8_t
mem_read(uint32_t addr, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint32_t ptr, mask = 0;
    uint8_t ret = 0xff;

    addr &= dev->bios_rom.mask;

    switch(dev->type) {
	case 0:		/* Xebec */
		if (addr >= 0x001000)
			DEBUG("ST506: Xebec ROM access(0x%06lx)\n", addr);
		break;

	case 1:		/* DTC */
		if (addr >= 0x002000)
			DEBUG("ST506: DTC-5150X ROM access(0x%06lx)\n", addr);
		break;

	case 11:	/* ST-11M */
	case 12:	/* ST-11R */
		mask = 0x1fff;	/* ST-11 decodes RAM on each 8K block */
		break;

	default:
		break;
    }

    ptr = (dev->bios_rom.mask & mask) - dev->bios_ram;
    if (mask && ((addr & mask) > ptr) &&
		((addr & mask) <= (ptr + dev->bios_ram))) {
	ret = dev->scratch[addr & (dev->bios_ram - 1)];
    } else
	ret = dev->bios_rom.rom[addr];

    return(ret);
}


/*
 * Set up and load the ROM BIOS for this controller.
 *
 * This is straightforward for most, but some (like the ST-11x)
 * map part of the area as scratchpad RAM, so we cannot use the
 * standard 'rom_init' function here.
 */
static void
loadrom(hdc_t *dev, const wchar_t *fn)
{
    uint32_t size;
    FILE *fp;

    if ((fp = plat_fopen(rom_path(fn), L"rb")) == NULL) {
	ERRLOG("ST506: BIOS ROM '%ls' not found!\n", fn);
	return;
    }

    /* Initialize the ROM entry. */
    memset(&dev->bios_rom, 0x00, sizeof(rom_t));

    /* Manually load and process the ROM image. */
    (void)fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    (void)fseek(fp, 0L, SEEK_SET);

    /*
     * The Xebec and DTC-5150X ROMs seem to be probing for
     * (other) ROMs at addresses between their own size and
     * 16K, at 2K blocks. So, we must enable all of that..
     */
    if (dev->type < 2)
	size = 16384;

    /* Load the ROM data. */
    dev->bios_rom.rom = (uint8_t *)mem_alloc(size);
    memset(dev->bios_rom.rom, 0xff, size);
    (void)fread(dev->bios_rom.rom, size, 1, fp);
    (void)fclose(fp);

    /* Set up an address mask for this memory. */
    dev->bios_size = size;
    dev->bios_rom.mask = (size - 1);

    /* Map this system into the memory map. */
    mem_map_add(&dev->bios_rom.mapping, dev->bios_addr, size,
		mem_read,NULL,NULL, mem_write,NULL,NULL,
		dev->bios_rom.rom, MEM_MAPPING_EXTERNAL, dev);
}


static void
loadhd(hdc_t *dev, int c, int d, const wchar_t *fn)
{
    drive_t *drive = &dev->drives[c];

    if (! hdd_image_load(d)) {
	drive->present = 0;
	return;
    }
	
    /* Make sure we can do this. */
    if (hdd[d].spt != dev->spt) {
	/*
	 * Uh-oh, MFM/RLL mismatch.
	 *
	 * Although this would be no issue in the code itself,
	 * most of the BIOSes were hardwired to whatever their
	 * native SPT setting was, so, do not allow this here.
	 */
	ERRLOG("ST506: drive%i: MFM/RLL mismatch (%i/%i)\n",
				c, hdd[d].spt, dev->spt);
	hdd_image_close(d);
	drive->present = 0;
	return;
    }

    drive->spt = (uint8_t)hdd[d].spt;
    drive->hpc = (uint8_t)hdd[d].hpc;
    drive->tracks = (uint16_t)hdd[d].tracks;

    drive->hdd_num = d;
    drive->present = 1;
}


/* Set the "drive type" switches for the IBM Xebec controller. */
static void
set_switches(hdc_t *dev)
{
    drive_t *drive;
    int c, d;

    dev->switches = 0x00;

    for (d = 0; d < ST506_NUM; d++) {
	drive = &dev->drives[d];

	if (! drive->present) continue;

	for (c = 0; c < 4; c++) {
		if ((drive->spt == hd_types[c].spt) &&
		    (drive->hpc == hd_types[c].hpc) &&
		    (drive->tracks == hd_types[c].tracks)) {
			dev->switches |= (c << (d ? 0 : 2));
			break;
		}
	}

	INFO("ST506: ");
	if (c == 4)
		INFO("*WARNING* drive%i unsupported", d);
	  else
		INFO("drive%i is type %i", d, c);
	INFO(" (%i/%i/%i)\n", drive->tracks, drive->hpc, drive->spt);
    }
}


static void *
st506_init(const device_t *info, UNUSED(void *parent))
{
    const wchar_t *fn;
    hdc_t *dev;
    int i, c;

    dev = (hdc_t *)mem_alloc(sizeof(hdc_t));
    memset(dev, 0x00, sizeof(hdc_t));
    dev->type = info->local & 255;

    /* Set defaults for the controller. */
    dev->spt = MFM_SECTORS;
    dev->base = 0x0320;
    dev->irq = 5;
    dev->dma = 3;
    dev->bios_addr = 0xc8000;

    fn = info->path;
    switch(dev->type) {
	case 0:		/* Xebec (MFM) */
		break;

	case 1:		/* DTC5150 (MFM) */
		dev->switches = 0xff;
		break;

#ifdef USE_ST11
	case 12:	/* Seagate ST-11R (RLL) */
		dev->spt = RLL_SECTORS;
		/*FALLTHROUGH*/

	case 11:	/* Seagate ST-11M (MFM) */
		dev->switches = 0x01;	/* fixed */
		dev->misc = device_get_config_int("revision");
		switch(dev->misc) {
			case 1:		/* v1.1 */
				break;

			case 5:		/* v1.7 */
				fn = ST11_BIOS_FILE_OLD;
				break;

			case 19:	/* v2.0 */
				fn = ST11_BIOS_FILE_NEW;
				break;
		}
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->bios_addr = device_get_config_hex20("bios_addr");
		dev->bios_ram = 64;	/* scratch RAM size */

		/*
		 * Industrial Madness Alert.
		 *
		 * With the ST-11 controller, Seagate decided to act
		 * like they owned the industry, and reserved the
		 * first cylinder of a drive for the controller. So,
		 * when the host accessed cylinder 0, that would be
		 * the actual cylinder 1 on the drive, and so on.
		 */
		dev->cyl_off = 1;
		break;
#endif

#ifdef USE_WD1002
	case 21:	/* Western Digital WD1002A-WX1 (MFM) */
		dev->switches = 0x10;	/* autobios */
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->bios_addr = device_get_config_hex20("bios_addr");
		break;

	case 22:	/* Western Digital WD1002A-27X (RLL) */
		dev->switches = 0x10;	/* autobios */
		dev->spt = RLL_SECTORS;
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->bios_addr = device_get_config_hex20("bios_addr");
		break;
#endif

	case 101:	/* Western Digital WD1002A-WX1 (MFM) */
		dev->switches = 0x10;	/* autobios */
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->bios_addr = device_get_config_hex20("bios_addr");
		break;
    }

    /* Load the ROM BIOS. */
    if (fn != NULL)
	loadrom(dev, fn);

    /* Set up the I/O region. */
    io_sethandler(dev->base, 4,
		  st506_read,NULL,NULL, st506_write,NULL,NULL, dev);

    /* Add the timer. */
    timer_add(st506_callback, dev, &dev->callback, &dev->callback);

    INFO("ST506: %s (I/O=%03X, IRQ=%i, DMA=%i, BIOS @0x%06lX, size %lu)\n",
	info->name,dev->base,dev->irq,dev->dma, dev->bios_addr,dev->bios_size);

    /* Load any drives configured for us. */
    INFO("ST506: looking for disks..\n");
    for (c = 0, i = 0; i < HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_ST506) && (hdd[i].bus_id.st506_channel < ST506_NUM)) {
		INFO("ST506: disk '%ls' on channel %i\n",
			hdd[i].fn, hdd[i].bus_id.st506_channel);
		loadhd(dev, hdd[i].bus_id.st506_channel, i, hdd[i].fn);

		if (++c > ST506_NUM) break;
	}
    }
    INFO("ST506: %i disks loaded.\n", c);

    /* For the Xebec, set the switches now. */
    if (dev->type == 0)
	set_switches(dev);

    /* Initial "active" drive parameters. */
    for (c = 0; c < ST506_NUM; c++) {
	dev->drives[c].cfg_cyl = dev->drives[c].tracks;
	dev->drives[c].cfg_hpc = dev->drives[c].hpc;
	dev->drives[c].cfg_spt = dev->drives[c].spt;
    }

    return(dev);
}


static void
st506_close(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    drive_t *drive;
    int d;

    for (d = 0; d < ST506_NUM; d++) {
	drive = &dev->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    if (dev->bios_rom.rom != NULL) {
	free(dev->bios_rom.rom);
	dev->bios_rom.rom = NULL;
    }

    free(dev);
}


static const device_config_t dtc_config[] = {
    {
	"bios_addr", "BIOS address", CONFIG_HEX20, "", 0xc8000,
	{
		{
			"Disabled", 0x00000
		},
		{
			"C800H", 0xc8000
		},
		{
			"CA00H", 0xca000
		},
		{
			"D800H", 0xd8000
		},
		{
			"F400H", 0xf4000
		},
		{
			NULL
		}
	}
    },
    {
	NULL
    }
};

#ifdef USE_ST11
static const device_config_t st11_config[] = {
    {
	"base", "Address", CONFIG_HEX16, "", 0x0320,
	{
		{
			"320H", 0x0320
		},
		{
			"324H", 0x0324
		},
		{
			"328H", 0x0328
		},
		{
			"32CH", 0x032c
		},
		{
			NULL
		}
	}
    },
    {
	"irq", "IRQ", CONFIG_SELECTION, "", 5,
	{
		{
			"IRQ 2", 2
		},
		{
			"IRQ 5", 5
		},
		{
			NULL
		}
	}
    },
    {
	"bios_addr", "BIOS address", CONFIG_HEX20, "", 0xc8000,
	{
		{
			"Disabled", 0x00000
		},
		{
			"C800H", 0xc8000
		},
		{
			"D000H", 0xd0000
		},
		{
			"D800H", 0xd8000
		},
		{
			"E000H", 0xe0000
		},
		{
			NULL
		}
	}
    },
    {
	"revision", "Board Revision", CONFIG_SELECTION, "", 19,
	{
#if 0	/*Not Tested Yet*/
		{
			"Rev. 01 (v1.1)", 1
		},
#endif
		{
			"Rev. 05 (v1.7)", 5
		},
		{
			"Rev. 19 (v2.0)", 19
		},
		{
			NULL
		}
	}
    },
    {
	NULL
    }
};
#endif

static const device_config_t wd_config[] = {
    {
	"bios_addr", "BIOS address", CONFIG_HEX20, "", 0xc8000,
	{
		{
			"Disabled", 0x00000
		},
		{
			"C800H", 0xc8000
		},
		{
			NULL
		}
	}
    },
    {
	"base", "Address", CONFIG_HEX16, "", 0x0320,
	{
		{
			"320H", 0x0320
		},
		{
			"324H", 0x0324
		},
		{
			NULL
		}
	}
    },
    {
	"irq", "IRQ", CONFIG_SELECTION, "", 5,
	{
		{
			"IRQ 2", 2
		},
		{
			"IRQ 5", 5
		},
		{
			NULL
		}
	}
    },
    {
	NULL
    }
};


const device_t st506_xt_xebec_device = {
    "IBM PC Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 0,
    XEBEC_BIOS_FILE,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

const device_t st506_xt_dtc5150x_device = {
    "DTC 5150X Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 1,
    DTC_BIOS_FILE,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    dtc_config
};

#ifdef USE_ST11
const device_t st506_xt_st11_m_device = {
    "ST-11M Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 11,
    ST11_BIOS_FILE_NEW,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    st11_config
};

const device_t st506_xt_st11_r_device = {
    "ST-11R RLL Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 12,
    ST11_BIOS_FILE_NEW,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    st11_config
};
#endif

#ifdef USE_WD1002
const device_t st506_xt_wd1002a_wx1_device = {
    "WD1002A-WX1 Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 21,
    WD1002A_WX1_BIOS_FILE,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    wd_config
};

const device_t st506_xt_wd1002a_27x_device = {
    "WD1002A-27X RLL Fixed Disk Adapter",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 22,
    WD1002A_27X_BIOS_FILE,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    wd_config
};
#endif

const device_t st506_xt_olim240_hdc_device = {
    "WD1002A-WX1 Fixed Disk Adapter (no BIOS)",
    DEVICE_ISA,
    (HDD_BUS_ST506 << 8) | 101,
    NULL,
    st506_init, st506_close, NULL,
    NULL, NULL, NULL, NULL,
    wd_config
};
