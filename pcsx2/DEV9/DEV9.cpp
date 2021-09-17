/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2020  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600

#ifdef _WIN32
//#include <winsock2.h>
#include <Winioctl.h>
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#endif

#include "ghc/filesystem.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#define EXTERN
#include "DEV9.h"
#undef EXTERN
#include "Config.h"
#include "AppConfig.h"
#include "smap.h"


#ifdef _WIN32
#pragma warning(disable : 4244)

HINSTANCE hInst = NULL;
#endif

//#define HDD_48BIT

#if defined(__i386__) && !defined(_WIN32)

static __inline__ unsigned long long GetTickCount(void)
{
	unsigned long long int x;
	__asm__ volatile("rdtsc"
					 : "=A"(x));
	return x;
}

#elif defined(__x86_64__) && !defined(_WIN32)

static __inline__ unsigned long long GetTickCount(void)
{
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc"
						 : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

#endif

// clang-format off
u8 eeprom[] = {
	//0x6D, 0x76, 0x63, 0x61, 0x31, 0x30, 0x08, 0x01,
	0x76, 0x6D, 0x61, 0x63, 0x30, 0x31, 0x07, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on

#ifdef _WIN32
HANDLE hEeprom;
HANDLE mapping;
#else
int hEeprom;
int mapping;
#endif

std::string s_strIniPath = "inis";
std::string s_strLogPath = "logs";

bool isRunning = false;

s32 DEV9init()
{
	Log::Console.debug("DEV9: DEV9init\n");

	memset(&dev9, 0, sizeof(dev9));
	dev9.ata = new ATA();
	Log::Console.debug("DEV9: DEV9init2\n");

	Log::Console.debug("DEV9: DEV9init3\n");

	FLASHinit();

#ifdef _WIN32
	hEeprom = CreateFile(
		L"eeprom.dat",
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_WRITE_THROUGH,
		NULL);

	if (hEeprom == INVALID_HANDLE_VALUE)
	{
		dev9.eeprom = (u16*)eeprom;
	}
	else
	{
		mapping = CreateFileMapping(hEeprom, NULL, PAGE_READWRITE, 0, 0, NULL);
		if (mapping == INVALID_HANDLE_VALUE)
		{
			CloseHandle(hEeprom);
			dev9.eeprom = (u16*)eeprom;
		}
		else
		{
			dev9.eeprom = (u16*)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);

			if (dev9.eeprom == NULL)
			{
				CloseHandle(mapping);
				CloseHandle(hEeprom);
				dev9.eeprom = (u16*)eeprom;
			}
		}
	}
#else
	hEeprom = open("eeprom.dat", O_RDWR, 0);

	if (-1 == hEeprom)
	{
		dev9.eeprom = (u16*)eeprom;
	}
	else
	{
		dev9.eeprom = (u16*)mmap(NULL, 64, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, hEeprom, 0);

		if (dev9.eeprom == NULL)
		{
			close(hEeprom);
			dev9.eeprom = (u16*)eeprom;
		}
	}
#endif

	int rxbi;

	for (rxbi = 0; rxbi < (SMAP_BD_SIZE / 8); rxbi++)
	{
		smap_bd_t* pbd = (smap_bd_t*)&dev9.dev9R[SMAP_BD_RX_BASE & 0xffff];
		pbd = &pbd[rxbi];

		pbd->ctrl_stat = SMAP_BD_RX_EMPTY;
		pbd->length = 0;
	}

	Log::Console.debug("DEV9: DEV9init ok\n");

	return 0;
}

void DEV9shutdown()
{
	Log::Console.debug("DEV9: DEV9shutdown\n");
	delete dev9.ata;
}

s32 DEV9open(void* pDsp)
{
	Log::Console.debug("DEV9: DEV9open\n");
	LoadConf();
#ifdef _WIN32
	//Convert to utf8
	char mbHdd[sizeof(config.Hdd)] = {0};
	WideCharToMultiByte(CP_UTF8, 0, config.Hdd, -1, mbHdd, sizeof(mbHdd) - 1, nullptr, nullptr);
	Log::Console.debug("DEV9: open r+: {:s}\n", mbHdd);
#else
	Log::Console.debug("DEV9: open r+: {:s}\n", config.Hdd);
#endif

	ghc::filesystem::path hddPath(config.Hdd);

	if (hddPath.empty())
		config.hddEnable = false;

	if (hddPath.is_relative())
	{
		ghc::filesystem::path path(GetSettingsFolder().ToString().wx_str());
		hddPath = path / hddPath;
	}

	if (config.hddEnable)
	{
		if (dev9.ata->Open(hddPath) != 0)
			config.hddEnable = false;
	}

	if (config.ethEnable)
		InitNet();

	isRunning = true;
	return 0;
}

void DEV9close()
{
	Log::Console.debug("DEV9: DEV9close\n");

	dev9.ata->Close();
	TermNet();
	isRunning = false;
}

int DEV9irqHandler(void)
{
	//dev9Ru16(SPD_R_INTR_STAT)|= dev9.irqcause;
	//Log::Console.debug("DEV9: DEV9irqHandler {:x}, {:x}\n", dev9.irqcause, dev9.irqmask);
	if (dev9.irqcause & dev9.irqmask)
		return 1;
	return 0;
}

void _DEV9irq(int cause, int cycles)
{
	//Log::Console.debug("DEV9: _DEV9irq {:x}, {:x}\n", cause, dev9.irqmask);

	dev9.irqcause |= cause;

	if (cycles < 1)
		dev9Irq(1);
	else
		dev9Irq(cycles);
}

//Fakes SPEED FIFO
void HDDWriteFIFO()
{
	if (dev9.ata->dmaReady && (dev9.if_ctrl & SPD_IF_ATA_DMAEN))
	{
		const int unread = (dev9.fifo_bytes_write - dev9.fifo_bytes_read);
		const int spaceSectors = (SPD_DBUF_AVAIL_MAX * 512 - unread) / 512;
		if (spaceSectors < 0)
		{
			Log::Console.error("DEV9: No Space on SPEED FIFO\n");
			pxAssert(false);
			abort();
		}

		const int readSectors = dev9.ata->nsectorLeft < spaceSectors ? dev9.ata->nsectorLeft : spaceSectors;
		dev9.fifo_bytes_write += readSectors * 512;
		dev9.ata->nsectorLeft -= readSectors;
	}
	//FIFOIntr();
}
void HDDReadFIFO()
{
	if (dev9.ata->dmaReady && (dev9.if_ctrl & SPD_IF_ATA_DMAEN))
	{
		const int writeSectors = (dev9.fifo_bytes_write - dev9.fifo_bytes_read) / 512;
		dev9.fifo_bytes_read += writeSectors * 512;
		dev9.ata->nsectorLeft -= writeSectors;
	}
	//FIFOIntr();
}
void IOPReadFIFO(int bytes)
{
	dev9.fifo_bytes_read += bytes;
	if (dev9.fifo_bytes_read > dev9.fifo_bytes_write)
		Log::Console.error("DEV9: UNDERFLOW BY IOP\n");
	//FIFOIntr();
}
void IOPWriteFIFO(int bytes)
{
	dev9.fifo_bytes_write += bytes;
	if (dev9.fifo_bytes_write - SPD_DBUF_AVAIL_MAX * 512 > dev9.fifo_bytes_read)
		Log::Console.error("DEV9: OVERFLOW BY IOP\n");
	//FIFOIntr();
}
void FIFOIntr()
{
	//FIFO Buffer Full/Empty
	const int unread = (dev9.fifo_bytes_write - dev9.fifo_bytes_read);

	if (unread == 0)
	{
		if ((dev9.irqcause & SPD_INTR_ATA_FIFO_EMPTY) == 0)
			_DEV9irq(SPD_INTR_ATA_FIFO_EMPTY, 1);
	}
	if (unread == SPD_DBUF_AVAIL_MAX * 512)
	{
		//Log_Error("FIFO Full");
		//INTR Full?
	}
}

u8 DEV9read8(u32 addr)
{
	if (!config.ethEnable & !config.hddEnable)
		return 0;

	u8 hard;
	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
		Log::Console.error("DEV9: ATA does not support 8bit reads {:x}\n", addr);
		return 0;
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		return smap_read8(addr);
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		return (u8)FLASHread32(addr, 1);
	}

	switch (addr)
	{
		case SPD_R_PIO_DATA:

			/*if(dev9.eeprom_dir!=1)
			{
				hard=0;
				break;
			}*/

			if (dev9.eeprom_state == EEPROM_TDATA)
			{
				if (dev9.eeprom_command == 2) //read
				{
					if (dev9.eeprom_bit == 0xFF)
						hard = 0;
					else
						hard = ((dev9.eeprom[dev9.eeprom_address] << dev9.eeprom_bit) & 0x8000) >> 11;
					dev9.eeprom_bit++;
					if (dev9.eeprom_bit == 16)
					{
						dev9.eeprom_address++;
						dev9.eeprom_bit = 0;
					}
				}
				else
					hard = 0;
			}
			else
				hard = 0;
			//Log::Console.debug("DEV9: SPD_R_PIO_DATA 8bit read {:x}\n", hard);
			return hard;

		case DEV9_R_REV:
			hard = 0x32; // expansion bay
			//Log::Console.debug("DEV9: DEV9_R_REV 8bit read {:x}\n", hard);
			return hard;

		default:
			hard = dev9Ru8(addr);
			Log::Console.error("DEV9: Unknown 8bit read at address {:x} value {:x}\n", addr, hard);
			return hard;
	}
}

