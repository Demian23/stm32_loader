#include "MsgLoader.h"
#include "projdefs.h"
#include <new>

namespace smp {
constexpr auto smallBufferSize = 16 * 56 - sizeof(smp::LoadHeader);

MsgLoader::MsgLoader(uint32_t wholeSize, uint32_t msgHash)
	:
#ifdef PROVOKE_NO_MEMORY
	acceptBuffer{nullptr},
#else
	acceptBuffer{new (std::nothrow_t{}) uint8_t[wholeSize]},
#endif
	  msgSize{wholeSize}, currentPosition{}, lastProcessedId{}, msgHash{msgHash}, allocatedAll{true}
{
    if(!(allocatedAll = acceptBuffer != nullptr)){
        acceptBuffer = new(std::nothrow_t{}) uint8_t[smallBufferSize];
    }
}
bool MsgLoader::allAllocated() const noexcept{return allocatedAll;}

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
	return msg.msgHash == msgHash && msg.packetId == lastProcessedId;
}

bool MsgLoader::getNextPacketFromStreamBuffer(StreamBufferHandle_t handle, uint32_t size) noexcept
{
	bool result = false;
	auto x2min = pdMS_TO_TICKS(120000);
	size_t received = 0;
	if(allocatedAll){
		if(size + currentPosition <= msgSize){
			received = xStreamBufferReceive(handle, acceptBuffer + currentPosition, size, x2min);
		}
	} else {
		if(size <= smallBufferSize && size + currentPosition <= msgSize){
			received = xStreamBufferReceive(handle, acceptBuffer, size, x2min);
		}
	}
	if(received == size){ // or loop here?
		result = true;
		lastProcessedId++;
		currentPosition += received;
	}
	return result;
}

const uint8_t* MsgLoader::data()const noexcept
{
	return acceptBuffer;
}

uint32_t MsgLoader::size() const noexcept
{
	return msgSize;
}
uint32_t MsgLoader::pos() const noexcept{return currentPosition;}

} /* namespace smp */
