#include <MsgLoader.h>
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

MsgLoader::MsgLoader(MsgLoader &&other)
	: acceptBuffer{other.acceptBuffer},
	  msgSize{other.firmwareSize},
	  currentPosition{other.currentPosition},
	  lastProcessedId{lastProcessedId}
{
	other.acceptBuffer = nullptr;
	other.msgSize= 0; other.currentPosition = 0; other.lastProcessedId = 0;
}

bool MsgLoader::loaded() const noexcept{
	return currentPosition == msgSize;
}


} /* namespace smp */
