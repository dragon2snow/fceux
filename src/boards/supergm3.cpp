/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2023 dragon2snow loong2snow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * SL12 Protected 3-in-1 mapper hardware (VRC2, MMC3, MMC1)
 * the same as 603-5052 board (TODO: add reading registers, merge)
 * SL1632 2-in-1 protected board, similar to SL12 (TODO: find difference)
 *
 * Known PCB:
 *
 * Garou Densetsu Special (G0904.PCB, Huang-1, GAL dip: W conf.)
 * Kart Fighter (008, Huang-1, GAL dip: W conf.)
 * Somari (008, C5052-13, GAL dip: P conf., GK2-P/GK2-V maskroms)
 * Somari (008, Huang-1, GAL dip: W conf., GK1-P/GK1-V maskroms)
 * AV Mei Shao Nv Zhan Shi (aka AV Pretty Girl Fighting) (SL-12 PCB, Hunag-1, GAL dip: unk conf. SL-11A/SL-11B maskroms)
 * Samurai Spirits (Full version) (Huang-1, GAL dip: unk conf. GS-2A/GS-4A maskroms)
 * Contra Fighter (603-5052 PCB, C5052-3, GAL dip: unk conf. SC603-A/SCB603-B maskroms)
 *
 */

#include "mapinc.h"
#include <windows.h>

#define MAPPER_UNROM 0
#define MAPPER_AMROM 1
#define MAPPER_MMC1  2
#define MAPPER_MMC3  3

#define mapper  (mode[0] >>4 &3)
#define prgOR   (mode[1] <<4 &0x1F0)
#define chrOR   (mode[0] <<7 &0x780)
#define rom6  !!(mode[1] &0x20)
#define rom8   !(mode[1] &0x80)
#define chrram !(mode[1] &0x40)


static uint8_t		audioEnable;
static uint8_t		ram[4096];
static uint8_t		mode[2];

static uint8 mmc3_regs[10], mmc3_ctrl, mmc3_mirr;
static uint8 IRQCount, IRQLatch, IRQa;
static uint8 IRQReload;
static uint8 mmc1_regs[4], mmc1_buffer, mmc1_shift;

static writefunc defapuwrite[0x1000];
static readfunc defapuread[0x1000];

static uint8 *WRAM;
static uint32 WRAMSIZE;
static uint8 *CHRRAM;
static uint32 CHRRAMSIZE;

static uint8 dipValue = 0;

struct  Latch
{
	uint16 address;
	uint8 data;
};

static Latch latch;

static SFORMAT StateRegs[] =
{
	{ 0 }
};

static DECLFR(readRAM) {
	return ram[A];
}

static DECLFW(writeRAM) {
	ram[A] = V;
}





static DECLFR(readCoinDIP) {
	if ((A & 0xF0F) == 0xF0F) {
		return (GetKeyState('C') ? 0x80 : 0x00) | dipValue;
	}
	else
		return defapuread[A & 0xFFF](A);
}



