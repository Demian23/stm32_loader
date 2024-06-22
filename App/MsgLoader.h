#pragma once

#include "Protocol.h"
#include <cstdint>

namespace smp {

class MsgLoader final {
public:
	MsgLoader(uint32_t wholeSize, uint32_t msgHash) noexcept;
	smp::StatusCode copy() noexcept; // from packet, knowing protocol details
	bool loaded()const noexcept;
	explicit operator bool() const noexcept;
	~MsgLoader();

	MsgLoader(MsgLoader &&other) noexcept;

	MsgLoader& operator=(MsgLoader &&other) noexcept = delete;
	MsgLoader(const MsgLoader&) = delete;
	MsgLoader& operator=(const MsgLoader&) = delete;
private:
	uint8_t* acceptBuffer;
	uint32_t msgSize;
	uint32_t currentPosition;
	uint32_t lastProcessedId;
	uint32_t msgHash;
};

} /* namespace smp */

