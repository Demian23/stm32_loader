#pragma once
#include <cstdint>
#include "MsgLoader.h"

class FlashWriter final {
public:

	enum class ResCode{Ok, AlreadyUnlocked, FlashBusy, CantWrite, ErasingMoreThanOneSector, MsgTooBig};
	static constexpr uint32_t baseWriteAddress = 0x08020000;
	// writing only from 5th sector, 0x08020000
	FlashWriter() noexcept;
	~FlashWriter();

	ResCode write(const uint8_t* buffer, uint32_t size) noexcept;
	ResCode write(const smp::MsgLoader& loader, uint32_t size) noexcept;
	ResCode erase() noexcept;
};