u16 DEV9read16(u32 addr)
{
	if (!config.ethEnable & !config.hddEnable)
		return 0;

	u16 hard;
	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
		return dev9.ata->Read16(addr);
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		return smap_read16(addr);
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		return (u16)FLASHread32(addr, 2);
	}

	switch (addr)
	{
		case SPD_R_INTR_STAT:
			//Log::Console.debug("DEV9: SPD_R_INTR_STAT 16bit read {:x}\n", dev9.irqcause);
			return dev9.irqcause;

		case SPD_R_INTR_MASK:
			//Log::Console.debug("DEV9: SPD_R_INTR_MASK 16bit read {:x}\n", dev9.irqmask);
			return dev9.irqmask;

		case SPD_R_PIO_DATA:

			/*if(dev9.eeprom_dir!=1)
			{
				hard=0;
				break;
			}*/

			if (dev9.eeprom_state == EEPROM_TDATA)
			{
				if (dev9.eeprom_command == 2) //read
				{
					if (dev9.eeprom_bit == 0xFF)
						hard = 0;
					else
						hard = ((dev9.eeprom[dev9.eeprom_address] << dev9.eeprom_bit) & 0x8000) >> 11;
					dev9.eeprom_bit++;
					if (dev9.eeprom_bit == 16)
					{
						dev9.eeprom_address++;
						dev9.eeprom_bit = 0;
					}
				}
				else
					hard = 0;
			}
			else
				hard = 0;
			//Log::Console.debug("DEV9: SPD_R_PIO_DATA 16bit read {:x}\n", hard);
			return hard;

		case DEV9_R_REV:
			//hard = 0x0030; // expansion bay
			//Log::Console.debug("DEV9: DEV9_R_REV 16bit read {:x}\n", dev9.irqmask);
			hard = 0x0032;
			return hard;

		case SPD_R_REV_1:
			//Log::Console.debug("DEV9: SPD_R_REV_1 16bit read {:x}\n", 0);
			return 0;

		case SPD_R_REV_2:
			hard = 0x0011;
			//Log::Console.debug("DEV9: STD_R_REV_2 16bit read {:x}\n", hard);
			return hard;

		case SPD_R_REV_3:
			hard = 0;
			if (config.hddEnable)
				hard |= SPD_CAPS_ATA;
			if (config.ethEnable)
				hard |= SPD_CAPS_SMAP;
			hard |= SPD_CAPS_FLASH;
			//Log::Console.debug("DEV9: SPD_R_REV_3 16bit read {:x}\n", hard);
			return hard;

		case SPD_R_0e:
			hard = 0x0002; //Have HDD inserted
			Log::Console.debug("DEV9: SPD_R_0e 16bit read {:x}\n", hard);
			return hard;
		case SPD_R_XFR_CTRL:
			Log::Console.debug("DEV9: SPD_R_XFR_CTRL 16bit read {:x}\n", dev9.xfr_ctrl);
			return dev9.xfr_ctrl;
		case SPD_R_DBUF_STAT:
		{
			hard = 0;
			if (dev9.if_ctrl & SPD_IF_READ) //Semi async
			{
				HDDWriteFIFO(); //Yes this is not a typo
			}
			else
			{
				HDDReadFIFO();
			}
			FIFOIntr();

			const u8 count = (u8)((dev9.fifo_bytes_write - dev9.fifo_bytes_read) / 512);
			if (dev9.xfr_ctrl & SPD_XFR_WRITE) //or ifRead?
			{
				hard = (u8)(SPD_DBUF_AVAIL_MAX - count);
				hard |= (count == 0) ? SPD_DBUF_STAT_1 : (u16)0;
				hard |= (count > 0) ? SPD_DBUF_STAT_2 : (u16)0;
			}
			else
			{
				hard = count;
				hard |= (count < SPD_DBUF_AVAIL_MAX) ? SPD_DBUF_STAT_1 : (u16)0;
				hard |= (count == 0) ? SPD_DBUF_STAT_2 : (u16)0;
				//If overflow (HDD->SPEED), set both SPD_DBUF_STAT_2 & SPD_DBUF_STAT_FULL
				//and overflow INTR set
			}

			if (count == SPD_DBUF_AVAIL_MAX)
			{
				hard |= SPD_DBUF_STAT_FULL;
			}

			//Log::Console.debug("DEV9: SPD_R_DBUF_STAT 16bit read {:x}\n", hard);
			return hard;
		}
		case SPD_R_IF_CTRL:
			//Log::Console.debug("DEV9: SPD_R_IF_CTRL 16bit read {:x}\n", dev9.if_ctrl);
			return dev9.if_ctrl;
		default:
			hard = dev9Ru16(addr);
			Log::Console.error("DEV9: Unknown 16bit read at address {:x} value {:x}\n", addr, hard);
			return hard;
	}
}

