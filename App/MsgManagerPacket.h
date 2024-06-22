#pragma once

#include "Protocol.h"


namespace smp{

#pragma pack(push,2)
struct ManagerLedTaskMsg{
	uint32_t startWord;
	uint8_t requestId;
	LedMsg msg;
};
#pragma pack(pop)

static_assert(sizeof(ManagerLedTaskMsg) == sizeof(LedMsg) + sizeof(uint32_t) + sizeof(uint8_t));

union BufferedManagerLedTaskMsg{
	ManagerLedTaskMsg packet;
	std::array<uint8_t, sizeof(packet)> buffer;

};

}