static void syncWRAM(int bank){
	switch (mapper) {
	case MAPPER_UNROM:
		break;
	case MAPPER_AMROM:
		break;
	case MAPPER_MMC1:
	{
		//if (WRAMSIZE > 0x2000) {
		//	if (WRAMSIZE > 0x4000)
		//		setprg8r(0x10, 0x6000, (bank) & 3);
		//	else
		//		setprg8r(0x10, 0x6000, (bank) & 1);
		//}
		setprg8r(0x10, 0x6000, bank);
	}
		break;
	case MAPPER_MMC3:
		setprg8r(0x10, 0x6000, 0);
		break;
	}
}
static void syncPRG(uint16 AND, uint16 OR) {
	switch (mapper) {
		case MAPPER_UNROM:
			break;
		case MAPPER_AMROM:
			break;
		case MAPPER_MMC1:
		{
			uint8 offs_16banks = mmc1_regs[1] & 0x10;
			uint8 prg_reg = mmc1_regs[3] & 0xF; //homebrewers arent allowed to use more banks on MMC1. use another mapper.

			switch (mmc1_regs[0] & 0xC) {
			case 0xC:
				setprg16(0x8000, ((prg_reg + offs_16banks)& AND) | OR);
				setprg16(0xC000, ((0xF + offs_16banks)& AND) | OR);
				break;
			case 0x8:
				setprg16(0xC000, ((prg_reg + offs_16banks)& AND) | OR);
				setprg16(0x8000, ((offs_16banks)& AND) | OR);
				break;
			case 0x0:
			case 0x4:
				setprg16(0x8000, (((prg_reg & ~1) + offs_16banks)& AND) | OR);
				setprg16(0xc000, (((prg_reg & ~1) + offs_16banks + 1)& AND) | OR);
				break;
			}
		}
		break;
		case MAPPER_MMC3:
		{
			uint32 swap = (mmc3_ctrl >> 5) & 2;
			setprg8(0x8000, ((mmc3_regs[6 + swap]) & AND) | OR);
			setprg8(0xA000, ((mmc3_regs[7]) & AND) | OR);
			setprg8(0xC000, ((mmc3_regs[6 + (swap ^ 2)]) & AND) | OR);
			setprg8(0xE000, ((mmc3_regs[9]) & AND) | OR);
		}

		break;
	}
}
static void syncCHR(uint16 AND, uint16 OR)
{
	switch (mapper) {
	case MAPPER_UNROM:
		break;
	case MAPPER_AMROM:
		break;
	case MAPPER_MMC1:
	{
		if (mmc1_regs[0] & 0x10) {
			setchr4(0x0000, (mmc1_regs[1] & AND) | OR);
			setchr4(0x1000, (mmc1_regs[2] & AND) | OR);
		}
		else
		{
			uint8 newAND = AND >> 1;
			uint8 newOR = OR >> 1;
			setchr8(((mmc1_regs[1] >> 1) & newAND)|newOR);
		}
			
	}
		break;
	case MAPPER_MMC3:
	{
		uint32 swap = (mmc3_ctrl & 0x80) << 5;
		setchr1(0x0000 ^ swap, (((mmc3_regs[0]) & 0xFE) & AND) | OR);
		setchr1(0x0400 ^ swap, ((mmc3_regs[0] | 1) & AND) | OR);
		setchr1(0x0800 ^ swap, (((mmc3_regs[1]) & 0xFE) & AND) | OR);
		setchr1(0x0c00 ^ swap, ((mmc3_regs[1] | 1) & AND) | OR);
		setchr1(0x1000 ^ swap, (mmc3_regs[2] & AND) | OR);
		setchr1(0x1400 ^ swap, (mmc3_regs[3] & AND) | OR);
		setchr1(0x1800 ^ swap, (mmc3_regs[4] & AND) | OR);
		setchr1(0x1c00 ^ swap, (mmc3_regs[5] & AND) | OR);
	}

		break;
	}
}
static void syncMirror() {
	switch (mapper) {
	case MAPPER_UNROM:
		break;
	case MAPPER_AMROM:
		break;
	case MAPPER_MMC1:
		{
			switch (mmc1_regs[0] & 3) {
			case 0: setmirror(MI_0); break;
			case 1: setmirror(MI_1); break;
			case 2: setmirror(MI_V); break;
			case 3: setmirror(MI_H); break;
			}
		}
		break;
	case MAPPER_MMC3:

		setmirror((mmc3_mirr & 1) ^ 1);

		break;
	}
}

