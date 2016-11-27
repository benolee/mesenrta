#include "stdafx.h"
#include "MemoryManager.h"
#include "PPU.h"
#include "CodeDataLogger.h"
#include "BaseMapper.h"
#include "MemoryDumper.h"
#include "VideoDecoder.h"

MemoryDumper::MemoryDumper(shared_ptr<PPU> ppu, shared_ptr<MemoryManager> memoryManager, shared_ptr<BaseMapper> mapper, shared_ptr<CodeDataLogger> codeDataLogger)
{
	_ppu = ppu;
	_memoryManager = memoryManager;
	_mapper = mapper;
	_codeDataLogger = codeDataLogger;
}

void MemoryDumper::SetMemoryState(DebugMemoryType type, uint8_t *buffer)
{
	switch(type) {
		case DebugMemoryType::InternalRam:
			for(int i = 0; i < 0x800; i++) {
				_memoryManager->DebugWrite(i, buffer[i]);
			}
			break;

		case DebugMemoryType::PaletteMemory:
			for(int i = 0; i < 0x20; i++) {
				_ppu->WritePaletteRAM(i, buffer[i]);
			}
			break;

		case DebugMemoryType::SpriteMemory: memcpy(_ppu->GetSpriteRam(), buffer, 0x100); break;
		case DebugMemoryType::SecondarySpriteMemory: memcpy(_ppu->GetSecondarySpriteRam(), buffer, 0x20); break;

		case DebugMemoryType::ChrRam:
		case DebugMemoryType::WorkRam:
		case DebugMemoryType::SaveRam:
			_mapper->WriteMemory(type, buffer);
			break;
	}
}

uint32_t MemoryDumper::GetMemoryState(DebugMemoryType type, uint8_t *buffer)
{
	switch(type) {
		case DebugMemoryType::CpuMemory:
			for(int i = 0; i <= 0xFFFF; i++) {
				buffer[i] = _memoryManager->DebugRead(i);
			}
			return 0x10000;

		case DebugMemoryType::PpuMemory:
			for(int i = 0; i <= 0x3FFF; i++) {
				buffer[i] = _memoryManager->DebugReadVRAM(i);
			}
			return 0x4000;

		case DebugMemoryType::PaletteMemory:
			for(int i = 0; i <= 0x1F; i++) {
				buffer[i] = _ppu->ReadPaletteRAM(i);
			}
			return 0x20;

		case DebugMemoryType::SpriteMemory:
			memcpy(buffer, _ppu->GetSpriteRam(), 0x100);
			return 0x100;

		case DebugMemoryType::SecondarySpriteMemory:
			memcpy(buffer, _ppu->GetSecondarySpriteRam(), 0x20);
			return 0x20;

		case DebugMemoryType::PrgRom:
		case DebugMemoryType::ChrRom:
		case DebugMemoryType::ChrRam:
		case DebugMemoryType::WorkRam:
		case DebugMemoryType::SaveRam:
			return _mapper->CopyMemory(type, buffer);

		case DebugMemoryType::InternalRam:
			for(int i = 0; i < 0x800; i++) {
				buffer[i] = _memoryManager->DebugRead(i);
			}
			return 0x800;
	}
	return 0;
}

