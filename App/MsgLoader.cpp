#include "MsgLoader.h"
#include "projdefs.h"
#include <new>

namespace smp {

MsgLoader::MsgLoader(uint32_t wholeSize, uint32_t msgHash)
	: acceptBuffer{new (std::nothrow_t{}) uint8_t[wholeSize]},
	  msgSize{wholeSize}, currentPosition{}, lastProcessedId{}, msgHash{msgHash}
{}

MsgLoader::operator bool() const noexcept{
	return acceptBuffer != nullptr;
}

MsgLoader::~MsgLoader() {
	if(acceptBuffer != nullptr){
		delete[] acceptBuffer; // change to malloc?
	}
}

MsgLoader::MsgLoader(MsgLoader &&other) noexcept
	: acceptBuffer{other.acceptBuffer},
	  msgSize{other.msgSize},
	  currentPosition{other.currentPosition},
	  lastProcessedId{other.lastProcessedId}
{
	other.acceptBuffer = nullptr;
	other.msgSize= 0; other.currentPosition = 0; other.lastProcessedId = 0;
}

bool MsgLoader::loaded() const noexcept{
	return currentPosition == msgSize;
}


bool MsgLoader::isValidPacket(LoadMsg msg)const noexcept
{
	return msg.msgHash == msgHash && msg.packetId == lastProcessedId + 1;
}

bool MsgLoader::getNextPacketFromStreamBuffer(StreamBufferHandle_t handle, uint32_t size) noexcept
{
	bool result = false;
	if(size + currentPosition <= msgSize){
		auto x2min = pdMS_TO_TICKS(120000);
		auto received = xStreamBufferReceive(handle, acceptBuffer + currentPosition, size, x2min);
		// if some data left in buffer check
		if(received == size){ // or loop here?
			result = true;
		}
	}
	return result;
}


} /* namespace smp */
