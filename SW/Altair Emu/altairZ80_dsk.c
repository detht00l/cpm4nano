/*	altairZ80_dsk.c: MITS Altair 88-DISK Simulator
		Written by Charles E Owen ((c) 1997, Commercial use prohibited)
		Minor modifications by Peter Schorn, 2001

		The 88_DISK is a 8-inch floppy controller which can control up
		to 16 daisy-chained Pertec FD-400 hard-sectored floppy drives.
		Each diskette has physically 77 tracks of 32 137-byte sectors
		each.

		The controller is interfaced to the CPU by use of 3 I/O addreses,
		standardly, these are device numbers 10, 11, and 12 (octal).

		Address	Mode	Function
		-------	----	--------

			10			Out		Selects and enables Controller and Drive
			10			In		Indicates status of Drive and Controller
			11			Out		Controls Disk Function
			11			In		Indicates current sector position of disk
			12			Out		Write data
			12			In		Read data

		Drive Select Out (Device 10 OUT):

		+---+---+---+---+---+---+---+---+
		| C | X | X | X |   Device      |
		+---+---+---+---+---+---+---+---+

		C = If this bit is 1, the disk controller selected by 'device' is
				cleared. If the bit is zero, 'device' is selected as the
				device being controlled by subsequent I/O operations.
		X = not used
		Device = value zero thru 15, selects drive to be controlled.

		Drive Status In (Device 10 IN):

		+---+---+---+---+---+---+---+---+
		| R | Z | I | X | X | H | M | W |
		+---+---+---+---+---+---+---+---+

		W - When 0, write circuit ready to write another byte.
		M - When 0, head movement is allowed
		H - When 0, indicates head is loaded for read/write
		X - not used (will be 0)
		I - When 0, indicates interrupts enabled (not used this simulator)
		Z - When 0, indicates head is on track 0
		R - When 0, indicates that read circuit has new byte to read

		Drive Control (Device 11 OUT):

		+---+---+---+---+---+---+---+---+
		| W | C | D | E | U | H | O | I |
		+---+---+---+---+---+---+---+---+

		I - When 1, steps head IN one track
		O - When 1, steps head OUT one track
		H - When 1, loads head to drive surface
		U - When 1, unloads head
		E - Enables interrupts (ignored this simulator)
		D - Disables interrupts (ignored this simulator)
		C - When 1 lowers head current (ignored this simulator)
		W - When 1, starts Write Enable sequence:	W bit on device 10
				(see above) will go 1 and data will be read from port 12
				until 137 bytes have been read by the controller from
				that port. The W bit will go off then, and the sector data
				will be written to disk. Before you do this, you must have
				stepped the track to the desired number, and waited until
				the right sector number is presented on device 11 IN, then
				set this bit.

		Sector Position (Device 11 IN):

		As the sectors pass by the read head, they are counted and the
		number of the current one is available in this register.

		+---+---+---+---+---+---+---+---+
		| X | X |  Sector Number    | T |
		+---+---+---+---+---+---+---+---+

		X = Not used
		Sector number = binary of the sector number currently under the
										head, 0-31.
		T = Sector True, is a 1 when the sector is positioned to read or
				write.

*/

#include <stdio.h>
#include "altairZ80_defs.h"

#define	UNIT_V_ENABLE				(UNIT_V_UF + 0)			/* Write Enable */
#define UNIT_ENABLE					(1 << UNIT_V_ENABLE)
#define DSK_SECTSIZE				137									/* size of sector */
#define DSK_SECT						32									/* sectors per track */
#define TRACKS							254									/* number of tracks,
																									original Altair has 77 tracks only */
#define DSK_TRACSIZE				(DSK_SECTSIZE * DSK_SECT)
#define DSK_SIZE						(DSK_TRACSIZE * TRACKS)
#define TRACE_IN_OUT				1
#define TRACE_READ_WRITE		2
#define TRACE_SECTOR_STUCK	4
#define TRACE_TRACK_STUCK		8