void MemoryDumper::GetNametable(int nametableIndex, uint32_t* frameBuffer, uint8_t* tileData, uint8_t* paletteData)
{
	uint16_t *screenBuffer = new uint16_t[256 * 240];
	uint16_t bgAddr = _ppu->GetState().ControlFlags.BackgroundPatternAddr;
	uint16_t baseAddr = 0x2000 + nametableIndex * 0x400;
	uint16_t baseAttributeAddr = baseAddr + 960;
	for(uint8_t y = 0; y < 30; y++) {
		for(uint8_t x = 0; x < 32; x++) {
			uint8_t tileIndex = _mapper->ReadVRAM(baseAddr + (y << 5) + x);
			uint8_t attribute = _mapper->ReadVRAM(baseAttributeAddr + ((y & 0xFC) << 1) + (x >> 2));
			tileData[y * 32 + x] = tileIndex;
			paletteData[y * 32 + x] = attribute;
			uint8_t shift = (x & 0x02) | ((y & 0x02) << 1);

			uint8_t paletteBaseAddr = ((attribute >> shift) & 0x03) << 2;
			uint16_t tileAddr = bgAddr + (tileIndex << 4);
			for(uint8_t i = 0; i < 8; i++) {
				uint8_t lowByte = _mapper->ReadVRAM(tileAddr + i);
				uint8_t highByte = _mapper->ReadVRAM(tileAddr + i + 8);
				for(uint8_t j = 0; j < 8; j++) {
					uint8_t color = ((lowByte >> (7 - j)) & 0x01) | (((highByte >> (7 - j)) & 0x01) << 1);
					screenBuffer[(y << 11) + (x << 3) + (i << 8) + j] = color == 0 ? _ppu->ReadPaletteRAM(0) : _ppu->ReadPaletteRAM(paletteBaseAddr + color);
				}
			}
		}
	}

	VideoDecoder::GetInstance()->DebugDecodeFrame(screenBuffer, frameBuffer, 256 * 240);

	delete[] screenBuffer;
}

void MemoryDumper::GetChrBank(int bankIndex, uint32_t* frameBuffer, uint8_t palette, bool largeSprites, CdlHighlightType highlightType)
{
	uint16_t *screenBuffer = new uint16_t[128 * 128];
	uint8_t chrBuffer[0x1000];
	bool chrIsDrawn[0x1000];
	bool tileUsed[0x4000];
	if(bankIndex == 0 || bankIndex == 1) {
		uint16_t baseAddr = bankIndex == 0 ? 0x0000 : 0x1000;
		for(int i = 0; i < 0x1000; i++) {
			chrBuffer[i] = _mapper->ReadVRAM(baseAddr + i);
			chrIsDrawn[i] = _codeDataLogger->IsDrawn(_mapper->ToAbsoluteChrAddress(baseAddr + i));
		}
	} else {
		int bank = bankIndex - 2;
		uint32_t baseAddr = bank * 0x1000;
		bool useChrRam = _mapper->GetChrSize(false) == 0;
		uint32_t chrSize = _mapper->GetChrSize(useChrRam);
		vector<uint8_t> chrData(chrSize, 0);
		_mapper->CopyMemory(useChrRam ? DebugMemoryType::ChrRam : DebugMemoryType::ChrRom, chrData.data());

		for(int i = 0; i < 0x1000; i++) {
			chrBuffer[i] = chrData[baseAddr + i];
			chrIsDrawn[i] = useChrRam ? true : _codeDataLogger->IsDrawn(baseAddr + i);
		}
	}

	for(uint8_t y = 0; y < 16; y++) {
		for(uint8_t x = 0; x < 16; x++) {
			uint8_t tileIndex = y * 16 + x;
			uint8_t paletteBaseAddr = palette << 2;
			uint16_t tileAddr = tileIndex << 4;
			for(uint8_t i = 0; i < 8; i++) {
				uint8_t lowByte = chrBuffer[tileAddr + i];
				uint8_t highByte = chrBuffer[tileAddr + i + 8];
				bool isDrawn = chrIsDrawn[tileAddr + i];
				for(uint8_t j = 0; j < 8; j++) {
					uint8_t color = ((lowByte >> (7 - j)) & 0x01) | (((highByte >> (7 - j)) & 0x01) << 1);

					uint32_t position;
					if(largeSprites) {
						int tmpX = x / 2 + ((y & 0x01) ? 8 : 0);
						int tmpY = (y & 0xFE) + ((x & 0x01) ? 1 : 0);

						position = (tmpY << 10) + (tmpX << 3) + (i << 7) + j;
					} else {
						position = (y << 10) + (x << 3) + (i << 7) + j;
					}

					screenBuffer[position] = color == 0 ? _ppu->ReadPaletteRAM(0) : _ppu->ReadPaletteRAM(paletteBaseAddr + color);
					tileUsed[position] = isDrawn;
				}
			}
		}
	}

	VideoDecoder::GetInstance()->DebugDecodeFrame(screenBuffer, frameBuffer, 128 * 128);

	if(highlightType != CdlHighlightType::None) {
		for(int i = 0; i < 0x4000; i++) {
			if(tileUsed[i] == (highlightType != CdlHighlightType::HighlightUsed)) {
				frameBuffer[i] &= 0x4FFFFFFF;
			}
		}
	}

	delete[] screenBuffer;
}

