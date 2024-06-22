#include "Protocol.h"
#include "usart.h"
#include <FreeRTOS.h>
#include <MsgManagerPacket.h>
#include "task.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"
#include <algorithm>
#include <atomic>

static uint16_t getFreeId();
static void handshake();
static uint16_t dispatch(uint8_t* buffer, size_t size);

#ifdef __cplusplus
extern "C"{
#endif

extern osThreadId_t ledControllerHandle;
extern osThreadId_t loaderHandle;


constexpr uint16_t desirablePacketSize = 16 * 56;
	// handshake credentials
constexpr uint32_t handshakeStartWord = 0xAE711707;
static constexpr std::array<uint8_t, 16> handshakeBuffer {
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07,
	0xAE, 0x71, 0x17, 0x07
};

static uint16_t ids{}; // 16 potential connections can be handled
static uint32_t startWord{};

//TODO correct size;
constexpr uint32_t loaderMsgBufferSize = 512;
constexpr uint32 processingBufferSize = 1024;
constexpr uint16_t defaultBufferSize = 0x2000;
constexpr uint8_t ledBufferSize = sizeof(smp::ManagerLedTaskMsg);

static uint8_t ledBuffer[ledBufferSize + 2]{};
static StaticStreamBuffer_t ledStreamBuffer{};
StreamBufferHandle_t ledBufferHandle{};

StreamBufferHandle_t loaderBufferHandle{};


void StartMsgManager(void *argument)
{
	static std::array<uint8_t, defaultBufferSize> buffer; // static or on stack, what's better
	static std::array<uint8_t, processingBufferSize> processingBuffer;
	uint16_t processingBufferSize {};

	{
		// TODO tick always the same
		uint32_t newStartWordBase = handshakeStartWord + HAL_GetTick(); // overflow
		startWord = smp::djb2(reinterpret_cast<uint8_t*>(&newStartWordBase), 4);
	}

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
		// TODO processing buffer can be "locked" by loader
		// if we read 16 bytes and have id and so on
		// we answer back that buffer busy
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

static uint16_t getFreeId()
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

static uint16_t dispatch(uint8_t* buffer, size_t size)
{
	smp::BufferedAnswer packet{};
	size_t i = 0;
	while(i < size)
	{
		if(size >= 16 && std::equal(buffer + i, buffer + i + handshakeBuffer.size(),
				handshakeBuffer.cbegin(), handshakeBuffer.cend())){
			handshake();
			i += handshakeBuffer.size();
			continue;
		}

		if(*reinterpret_cast<uint32_t*>(buffer + i) == startWord
				&& (size - i) >= sizeof(smp::header)){
			smp::header* headerView= reinterpret_cast<smp::header*>(buffer + i);
			uint16_t id = headerView->connectionId;

			if(ids & id){
				if(headerView->packetLength > size - i)
					break;
				auto msgSize = headerView->packetLength - sizeof(smp::header);
			    auto hash = smp::djb2(buffer + i, smp::sizeBeforeHashField); // header before hash
			    hash = smp::djb2(buffer + i + sizeof(smp::header), msgSize, hash);
			    if(hash == headerView->hash){
			    	switch(headerView->flags){
			    	case smp::action::goodbye:
						if(headerView->connectionId < 16){
							uint8_t idValToMask = 1;
							idValToMask <<= headerView->connectionId - 1;
							ids &= ~idValToMask;
						}
						// TODO answer on goodbye?
						break;
			    	case smp::action::peripheral:
			    	{
			    		auto device = *reinterpret_cast<smp::peripheral_devices*>(buffer + i + sizeof(smp::header));
			    		switch(device){
			    		case smp::peripheral_devices::LED:{
							const TickType_t sendTimeout = pdMS_TO_TICKS(50);
							smp::BufferedManagerLedTaskMsg msg{};
							auto userMsg = buffer + i + sizeof(smp::header) + sizeof(smp::peripheral_devices);
							msg.packet = {.startWord = startWord, .requestId = static_cast<uint8_t>(id), .msg = *reinterpret_cast<smp::LedMsg*>(userMsg)};
							auto bytesSent = xStreamBufferSend(ledBufferHandle, msg.buffer.data(), msg.buffer.size(), sendTimeout);
			    			break;
			    		}
			    		default: break;
			    		}
			    		break;
			    	}
			    	case smp::action::load:{
			    		if(loaderChannel.processed){
			    			loaderChannel.processed = false;
			    			loaderChannel.buffer = buffer + i;
			    			loaderChannel.size = msgSize; // first 8 bytes -> packetId and whole message size
							xTaskNotify(reinterpret_cast<TaskHandle_t>(loaderHandle), 0, eNoAction);

			    		} else {
							packet.answer = {
								.header = {
									.startWord = startWord,
									.packetLength = static_cast<uint16_t>(packet.buffer.size()),
									.connectionId = id,
									.flags = static_cast<uint16_t>(0x8000 + headerView->flags)
								},
								.code = smp::StatusCode::PrevPacketProcessing
							};
							auto hash = smp::djb2(packet.buffer.data(), smp::sizeBeforeHashField);
							hash = smp::djb2(packet.buffer.data() + sizeof(smp::header), sizeof(packet.answer.code), hash);
							packet.answer.header.hash = hash;
							while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, packet.buffer.data(), packet.buffer.size())){
								vTaskDelay(pdMS_TO_TICKS(100));
							}
			    		}
			    		break;
			    	}
					}
			    } else {
			    	packet.answer = {
						.header = {
							.startWord = startWord,
							.packetLength = static_cast<uint16_t>(packet.buffer.size()),
							.connectionId = id,
							.flags = static_cast<uint16_t>(0x8000 + headerView->flags)
						},
						.code = smp::StatusCode::HashBroken
			    	};

			    	// TODO fix hash calculation in two projects: not 10 -> 12
			    	auto hash = smp::djb2(packet.buffer.data(), smp::sizeBeforeHashField);
			    	hash = smp::djb2(packet.buffer.data() + sizeof(smp::header), sizeof(packet.answer.code), hash);
			    	packet.answer.header.hash = hash;
			    	// TODO think how to write back better
					while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, packet.buffer.data(), packet.buffer.size())){
						vTaskDelay(pdMS_TO_TICKS(100));
					}
			    }
				i += headerView->packetLength;
			} else {
				// send wrong id?
				packet.answer = {
					.header = {
						.startWord = startWord,
						.packetLength = static_cast<uint16_t>(packet.buffer.size()),
						.connectionId = static_cast<uint8_t>(id),
						.flags = static_cast<uint16_t>(0x8000 + headerView->flags)
					},
					.code = smp::StatusCode::InvalidId
				};
				auto hash = smp::djb2(packet.buffer.data(), smp::sizeBeforeHashField);
				hash = smp::djb2(packet.buffer.data() + sizeof(smp::header), sizeof(packet.answer.code), hash);
				packet.answer.header.hash = hash;
				// TODO think how to write back better
				while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, packet.buffer.data(), packet.buffer.size())){
					vTaskDelay(pdMS_TO_TICKS(100));
				}
				i += headerView->packetLength;
			}
		}
		break;
	}
	return i;
}

static void handshake() noexcept
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