int32 dsk10(int32 port, int32 io, int32 data);
int32 dsk11(int32 port, int32 io, int32 data);
int32 dsk12(int32 port, int32 io, int32 data);
int32 dskseek(UNIT *xptr);
t_stat dsk_boot (int32 unitno);
t_stat dsk_reset (DEVICE *dptr);
t_stat dsk_svc (UNIT *uptr);
void writebuf();

extern int32 PCX;
extern int32 saved_PC;
extern FILE *sim_log;
extern void PutBYTEWrapper(register uint32 Addr, register uint32 Value);

/* Global data on status */

int32 cur_disk = 8;		/* Currently selected drive */
int32 cur_track[9]	= {0, 0, 0, 0, 0, 0, 0, 0, 0xff};
int32 cur_sect[9]		= {0, 0, 0, 0, 0, 0, 0, 0, 0xff};
int32 cur_byte[9]		= {0, 0, 0, 0, 0, 0, 0, 0, 0xff};
int32 cur_flags[9]	= {0, 0, 0, 0, 0, 0, 0, 0, 0};
int32 trace_flag		= 0;
int32 in9_count			= 0;
int32 in9_message		= FALSE;

/*	The following code is used to boot a drive. It is a modification of
		the boot ROM code from above and allows to boot from an arbitrary
		drive.																																*/
int32 dskBootCode[bootrom_size] = {
	0x21, 0x00, 0x5c, 0x11, 0x13, 0xfe, 0x0e, 0xa3, /* fe00-fe07 */
	0x1a, 0x77, 0x13, 0x23, 0x0d, 0xc2, 0x08, 0xfe, /* fe08-fe0f */
	0xc3, 0x00, 0x5c, 0x31, 0x3c, 0x5d, 0xaf, 0xd3, /* fe10-fe17 */
	0xfd, 0x3e, 0x0c, 0xd3, 0xfe, 0xaf, 0xd3, 0xfe, /* fe18-fe1f */
	0x3e, 0x00, 0xd3, 0x08, 0x3e, 0x04, 0xd3, 0x09, /* fe20-fe27 */
	0xc3, 0x23, 0x5c, 0xdb, 0x08, 0xe6, 0x02, 0xc2, /* fe28-fe2f */
	0x18, 0x5c, 0x3e, 0x02, 0xd3, 0x09, 0xdb, 0x08, /* fe30-fe37 */
	0xe6, 0x40, 0xc2, 0x18, 0x5c, 0x11, 0x00, 0x00, /* fe38-fe3f */
	0x06, 0x08, 0xc3, 0x34, 0x5c, 0x06, 0x00, 0x3e, /* fe40-fe47 */
	0x10, 0xf5, 0xd5, 0xc5, 0xd5, 0x11, 0x86, 0x80, /* fe48-fe4f */
	0x21, 0xa3, 0x5c, 0xdb, 0x09, 0x1f, 0xda, 0x40, /* fe50-fe57 */
	0x5c, 0xe6, 0x1f, 0xb8, 0xc2, 0x40, 0x5c, 0xdb, /* fe58-fe5f */
	0x08, 0xb7, 0xfa, 0x4c, 0x5c, 0xdb, 0x0a, 0x77, /* fe60-fe67 */
	0x23, 0x1d, 0xca, 0x62, 0x5c, 0x1d, 0xdb, 0x0a, /* fe68-fe6f */
	0x77, 0x23, 0xc2, 0x4c, 0x5c, 0xe1, 0x11, 0xa6, /* fe70-fe77 */
	0x5c, 0x0e, 0x80, 0x1a, 0x77, 0x13, 0x23, 0x0d, /* fe78-fe7f */
	0xc2, 0x68, 0x5c, 0xc1, 0xeb, 0xf1, 0xf1, 0x21, /* fe80-fe87 */
	0x00, 0x5c, 0x7a, 0xbc, 0xc2, 0x7e, 0x5c, 0x7b, /* fe88-fe8f */
	0xbd, 0xd2, 0x9c, 0x5c, 0x04, 0x04, 0x78, 0xfe, /* fe90-fe97 */
	0x20, 0xda, 0x34, 0x5c, 0x06, 0x01, 0xca, 0x34, /* fe98-fe9f */
	0x5c, 0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x8e, 0x5c, /* fea0-fea7 */
	0x3e, 0x01, 0xd3, 0x09, 0xc3, 0x32, 0x5c, 0x3e, /* fea8-feaf */
	0x80, 0xd3, 0x08, 0xc3, 0x00, 0x00, 0x00, 0x00, /* feb0-feb7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* feb8-febf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fec0-fec7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fec8-fecf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fed0-fed7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fed8-fedf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fee0-fee7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fee8-feef */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fef0-fef7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fef8-feff */
};

