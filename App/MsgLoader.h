#pragma once

#include "Protocol.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include <cstdint>

namespace smp {

class MsgLoader final {
public:
	MsgLoader(uint32_t wholeSize, uint32_t msgHash) noexcept;
	smp::StatusCode copy() noexcept; // from packet, knowing protocol details
	bool isValidPacket(LoadMsg msg)const noexcept;
	bool loaded()const noexcept;
	bool getNextPacketFromStreamBuffer(StreamBufferHandle_t handle, uint32_t size) noexcept;
	explicit operator bool() const noexcept;
	~MsgLoader();

	const uint8_t* data()const noexcept;
	uint32_t size() const noexcept;

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

