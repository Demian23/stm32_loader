#include "Protocol.h"
#include "usart.h"
#include <FreeRTOS.h>
#include "SmpUtil.h"
#include "task.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"
#include <algorithm>
#include <atomic>
#include <random>

namespace {

uint16_t getFreeId() noexcept;
void handshake() noexcept;
uint16_t dispatch(uint8_t* buffer, size_t size) noexcept;
void goodbye(uint16_t id) noexcept;
void peripheral(uint8_t* buffer) noexcept;
bool hashValid(uint8_t* buffer) noexcept;

void generateStartWord();

constexpr uint16_t desirablePacketSize = 16 * 56;
	// handshake credentials
constexpr uint32_t handshakeStartWord = 0xAE711707;
constexpr std::array<uint8_t, 16> handshakeBuffer {
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07
};

uint16_t ids{}; // 16 potential connections can be handled
uint32_t startWord{};


constexpr uint32_t loaderMsgBufferSize = 512;
constexpr uint32_t processingBufferSize = 1024;
constexpr uint16_t defaultBufferSize = 0x2000;
//TODO correct size;
constexpr uint8_t ledBufferSize = sizeof(smp::ManagerLedTaskMsg);
uint8_t ledBuffer[ledBufferSize + 2]{};
StaticStreamBuffer_t ledStreamBuffer{};



}

#ifdef __cplusplus
extern "C"{
#endif

StreamBufferHandle_t ledBufferHandle{};

StreamBufferHandle_t loaderBufferHandle{};

extern osThreadId_t ledControllerHandle;


void StartMsgManager(void *argument)
{
	static std::array<uint8_t, defaultBufferSize> buffer; // static or on stack, what's better
	static std::array<uint8_t, processingBufferSize> processingBuffer;
	uint16_t processingBufferSize {};


	ledBufferHandle = xStreamBufferCreateStatic(ledBufferSize + 1, ledBufferSize, ledBuffer, &ledStreamBuffer); // TODO debug here size of buffer should be less on one byte according to docs, but smth was wrong
	loaderBufferHandle = xStreamBufferCreate(loaderMsgBufferSize, 1);

	HAL_UART_Receive_DMA(&huart1, buffer.begin(), buffer.size());
	auto writeIter = buffer.begin();

	xTaskNotify(reinterpret_cast<TaskHandle_t>(ledControllerHandle), 0, eNoAction);
	while(true){
		auto currentIter = buffer.begin() + buffer.size() - huart1.hdmarx->Instance->NDTR;
		size_t written{};
		if(writeIter < currentIter){
			written = currentIter - writeIter;
			if(written < processingBuffer.size() - processingBufferSize){
				std::copy(writeIter, currentIter, processingBuffer.begin() + processingBufferSize);
			} else {
				// TODO solve it
			}
		}
		if(writeIter > currentIter){
			written = buffer.end() - writeIter  + currentIter - buffer.begin();
			if(written < processingBuffer.size() - processingBufferSize){
				auto resume = std::copy(writeIter, buffer.end(), processingBuffer.begin() + processingBufferSize);
				std::copy(buffer.begin(), currentIter, resume);
			} else {

			}
		}
		processingBufferSize += written;
		writeIter = currentIter;
		auto lastPos = dispatch(processingBuffer.begin(), processingBufferSize);
		if(lastPos != processingBufferSize){
			// shift to start
		} else {
			processingBufferSize = 0;
		}
	}
}


#ifdef __cplusplus
}
#endif