char dskbuf[DSK_SECTSIZE];	/* Data Buffer */
int32 dirty = 0;	/* 1 when buffer has unwritten data in it */
UNIT *dptr;				/* fileref to write dirty buffer to */

/* 88DSK Standard I/O Data Structures */

UNIT dsk_unit[] = {
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, DSK_SIZE) } };

REG dsk_reg[] = {
	{ DRDATA (DISK, cur_disk, 4) },
	{ ORDATA (TRACE, trace_flag, 8) },
	{ DRDATA (IN9, in9_count, 4), REG_RO },
	{ NULL } };

DEVICE dsk_dev = {
	"DSK", dsk_unit, dsk_reg, NULL,
	8, 10, 31, 1, 8, 8,
	NULL, NULL, &dsk_reset,
	&dsk_boot, NULL, NULL };

/* Service routines to handle simlulator functions */

/* service routine - actually gets char & places in buffer */

t_stat dsk_svc (UNIT *uptr)
{
	return SCPE_OK;
}

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
	cur_disk		= 0;
	trace_flag	= 0;
	in9_count		= 0;
	in9_message	= FALSE;
	return SCPE_OK;
}

#define seldsk1 (dsk_boot_origin + 0x21)	/* MOV A,<unitno>				*/
#define seldsk2 (dsk_boot_origin + 0xb0)	/* MOV a,80h | <unitno>	*/

t_stat dsk_boot (int32 unitno)
{
	int32 i;
	for (i = 0; i < bootrom_size; i++) {
		PutBYTEWrapper(i + dsk_boot_origin, dskBootCode[i] & 0xFF);
	}
	PutBYTEWrapper(seldsk1, unitno & 0xff);					/* select correct drive		*/
	PutBYTEWrapper(seldsk2, 0x80 | (unitno & 0xff));	/* clear disk controller	*/
	saved_PC = dsk_boot_origin;
	return SCPE_OK;
}

/*	I/O instruction handlers, called from the CPU module when an
		IN or OUT instruction is issued.

		Each function is passed an 'io' flag, where 0 means a read from
		the port, and 1 means a write to the port. On input, the actual
		input is passed as the return value, on output, 'data' is written
		to the device.
*/

/* Disk Controller Status/Select */

/*	IMPORTANT: The status flags read by port 8 IN instruction are
		INVERTED, that is, 0 is true and 1 is false. To handle this, the
		simulator keeps it's own status flags as 0=false, 1=true; and
		returns the COMPLEMENT of the status flags when read. This makes
		setting/testing of the flag bits more logical, yet meets the
		simulation requirement that they are reversed in hardware.
*/

int32 dsk10(int32 port, int32 io, int32 data)
{
	in9_count = 0;
	if (io == 0) {														/* IN: return flags */
		return ((~cur_flags[cur_disk]) & 0xFF);	/* Return the COMPLEMENT! */
	}

	/* OUT: Controller set/reset/enable/disable */
	if (dirty == 1)
		writebuf();
	if (trace_flag & TRACE_IN_OUT) {
		printf("\n[%04xh] OUT 08: %x", PCX, data);
		if (sim_log) {
			fprintf(sim_log, "\n[%04xh] OUT 08: %x", PCX, data);
		}
	}
	cur_disk = data & 0x0F;
	if ((((dsk_dev.units + cur_disk) -> flags) & UNIT_ATT) == 0) { /* nothing attached? */
		cur_disk = 8;
	}
	else {
		cur_sect[cur_disk] = 0xff;	/* reset internal counters */
		cur_byte[cur_disk] = 0xff;
		cur_flags[cur_disk] = data & 0x80 ?	0 /* Disable drive */ :
			(cur_track[cur_disk] == 0				?	0x5A /* Enable: head move true, track 0 if there	*/ :
																				0x1A); /* Enable: head move true */
	}
	return (0);	/* ignored since OUT */
}