static void Sync(void) {
	switch (mapper) {
	case MAPPER_UNROM:
		setprg16(0x8000, prgOR >> 1 | latch.data & 7);
		setprg16(0xC000, prgOR >> 1 | 7);
		setmirror(MI_V);
		break;
	case MAPPER_AMROM:
		setprg32(0x8000, prgOR >> 2 | latch.data & 7);
		if (latch.data & 0x10)
			setmirror(MI_1);
		else
			setmirror(MI_0);
		break;
	case MAPPER_MMC1:
		syncWRAM(0);
		syncPRG(0x07, prgOR >> 1);
		syncCHR(0x1F, chrOR >> 2);
		syncMirror();
		break;
	case MAPPER_MMC3:

		syncWRAM(0);
		syncPRG(mode[1] & 0x20 ? 0x0F : 0x1F, prgOR);
		syncCHR(mode[0] & 0x40 ? 0x7F : 0xFF, chrOR);
		syncMirror();
		break;
	}

	setprg4(0x5000, 0x380 + 0x5);
	if (rom6)   
		setprg8(0x6000, (0x380 + 0x6) >> 1);
	if (rom8)   
		setprg32(0x8000, (0x380 + 0x8) >> 3);
	if (chrram) setchr8r(0x10, 0x0);
}

static DECLFW(SuperGM3Mmc3Write) {
	switch (A & 0xE001) {
		case 0x8000: {
			uint8 old_ctrl = mmc3_ctrl;
			mmc3_ctrl = V;
			if ((old_ctrl & 0x40) != (mmc3_ctrl & 0x40))
				Sync();
			if ((old_ctrl & 0x80) != (mmc3_ctrl & 0x80))
				Sync();
			break;
		}
		case 0x8001:
			mmc3_regs[mmc3_ctrl & 7] = V;
			if ((mmc3_ctrl & 7) < 6)
				Sync();
			else
				Sync();
			break;
		case 0xA000:
			mmc3_mirr = V;
			Sync();
			break;
		case 0xC000:
			IRQLatch = V;
			break;
		case 0xC001:
			IRQReload = 1;
			break;
		case 0xE000:
			X6502_IRQEnd(FCEU_IQEXT);
			IRQa = 0;
			break;
		case 0xE001:
			IRQa = 1;
			break;
	}
}
static uint64 lreset;

static DECLFW(SuperGM3Mmc1Write) {


	if ((timestampbase + timestamp) < (lreset + 2))
		return;

	if (V & 0x80) {
		mmc1_regs[0] |= 0xc;
		mmc1_buffer = mmc1_shift = 0;
		//Sync();
		syncPRG(0x07, prgOR >> 1);
		//syncCHR(0x1F, chrOR >> 2);
		//syncMirror();
		lreset = timestampbase + timestamp;
	}
	else {
		uint8 n = (A >> 13) - 4;
		mmc1_buffer |= (V & 1) << (mmc1_shift++);
		if (mmc1_shift == 5) {
			mmc1_regs[n] = mmc1_buffer;
			mmc1_buffer = mmc1_shift = 0;
			switch (n) {
			case 0: syncMirror();//Sync();
			case 2: syncCHR(0x1F, chrOR >> 2);//Sync();
			case 3:
			case 1: syncPRG(0x07, prgOR >> 1);//Sync();
			}
		}
	}
}

static DECLFW(SuperGM3LatchWrite) {
	latch.address = A;
	latch.data = V;
	Sync();
}



static void SuperGM3HBIRQ(void) {
	if ((mapper) == MAPPER_MMC3) {
		int32 count = IRQCount;
		if (!count || IRQReload) {
			IRQCount = IRQLatch;
			IRQReload = 0;
		} else
			IRQCount--;
		if (!IRQCount) {
			if (IRQa)
				X6502_IRQBegin(FCEU_IQEXT);
		}
	}
}

static void	applyMode(void) {
	switch (mapper) {
	case MAPPER_UNROM:
	case MAPPER_AMROM:
		SetWriteHandler(0x8000, 0xFFFF, SuperGM3LatchWrite);
		break;
	case MAPPER_MMC1:
		SetWriteHandler(0x8000, 0xFFFF, SuperGM3Mmc1Write);
		break;
	case MAPPER_MMC3:
		SetWriteHandler(0x8000, 0xFFFF, SuperGM3Mmc3Write);
		break;
	}
}