namespace {

uint16_t getFreeId() noexcept
{
	if(ids == 0xFFFF)
		return 0;
	else {
		uint16_t result = 1;
		uint16_t mask = 1;
		while((ids & mask) != 0){
			mask <<= 1;
			result += 1;
		}
		ids |= mask;
		return result;
	}
}

uint16_t dispatch(uint8_t* buffer, size_t size) noexcept
{
	static smp::BufferedAnswer packet{};
	size_t i = 0;
	while(i < size)
	{
		if(size >= 16 && std::equal(buffer + i, buffer + i + handshakeBuffer.size(),
				handshakeBuffer.cbegin(), handshakeBuffer.cend())){
			if(startWord == 0){
				generateStartWord();
			}
			handshake();
			i += handshakeBuffer.size();
			continue;
		}

		if(*reinterpret_cast<uint32_t*>(buffer + i) == startWord
				&& (size - i) >= sizeof(smp::header)){
			smp::header* headerView= reinterpret_cast<smp::header*>(buffer + i);
			uint16_t id = headerView->connectionId;

			if((ids & id)){
				if(headerView->packetLength <= size - i){
					if(hashValid(buffer + i)){
						switch(headerView->flags){
						case smp::action::goodbye:
							goodbye(headerView->connectionId);
							break;
						case smp::action::peripheral:
							peripheral(buffer + i);
							break;
						case smp::action::startLoad:
							if(headerView->packetLength == sizeof(smp::StartLoadHeader)){
								// TODO ret val?
								xStreamBufferSend(loaderBufferHandle, buffer + i, sizeof(smp::StartLoadHeader), pdMS_TO_TICKS(100));
							} else {
								smp::sendAnswer(packet, startWord, id, headerView->flags + 0x8000, smp::StatusCode::WrongMsgSize);
							}
							break;
						case smp::action::loading:
							if(headerView->packetLength > sizeof(smp::LoadHeader) && headerView->packetLength <= desirablePacketSize){
								xStreamBufferSend(loaderBufferHandle, buffer + i, headerView->packetLength, pdMS_TO_TICKS(100));
							} else {
								smp::sendAnswer(packet, startWord, id, headerView->flags + 0x8000, smp::StatusCode::WrongMsgSize);
							}
							break;
						}
					} else {
						smp::sendAnswer(packet, startWord, id, headerView->flags + 0x8000, smp::StatusCode::HashBroken);
					}
				} else {
					break;
				}
			} else {
				// send wrong id?
				smp::sendAnswer(packet, startWord, id, headerView->flags + 0x8000, smp::StatusCode::InvalidId);
			}
			i += headerView->packetLength;
		}
	}
	return i;
}

void handshake() noexcept
{
	auto id = getFreeId();
	if(id){
		constexpr auto bufferSize = handshakeBuffer.size() + sizeof(startWord) + sizeof(desirablePacketSize) + sizeof(id);
		std::array<uint8_t, bufferSize> buffer{};
		auto nextIter = std::copy(handshakeBuffer.cbegin(), handshakeBuffer.cend(), buffer.begin());
		*reinterpret_cast<uint32_t*>(nextIter) = startWord;
		nextIter += sizeof(startWord);
		*reinterpret_cast<uint16_t*>(nextIter) = desirablePacketSize;
		nextIter += sizeof(desirablePacketSize);
		*reinterpret_cast<uint16_t*>(nextIter) = id;
		// TODO fix this
		while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, buffer.cbegin(), buffer.size())){
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}
}

void goodbye(uint16_t id) noexcept
{
	if(id < 16){
		uint8_t idValToMask = 1;
		idValToMask <<= id - 1;
		ids &= ~idValToMask;
	}
}

void peripheral(uint8_t* buffer)noexcept
{
	auto device = *reinterpret_cast<smp::peripheral_devices*>(buffer + sizeof(smp::header));
	switch(device){
	case smp::peripheral_devices::LED:{
		smp::BufferedManagerLedTaskMsg msg{};
		auto id = static_cast<uint8_t>(reinterpret_cast<smp::header*>(buffer)->connectionId);
		auto ledMsg = *reinterpret_cast<smp::LedMsg*>(buffer + sizeof(smp::header) + sizeof(smp::peripheral_devices));
		msg.packet = {.startWord = startWord, .requestId = id, .msg = ledMsg};
		xStreamBufferSend(ledBufferHandle, msg.buffer.data(), msg.buffer.size(), pdMS_TO_TICKS(50));
		break;
	}
	default: break;
	}
}

bool hashValid(uint8_t* buffer) noexcept
{
	auto msgSize = reinterpret_cast<smp::header*>(buffer)->packetLength - sizeof(smp::header);
	auto hash = smp::djb2(buffer, smp::sizeBeforeHashField); // header before hash
	hash = smp::djb2(buffer + sizeof(smp::header), msgSize, hash);
	return reinterpret_cast<smp::header*>(buffer)->hash == hash;
}

void generateStartWord()noexcept
{
	uint32_t newStartWordBase = static_cast<uint32_t>(handshakeStartWord + 1ULL + HAL_GetTick());
	startWord = smp::djb2(reinterpret_cast<uint8_t*>(&newStartWordBase), 4);
}

}