/* Disk Drive Status/Functions */

int32 dsk11(int32 port, int32 io, int32 data)
{
	if (io == 0) {	 /* Read sector position */
		in9_count++;
		if ((trace_flag & TRACE_SECTOR_STUCK) && (in9_count > 2*DSK_SECT) && (!in9_message)) {
			in9_message = TRUE;
			printf("\n[%04xh] Looping on sector find %d.\n", PCX, cur_disk);
			if (sim_log) {
				fprintf(sim_log, "\n[%04xh] Looping on sector find %d.\n", PCX, cur_disk);
			}
		}
		if (trace_flag & TRACE_IN_OUT) {
			printf("\n[%04xh] IN 09", PCX);
			if (sim_log) {
				fprintf(sim_log, "\n[%04xh] IN 09", PCX);
			}
		}
		if (dirty == 1)
			writebuf();
		if (cur_flags[cur_disk] & 0x04) {	/* head loaded? */
			cur_sect[cur_disk]++;
			if (cur_sect[cur_disk] >= DSK_SECT)
				cur_sect[cur_disk] = 0;
			cur_byte[cur_disk] = 0xff;
			return (((cur_sect[cur_disk] << 1) & 0x3E)	/* return 'sector true' bit = 0 (true) */
				| 0xC0);																	/* set on 'unused' bits */
		} else {
			return (0);				/* head not loaded - return 0 */
		}
	}

	in9_count = 0;
	/* Drive functions */

	if (cur_disk > 7)
		return (0);				/* no drive selected - can do nothing */

	if (trace_flag & TRACE_IN_OUT) {
		printf("\n[%04xh] OUT 09: %x", PCX, data);
		if (sim_log) {
			fprintf(sim_log, "\n[%04xh] OUT 09: %x", PCX, data);
		}
	}
	if (data & 0x01) {			/* Step head in */
		if (trace_flag & TRACE_TRACK_STUCK) {
			if (cur_track[cur_disk] == (TRACKS-1)) {
				printf("\n[%04xh] Unnecessary step in for disk %d", PCX, cur_disk);
				if (sim_log) {
					fprintf(sim_log, "\n[%04xh] Unnecessary step in for disk %d", PCX, cur_disk);
				}
			}
		}
		cur_track[cur_disk]++;
		if (cur_track[cur_disk] > (TRACKS-1) )
			cur_track[cur_disk] = (TRACKS-1);
		if (dirty == 1)
			writebuf();
		cur_sect[cur_disk] = 0xff;
		cur_byte[cur_disk] = 0xff;
	}

	if (data & 0x02) {			/* Step head out */
		if (trace_flag & TRACE_TRACK_STUCK) {
			if (cur_track[cur_disk] == 0) {
				printf("\n[%04xh] Unnecessary step out for disk %d", PCX, cur_disk);
				if (sim_log) {
					fprintf(sim_log, "\n[%04xh] Unnecessary step out for disk %d", PCX, cur_disk);
				}
			}
		}
		cur_track[cur_disk]--;
		if (cur_track[cur_disk] < 0) {
			cur_track[cur_disk] = 0;
			cur_flags[cur_disk] |= 0x40;	/* track 0 if there */
		}
		if (dirty == 1)
			writebuf();
		cur_sect[cur_disk] = 0xff;
		cur_byte[cur_disk] = 0xff;
	}

	if (dirty == 1)
		writebuf();

	if (data & 0x04) {							/* Head load */
		cur_flags[cur_disk] |= 0x04;	/* turn on head loaded bit */
		cur_flags[cur_disk] |= 0x80;	/* turn on 'read data available */
	}

	if (data & 0x08) {							/* Head Unload */
		cur_flags[cur_disk] &= 0xFB;	/* off on 'head loaded' */
		cur_flags[cur_disk] &= 0x7F;	/* off on 'read data avail */
		cur_sect[cur_disk] = 0xff;
		cur_byte[cur_disk] = 0xff;
	}

	/* Interrupts & head current are ignored */

	if (data & 0x80) {							/* write sequence start */
		cur_byte[cur_disk] = 0;
		cur_flags[cur_disk] |= 0x01;	/* enter new write data on */
	}
	return (0);	/* ignored since OUT */
}