static DECLFW(writeASIC) {
	if (A & 0x10)
		audioEnable = V;
	else {
		mode[A & 1] = V;
		applyMode();
		Sync();
	}
}

static void StateRestore(int version) {
	Sync();
}
static void SuperGM3Reset(void) {
	for (auto& c : mode) c = 0;

	for (auto& c : ram) c = 0;

	applyMode();

	latch.address = latch.data = 0;

	mmc3_regs[0] = 0;
	mmc3_regs[1] = 2;
	mmc3_regs[2] = 4;
	mmc3_regs[3] = 5;
	mmc3_regs[4] = 6;
	mmc3_regs[5] = 7;
	mmc3_regs[6] = ~3;
	mmc3_regs[7] = ~2;
	mmc3_regs[8] = ~1;
	mmc3_regs[9] = ~0;
	mmc3_ctrl = mmc3_mirr = IRQCount = IRQLatch = IRQa = 0;
	mmc1_regs[0] = 0xc;
	mmc1_regs[1] = 0;
	mmc1_regs[2] = 0;
	mmc1_regs[3] = 0;
	mmc1_buffer = 0;
	mmc1_shift = 0;

	dipValue++;
}
static void SuperGM3Power(void) {


	for (auto& c : mode) c = 0;
	
	for (auto& c : ram) c = 0;

	applyMode();

	latch.address = latch.data = 0;

	mmc3_regs[0] = 0;
	mmc3_regs[1] = 2;
	mmc3_regs[2] = 4;
	mmc3_regs[3] = 5;
	mmc3_regs[4] = 6;
	mmc3_regs[5] = 7;
	mmc3_regs[6] = ~3;
	mmc3_regs[7] = ~2;
	mmc3_regs[8] = ~1;
	mmc3_regs[9] = ~0;
	mmc3_ctrl = mmc3_mirr = IRQCount = IRQLatch = IRQa = 0;
	mmc1_regs[0] = 0xc;
	mmc1_regs[1] = 0;
	mmc1_regs[2] = 0;
	mmc1_regs[3] = 0;
	mmc1_buffer = 0;
	mmc1_shift = 0;

	dipValue = 0;

	for (int i = 0; i < 0x1000; i++) {
		defapuread[i] = GetReadHandler(0x4000 | i);
		defapuwrite[i] = GetWriteHandler(0x4000 | i);
	}

	SetReadHandler(0x4000, 0x4FFF, readCoinDIP);
	SetWriteHandler(0x5000, 0x5fff, writeASIC);

	SetReadHandler(0x5000, 0x5FFF, CartBR);

	SetReadHandler(0x0800, 0x0FFF, readRAM);
	SetWriteHandler(0x0800, 0x0FFF, writeRAM);

	SetReadHandler(0x6000, 0x7FFF, CartBR);
	SetWriteHandler(0x6000, 0x7FFF, CartBW);

	SetReadHandler(0x8000, 0xFFFF, CartBR);

	SetWriteHandler(0x8000, 0xFFFF, SuperGM3LatchWrite);
	

	Sync();
}

void SuperGM3_Init(CartInfo *info) {

	info->Power = SuperGM3Power;
	info->Reset = SuperGM3Reset;
	GameHBIRQHook = SuperGM3HBIRQ;
	GameStateRestore = StateRestore;



	setchr8(0);

	CHRRAMSIZE = 8192;
	CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSIZE);
	SetupCartCHRMapping(0x10, CHRRAM, CHRRAMSIZE, 1);
	AddExState(CHRRAM, CHRRAMSIZE, 0, "CHRR");

	WRAMSIZE = 8192;
	WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
	SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
	AddExState(WRAM, WRAMSIZE, 0, "WRAM");

	AddExState(&StateRegs, ~0, 0, 0);
}