u32 DEV9read32(u32 addr)
{
	if (!config.ethEnable & !config.hddEnable)
		return 0;

	u32 hard;
	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
		Log::Console.error("DEV9: ATA does not support 32bit reads {:x}\n", addr);
		return 0;
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		return smap_read32(addr);
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		return (u32)FLASHread32(addr, 4);
	}

	hard = dev9Ru32(addr);
	Log::Console.error("DEV9: Unknown 32bit read at address {:x} value {:x}\n", addr, hard);
	return hard;
}

void DEV9write8(u32 addr, u8 value)
{
	if (!config.ethEnable & !config.hddEnable)
		return;

	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
#ifdef ENABLE_ATA
		ata_write<1>(addr, value);
#endif
		return;
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		smap_write8(addr, value);
		return;
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		FLASHwrite32(addr, (u32)value, 1);
		return;
	}

	switch (addr)
	{
		case 0x10000020:
			Log::Console.error("DEV9: SPD_R_INTR_CAUSE, WTFH ?\n");
			dev9.irqcause = 0xff;
			break;
		case SPD_R_INTR_STAT:
			Log::Console.error("DEV9: SPD_R_INTR_STAT,  WTFH ?\n");
			dev9.irqcause = value;
			return;
		case SPD_R_INTR_MASK:
			Log::Console.error("DEV9: SPD_R_INTR_MASK8, WTFH ?\n");
			break;

		case SPD_R_PIO_DIR:
			//Log::Console.debug("DEV9: SPD_R_PIO_DIR 8bit write {:x}\n", value);

			if ((value & 0xc0) != 0xc0)
				return;

			if ((value & 0x30) == 0x20)
			{
				dev9.eeprom_state = 0;
			}
			dev9.eeprom_dir = (value >> 4) & 3;

			return;

		case SPD_R_PIO_DATA:
			//Log::Console.debug("DEV9: SPD_R_PIO_DATA 8bit write {:x}\n", value);

			if ((value & 0xc0) != 0xc0)
				return;

			switch (dev9.eeprom_state)
			{
				case EEPROM_READY:
					dev9.eeprom_command = 0;
					dev9.eeprom_state++;
					break;
				case EEPROM_OPCD0:
					dev9.eeprom_command = (value >> 4) & 2;
					dev9.eeprom_state++;
					dev9.eeprom_bit = 0xFF;
					break;
				case EEPROM_OPCD1:
					dev9.eeprom_command |= (value >> 5) & 1;
					dev9.eeprom_state++;
					break;
				case EEPROM_ADDR0:
				case EEPROM_ADDR1:
				case EEPROM_ADDR2:
				case EEPROM_ADDR3:
				case EEPROM_ADDR4:
				case EEPROM_ADDR5:
					dev9.eeprom_address =
						(dev9.eeprom_address & (63 ^ (1 << (dev9.eeprom_state - EEPROM_ADDR0)))) |
						((value >> (dev9.eeprom_state - EEPROM_ADDR0)) & (0x20 >> (dev9.eeprom_state - EEPROM_ADDR0)));
					dev9.eeprom_state++;
					break;
				case EEPROM_TDATA:
				{
					if (dev9.eeprom_command == 1) //write
					{
						dev9.eeprom[dev9.eeprom_address] =
							(dev9.eeprom[dev9.eeprom_address] & (63 ^ (1 << dev9.eeprom_bit))) |
							((value >> dev9.eeprom_bit) & (0x8000 >> dev9.eeprom_bit));
						dev9.eeprom_bit++;
						if (dev9.eeprom_bit == 16)
						{
							dev9.eeprom_address++;
							dev9.eeprom_bit = 0;
						}
					}
				}
				break;
				default:
					Log::Console.error("DEV9: Unknown EEPROM COMMAND\n");
					break;
			}
			return;
		default:
			dev9Ru8(addr) = value;
			Log::Console.error("DEV9: Unknown 8bit write at address {:x} value {:x}\n", addr, value);
			return;
	}
}