/* Disk Data In/Out*/

INLINE int32 dskseek(UNIT *xptr) {
	return fseek(xptr -> fileref, DSK_TRACSIZE * cur_track[cur_disk] +
		DSK_SECTSIZE * cur_sect[cur_disk], SEEK_SET);
}

int32 dsk12(int32 port, int32 io, int32 data)
{
	static int32 i;
	UNIT *uptr;

	in9_count = 0;
	uptr = dsk_dev.units + cur_disk;
	if (io == 0) {
		if (cur_byte[cur_disk] >= DSK_SECTSIZE) {
			/* physically read the sector */
			if (trace_flag & TRACE_READ_WRITE) {
				printf("\n[%04xh] IN 0A (READ) D%d T%d S%d", PCX, cur_disk, cur_track[cur_disk], cur_sect[cur_disk]);
				if (sim_log) {
					fprintf(sim_log, "\n[%04xh] IN 0A (READ) D%d T%d S%d", PCX, cur_disk, cur_track[cur_disk],
						cur_sect[cur_disk]);
				}
			}
			for (i = 0; i < DSK_SECTSIZE; i++) {
				dskbuf[i] = 0;
			}
			dskseek(uptr);
			fread(dskbuf, DSK_SECTSIZE, 1, uptr -> fileref);
			cur_byte[cur_disk] = 0;
		}
		return (dskbuf[cur_byte[cur_disk]++] & 0xFF);
	}
	else {
		if (cur_byte[cur_disk] >= DSK_SECTSIZE) {
			writebuf();
		}
		else {
			dirty = 1;
			dptr = uptr;
			dskbuf[cur_byte[cur_disk]++] = data & 0xFF;
		}
		return (0);	/* ignored since OUT */
	}
}

void writebuf()
{
	int32 i, rtn;

	i = cur_byte[cur_disk];			/* null-fill rest of sector if any */
	while (i < DSK_SECTSIZE) {
		dskbuf[i++] = 0;
	}
	if (trace_flag & TRACE_READ_WRITE) {
		printf("\n[%04xh] OUT 0A (WRITE) D%d T%d S%d", PCX, cur_disk,
			cur_track[cur_disk], cur_sect[cur_disk]);
		if (sim_log) {
			fprintf (sim_log, "\n[%04xh] OUT 0A (WRITE) D%d T%d S%d", PCX, cur_disk,
				cur_track[cur_disk], cur_sect[cur_disk]);
		}
	}
	if (dskseek(dptr)) {
		printf("\n[%04xh] fseek failed D%d T%d S%d", PCX, cur_disk,
			cur_track[cur_disk], cur_sect[cur_disk]);
		if (sim_log) {
			fprintf(sim_log, "\n[%04xh] fseek failed D%d T%d S%d", PCX, cur_disk,
				cur_track[cur_disk], cur_sect[cur_disk]);
		}
	}
	rtn = fwrite(dskbuf, DSK_SECTSIZE, 1, dptr -> fileref);
	if (rtn != 1) {
		printf("\n[%04xh] fwrite failed T%d S%d Return=%d", PCX, cur_track[cur_disk],
			cur_sect[cur_disk], rtn);
		if (sim_log) {
			fprintf(sim_log, "\n[%04xh] fwrite failed T%d S%d Return=%d", PCX, cur_track[cur_disk],
				cur_sect[cur_disk], rtn);
		}
	}
	cur_flags[cur_disk] &= 0xFE;							/* ENWD off */
	cur_byte[cur_disk] = 0xff;
	dirty = 0;
}