void MemoryDumper::GetSprites(uint32_t* frameBuffer)
{
	uint16_t *screenBuffer = new uint16_t[64 * 128];
	memset(screenBuffer, 0, 64 * 128 * sizeof(uint16_t));
	uint8_t *spriteRam = _ppu->GetSpriteRam();

	uint16_t spriteAddr = _ppu->GetState().ControlFlags.SpritePatternAddr;
	bool largeSprites = _ppu->GetState().ControlFlags.LargeSprites;

	for(uint8_t y = 0; y < 8; y++) {
		for(uint8_t x = 0; x < 8; x++) {
			uint8_t ramAddr = ((y << 3) + x) << 2;
			uint8_t tileIndex = spriteRam[ramAddr + 1];
			uint8_t attributes = spriteRam[ramAddr + 2];

			bool verticalMirror = (attributes & 0x80) == 0x80;
			bool horizontalMirror = (attributes & 0x40) == 0x40;

			uint16_t tileAddr;
			if(largeSprites) {
				tileAddr = (tileIndex & 0x01 ? 0x1000 : 0x0000) + ((tileIndex & 0xFE) << 4);
			} else {
				tileAddr = spriteAddr + (tileIndex << 4);
			}

			uint8_t palette = 0x10 + ((attributes & 0x03) << 2);

			for(uint8_t i = 0, iMax = largeSprites ? 16 : 8; i < iMax; i++) {
				if(i == 8) {
					//Move to next tile for 2nd tile of 8x16 sprites
					tileAddr += 8;
				}

				uint8_t lowByte = _mapper->ReadVRAM(tileAddr + i);
				uint8_t highByte = _mapper->ReadVRAM(tileAddr + i + 8);
				for(uint8_t j = 0; j < 8; j++) {
					uint8_t color;
					if(horizontalMirror) {
						color = ((lowByte >> j) & 0x01) | (((highByte >> j) & 0x01) << 1);
					} else {
						color = ((lowByte >> (7 - j)) & 0x01) | (((highByte >> (7 - j)) & 0x01) << 1);
					}

					uint16_t destAddr;
					if(verticalMirror) {
						destAddr = (y << 10) + (x << 3) + (((largeSprites ? 15 : 7) - i) << 6) + j;
					} else {
						destAddr = (y << 10) + (x << 3) + (i << 6) + j;
					}

					screenBuffer[destAddr] = color == 0 ? _ppu->ReadPaletteRAM(0) : _ppu->ReadPaletteRAM(palette + color);
				}
			}
		}
	}

	VideoDecoder::GetInstance()->DebugDecodeFrame(screenBuffer, frameBuffer, 64 * 128);

	delete[] screenBuffer;
}

void MemoryDumper::GetPalette(uint32_t* frameBuffer)
{
	uint16_t *screenBuffer = new uint16_t[4 * 8];
	for(uint8_t i = 0; i < 32; i++) {
		screenBuffer[i] = _ppu->ReadPaletteRAM(i);
	}
	VideoDecoder::GetInstance()->DebugDecodeFrame(screenBuffer, frameBuffer, 4 * 8);
	delete[] screenBuffer;
}