void DEV9write16(u32 addr, u16 value)
{
	if (!config.ethEnable & !config.hddEnable)
		return;

	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
		dev9.ata->Write16(addr, value);
		return;
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		smap_write16(addr, value);
		return;
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		FLASHwrite32(addr, (u32)value, 2);
		return;
	}

	switch (addr)
	{
		case SPD_R_INTR_MASK:
			//Log::Console.debug("DEV9: SPD_R_INTR_MASK 16bit write {:x}	, checking for masked/unmasked interrupts\n", value);
			if ((dev9.irqmask != value) && ((dev9.irqmask | value) & dev9.irqcause))
			{
				//Log::Console.debug("DEV9: SPD_R_INTR_MASK16 firing unmasked interrupts\n");
				dev9Irq(1);
			}
			dev9.irqmask = value;
			break;

		case SPD_R_PIO_DIR:
			//Log::Console.debug("DEV9: SPD_R_PIO_DIR 16bit write {:x}\n", value);

			if ((value & 0xc0) != 0xc0)
				return;

			if ((value & 0x30) == 0x20)
			{
				dev9.eeprom_state = 0;
			}
			dev9.eeprom_dir = (value >> 4) & 3;

			return;

		case SPD_R_PIO_DATA:
			//Log::Console.debug("DEV9: SPD_R_PIO_DATA 16bit write {:x}\n", value);

			if ((value & 0xc0) != 0xc0)
				return;

			switch (dev9.eeprom_state)
			{
				case EEPROM_READY:
					dev9.eeprom_command = 0;
					dev9.eeprom_state++;
					break;
				case EEPROM_OPCD0:
					dev9.eeprom_command = (value >> 4) & 2;
					dev9.eeprom_state++;
					dev9.eeprom_bit = 0xFF;
					break;
				case EEPROM_OPCD1:
					dev9.eeprom_command |= (value >> 5) & 1;
					dev9.eeprom_state++;
					break;
				case EEPROM_ADDR0:
				case EEPROM_ADDR1:
				case EEPROM_ADDR2:
				case EEPROM_ADDR3:
				case EEPROM_ADDR4:
				case EEPROM_ADDR5:
					dev9.eeprom_address =
						(dev9.eeprom_address & (63 ^ (1 << (dev9.eeprom_state - EEPROM_ADDR0)))) |
						((value >> (dev9.eeprom_state - EEPROM_ADDR0)) & (0x20 >> (dev9.eeprom_state - EEPROM_ADDR0)));
					dev9.eeprom_state++;
					break;
				case EEPROM_TDATA:
				{
					if (dev9.eeprom_command == 1) //write
					{
						dev9.eeprom[dev9.eeprom_address] =
							(dev9.eeprom[dev9.eeprom_address] & (63 ^ (1 << dev9.eeprom_bit))) |
							((value >> dev9.eeprom_bit) & (0x8000 >> dev9.eeprom_bit));
						dev9.eeprom_bit++;
						if (dev9.eeprom_bit == 16)
						{
							dev9.eeprom_address++;
							dev9.eeprom_bit = 0;
						}
					}
				}
				break;
				default:
					Log::Console.error("DEV9: Unknown EEPROM COMMAND\n");
					break;
			}
			return;

		case SPD_R_DMA_CTRL:
			//Log::Console.debug("DEV9: SPD_R_IF_CTRL 16bit write {:x}\n", value);
			dev9.dma_ctrl = value;

			//if (value & SPD_DMA_TO_SMAP)
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL DMA For SMAP\n");
			//else
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL DMA For ATA\n");

			//if ((value & SPD_DMA_FASTEST) != 0)
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL Fastest DMA Mode\n");
			//else
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL Slower DMA Mode\n");

			//if ((value & SPD_DMA_WIDE) != 0)
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL Wide(32bit) DMA Mode Set\n");
			//else
			//	Log::Console.debug("DEV9: SPD_R_DMA_CTRL 16bit DMA Mode\n");

			if ((value & SPD_DMA_PAUSE) != 0)
				Log::Console.error("DEV9: SPD_R_DMA_CTRL Pause DMA Not Implemented\n");

			if ((value & 0b1111111111101000) != 0)
				Log::Console.error("DEV9: SPD_R_DMA_CTRL Unknown value written {:x}\n", value);

			break;
		case SPD_R_XFR_CTRL:
			//Log::Console.debug("DEV9: SPD_R_XFR_CTRL 16bit write {:x}\n", value);
			dev9.xfr_ctrl = value;

			//if (value & SPD_XFR_WRITE)
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL Set Write\n");
			//else
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL Set Read\n");

			//if ((value & (1 << 1)) != 0)
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL Unknown Bit 1\n");

			//if ((value & (1 << 2)) != 0)
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL Unknown Bit 2\n");

			//if (value & SPD_XFR_DMAEN)
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL For DMA Enabled\n");
			//else
			//	Log::Console.debug("DEV9: SPD_R_XFR_CTRL For DMA Disabled\n");

			if ((value & 0b1111111101111000) != 0)
				Log::Console.error("DEV9: SPD_R_XFR_CTRL Unknown value written {:x}\n", value);

			break;
		case SPD_R_DBUF_STAT:
			//Log::Console.debug("DEV9: SPD_R_DBUF_STAT 16bit write {:x}\n", value);

			if ((value & SPD_DBUF_RESET_FIFO) != 0)
			{
				//Log::Console.debug("DEV9: SPD_R_DBUF_STAT Reset FIFO\n");
				dev9.fifo_bytes_write = 0;
				dev9.fifo_bytes_read = 0;
				dev9.xfr_ctrl &= ~SPD_XFR_WRITE; //?
				dev9.if_ctrl |= SPD_IF_READ; //?

				FIFOIntr();
			}

			if (value != 3)
				Log::Console.error("DEV9: SPD_R_DBUF_STAT 16bit write {:x} Which != 3!!!\n", value);
			break;

		case SPD_R_IF_CTRL:
			//Log::Console.debug("DEV9: SPD_R_IF_CTRL 16bit write {:x}\n", value);
			dev9.if_ctrl = value;

			//if (value & SPD_IF_UDMA)
			//	Log::Console.debug("DEV9: IF_CTRL UDMA Enabled\n");
			//else
			//	Log::Console.debug("DEV9: IF_CTRL UDMA Disabled\n");
			//if (value & SPD_IF_READ)
			//	Log::Console.debug("DEV9: IF_CTRL DMA Is ATA Read\n");
			//else
			//	Log::Console.debug("DEV9: IF_CTRL DMA Is ATA Write\n");

			if (value & SPD_IF_ATA_DMAEN)
			{
				//Log::Console.debug("DEV9: IF_CTRL ATA DMA Enabled\n");
				if (value & SPD_IF_READ) //Semi async
				{
					HDDWriteFIFO(); //Yes this is not a typo
				}
				else
				{
					HDDReadFIFO();
				}
				FIFOIntr();
			}
			//else
			//	Log::Console.debug("DEV9: IF_CTRL ATA DMA Disabled\n");

			if (value & (1 << 3))
				Log::Console.debug("DEV9: IF_CTRL Unknown Bit 3 Set\n");

			if (value & (1 << 4))
				Log::Console.error("DEV9: IF_CTRL Unknown Bit 4 Set\n");
			if (value & (1 << 5))
				Log::Console.error("DEV9: IF_CTRL Unknown Bit 5 Set\n");

			if ((value & SPD_IF_HDD_RESET) == 0) //Maybe?
			{
				//Log::Console.debug("DEV9: IF_CTRL HDD Hard Reset\n");
				dev9.ata->ATA_HardReset();
			}
			if ((value & SPD_IF_ATA_RESET) != 0)
			{
				Log::Console.debug("DEV9: IF_CTRL ATA Reset\n");
				//0x62        0x0020
				dev9.if_ctrl = 0x001A;
				//0x66        0x0001
				dev9.pio_mode = 0x24;
				dev9.mdma_mode = 0x45;
				dev9.udma_mode = 0x83;
				//0x76    0x4ABA (And consequently 0x78 = 0x4ABA.)
			}

			if ((value & 0xFF00) > 0)
				Log::Console.error("DEV9: IF_CTRL Unknown Bit(s) {:x}\n", (value & 0xFF00));

			break;
		case SPD_R_PIO_MODE: //ATA only? or includes EEPROM?
			//Log::Console.debug("DEV9: SPD_R_PIO_MODE 16bit write {:x}\n", value);
			dev9.pio_mode = value;

			switch (value)
			{
				case 0x92:
					//Log::Console.debug("DEV9: SPD_R_PIO_MODE 0\n");
					break;
				case 0x72:
					//Log::Console.debug("DEV9: SPD_R_PIO_MODE 1\n");
					break;
				case 0x32:
					//Log::Console.debug("DEV9: SPD_R_PIO_MODE 2\n");
					break;
				case 0x24:
					//Log::Console.debug("DEV9: SPD_R_PIO_MODE 3\n");
					break;
				case 0x23:
					//Log::Console.debug("DEV9: SPD_R_PIO_MODE 4\n");
					break;

				default:
					Log::Console.error("DEV9: SPD_R_PIO_MODE UNKNOWN MODE {:x}\n", value);
					break;
			}
			break;
		case SPD_R_MDMA_MODE: //ATA only? or includes EEPROM?
			Log::Console.debug("DEV9: SPD_R_MDMA_MODE 16bit write {:x}\n", value);
			dev9.mdma_mode = value;

			switch (value)
			{
				case 0xFF:
					Log::Console.debug("DEV9: SPD_R_MDMA_MODE 0\n");
					break;
				case 0x45:
					Log::Console.debug("DEV9: SPD_R_MDMA_MODE 1\n");
					break;
				case 0x24:
					Log::Console.debug("DEV9: SPD_R_MDMA_MODE 2\n");
					break;
				default:
					Log::Console.error("DEV9: SPD_R_MDMA_MODE UNKNOWN MODE {:x}\n", value);
					break;
			}

			break;
		case SPD_R_UDMA_MODE: //ATA only?
			Log::Console.debug("DEV9: SPD_R_UDMA_MODE 16bit write {:x}\n", value);
			dev9.udma_mode = value;

			switch (value)
			{
				case 0xa7:
					Log::Console.debug("DEV9: SPD_R_UDMA_MODE 0\n");
					break;
				case 0x85:
					Log::Console.debug("DEV9: SPD_R_UDMA_MODE 1\n");
					break;
				case 0x63:
					Log::Console.debug("DEV9: SPD_R_UDMA_MODE 2\n");
					break;
				case 0x62:
					Log::Console.debug("DEV9: SPD_R_UDMA_MODE 3\n");
					break;
				case 0x61:
					Log::Console.debug("DEV9: SPD_R_UDMA_MODE 4\n");
					break;
				default:
					Log::Console.error("DEV9: SPD_R_UDMA_MODE UNKNOWN MODE {:x}\n", value);
					break;
			}
			break;

		default:
			dev9Ru16(addr) = value;
			Log::Console.error("DEV9: *Unknown 16bit write at address {:x} value {:x}\n", addr, value);
			return;
	}
}

