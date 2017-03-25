/*  CPM4NANO - i8080 & CP/M emulator for Arduino Nano 3.0 
*   Copyright (C) 2017 - Alexey V. Voronin @ FoxyLab 
*   Email:    support@foxylab.com
*   Website:  https://acdc.foxylab.com
*/

//TO DO
//проверить для четырех дисков

const uint32_t SD_MEM_SIZE  =   65536;
const uint32_t SD_FDD_A_OFFSET = 0x0001000; 
const uint32_t SD_FDD_B_OFFSET = 0x0002000; 
const uint32_t SD_FDD_C_OFFSET = 0x0003000; 
const uint32_t SD_FDD_D_OFFSET = 0x0004000; 
const uint8_t FDD_NUM = 4; 
const uint8_t CPM_EMPTY = 0xE5; //empty byte (disk)



#define SECTOR_SIZE   128
#define TRACK_SIZE    26
#define DISK_SIZE     77

#define FDD_SIZE TRACK_SIZE*DISK_SIZE //sectors

const uint8_t DISK_SUCCESS = 0;
const uint8_t DISK_ERROR = 1;

const uint16_t b_offset = 0xA800; //RAM Size - 20K   62K RAM
const uint16_t BOOT = 0;
const uint16_t TBASE = 0x100;
const uint16_t CBASE = 0x3400 + b_offset; //CCP 0xDC00
const uint16_t FBASE = 0x3c06 + b_offset; //0xE406

const uint16_t SP_INIT = CBASE - 0x100;

const uint16_t _BIOS = 0x4a00 + b_offset; //0xF200
//$4A00+$A800 43008  + 20K = 62K (63488 0xF800)
const uint16_t _BIOS_LO = 0x4a00 + b_offset;
const uint16_t _BIOS_HI = _BIOS_LO + 0x32;
const uint16_t _DPBASE = _BIOS_HI + 1; //DPB
//TRANS, 0000H
//0000H, 0000H
//DIRBF, DPBLK
//CHK00, ALL00
//...
const uint16_t  _DPBLK = _DPBASE + FDD_NUM*16;//DISK PARAMETER BLOCK, COMMON TO ALL DISKS
/*
DW  26    ;SECTORS PER TRACK
DB  3   ;BLOCK SHIFT FACTOR
DB  7   ;BLOCK MASK
DB  0   ;NULL MASK
DW  242   ;DISK SIZE-1
DW  63    ;DIRECTORY MAX
DB  192   ;ALLOC 0
DB  0   ;ALLOC 1
DW  16    ;CHECK SIZE
DW  2   ;TRACK OFFSET
 */

const uint16_t _TRACK = _DPBLK + 16;//TWO BYTES FOR EXPANSION
const uint16_t _SECTOR = _TRACK + 2;//TWO BYTES FOR EXPANSION
const uint16_t _DMAAD = _SECTOR + 2;//DIRECT MEMORY ADDRESS
const uint16_t _DISKNO = _DMAAD + 2;//DISK NUMBER 0-15

//SCRATCH RAM AREA FOR BDOS USE
const uint16_t _BEGDAT = _DISKNO + 1;//BEGINNING OF DATA AREA
const uint16_t _DIRBUF = _BEGDAT;//SCRATCH DIRECTORY AREA
const uint16_t _ALL00 = _DIRBUF + 128;//ALLOCATION VECTOR 0
const uint16_t _ALL01 = _ALL00 + 31;//ALLOCATION VECTOR 1
const uint16_t _ALL02 = _ALL01 + 31;//ALLOCATION VECTOR 2
const uint16_t _ALL03 = _ALL02 + 31;//ALLOCATION VECTOR 3
const uint16_t _CHK00 = _ALL03 + 31;//CHECK VECTOR 0
const uint16_t _CHK01 = _CHK00 + 16;//CHECK VECTOR 1
const uint16_t _CHK02 = _CHK01 + 16;//CHECK VECTOR 2
const uint16_t _CHK03 = _CHK02 + 16;//CHECK VECTOR 3
const uint16_t _ENDDAT = _CHK03 + 16;//END OF DATA AREA

//BIOS : 0x4a00 ... 0x4fff

//BOOT area
const uint16_t JMP_BOOT = 0; //warm start jmp 0x4a03
const uint16_t IOBYTE = 3;//INTEL I/O BYTE
const uint16_t CDISK = 4;//CURRENT DISK NUMBER 0=A,...
const uint16_t LOGINBYTE = 4;

const uint16_t JMP_BDOS = 5; //BDOS start jmp 0x3c06

#define SEC_BUF 0x80 //sector buffer

/*
0000 ... 00FF  -  RAM

0100   

V2.2:  b = memsize-20K(0x5000)
0000   - 00FF     System scratch area
0100   - 33FF+b   TPA (Transient Program Area) - COM file area
3400+b - 3BFF+b   CCP - Console COmmand Processor
3C00+b - 49FF+b   BDOS
4A00+b - 4FFF+b   CBIOS

System scratch area, "page zero":

00 - 02     Jump to BIOS warm start entry point
03          IOBYTE
04          Login byte: Login drive number, current user number
05 - 07     Jump to BDOS
08 - 37     Reserved; interrupt vectors & future use
38 - 3A     RST7 - used by DDT and SID programs, contains JMP into DDT/SID
3B - 3F     Reserved for interrupt vector
40 - 4F     Scratch area for CBIOS; unused by distribution version of CP/M
50 - 5B     Not used, reserved
5C - 7C     Default FCB (File Control Block) area
7D - 7F     Optional Default Random Record Position (V2.x)
80 - FF     Default DMA buffer area (128 bytes) for disk I/O
            Also filled with CCP commandline at the start of a program 

*/