void DEV9write32(u32 addr, u32 value)
{
	if (!config.ethEnable & !config.hddEnable)
		return;

	if (addr >= ATA_DEV9_HDD_BASE && addr < ATA_DEV9_HDD_END)
	{
#ifdef ENABLE_ATA
		ata_write<4>(addr, value);
#endif
		return;
	}
	if (addr >= SMAP_REGBASE && addr < FLASH_REGBASE)
	{
		//smap
		smap_write32(addr, value);
		return;
	}
	if ((addr >= FLASH_REGBASE) && (addr < (FLASH_REGBASE + FLASH_REGSIZE)))
	{
		FLASHwrite32(addr, (u32)value, 4);
		return;
	}

	switch (addr)
	{
		case SPD_R_INTR_MASK:
			Log::Console.error("DEV9: SPD_R_INTR_MASK, WTFH ?\n");
			break;
		default:
			dev9Ru32(addr) = value;
			Log::Console.error("DEV9: Unknown 32bit write at address {:x} write {:x}\n", addr, value);
			return;
	}
}

void DEV9readDMA8Mem(u32* pMem, int size)
{
	if (!config.ethEnable & !config.hddEnable)
		return;

	size >>= 1;

	Log::Console.debug("DEV9: *DEV9readDMA8Mem: size {:x}\n", size);

	if (dev9.dma_ctrl & SPD_DMA_TO_SMAP)
		smap_readDMA8Mem(pMem, size);
	else
	{
		if (dev9.xfr_ctrl & SPD_XFR_DMAEN &&
			!(dev9.xfr_ctrl & SPD_XFR_WRITE))
		{
			HDDWriteFIFO();
			IOPReadFIFO(size);
			dev9.ata->ATAreadDMA8Mem((u8*)pMem, size);
			FIFOIntr();
		}
	}

	//TODO, track if read was successful
}

void DEV9writeDMA8Mem(u32* pMem, int size)
{
	if (!config.ethEnable & !config.hddEnable)
		return;

	size >>= 1;

	Log::Console.debug("DEV9: *DEV9writeDMA8Mem: size {:x}\n", size);

	if (dev9.dma_ctrl & SPD_DMA_TO_SMAP)
		smap_writeDMA8Mem(pMem, size);
	else
	{
		if (dev9.xfr_ctrl & SPD_XFR_DMAEN &&
			dev9.xfr_ctrl & SPD_XFR_WRITE)
		{
			IOPWriteFIFO(size);
			HDDReadFIFO();
			dev9.ata->ATAwriteDMA8Mem((u8*)pMem, size);
			FIFOIntr();
		}
	}

	//TODO, track if write was successful
}

void DEV9async(u32 cycles)
{
	smap_async(cycles);
	dev9.ata->Async(cycles);
}

// extended funcs

void DEV9setSettingsDir(const char* dir)
{
	// Grab the ini directory.
	// TODO: Use
	s_strIniPath = (dir == NULL) ? "inis" : dir;
}

void DEV9setLogDir(const char* dir)
{
	// Get the path to the log directory.
	s_strLogPath = (dir == NULL) ? "logs" : dir;
}

void ApplyConfigIfRunning(Config oldConfig)
{
	if (!isRunning)
		return;

	//Eth
	ReconfigureLiveNet(&oldConfig);

	//Hdd
	//Hdd Validate Path
	ghc::filesystem::path hddPath(config.Hdd);

	if (hddPath.empty())
		config.hddEnable = false;

	if (hddPath.is_relative())
	{
		//GHC uses UTF8 on all platforms
		ghc::filesystem::path path(GetSettingsFolder().ToString().wx_str());
		hddPath = path / hddPath;
	}

	//Hdd Compare with old config
	if (config.hddEnable)
	{
		if (oldConfig.hddEnable)
		{
			//ATA::Open/Close dosn't set any regs
			//So we can close/open to apply settings
#ifdef _WIN32
			if (wcscmp(config.Hdd, oldConfig.Hdd))
#else
			if (strcmp(config.Hdd, oldConfig.Hdd))
#endif
			{
				dev9.ata->Close();
				if (dev9.ata->Open(hddPath) != 0)
					config.hddEnable = false;
			}

			if (config.HddSize != oldConfig.HddSize)
			{
				dev9.ata->Close();
				if (dev9.ata->Open(hddPath) != 0)
					config.hddEnable = false;
			}
		}
	}
	else if (oldConfig.hddEnable)
		dev9.ata->Close();
}
