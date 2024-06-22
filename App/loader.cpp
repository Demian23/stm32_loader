#include "Protocol.h"
#include "usart.h"
#include "stream_buffer.h"
#include <FreeRTOS.h>
#include "MsgManagerPacket.h"
#include "MsgLoader.h"
#include <new>
#include "task.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C"{
#endif

// TODO if i move free rtos heap to ccmram
// need to check that all dma buffers in static memory or allocated with smth as malloc

extern StreamBufferHandle_t loaderBufferHandle{};

void StartLoader(void*)
{
	// TODO communication through stream buffer of 512 byte?

	static smp::BufferedAnswer packet{};
	while(true){
		xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
		// read from stream buffer size and hash
		uint32_t wholeMessageSize, msgHash;
		smp::MsgLoader loader{wholeMessageSize, msgHash};
		std::array<uint8_t, 512> receiver{}; // or maybe small buffer here
		if(loader){

			while(!loader.loaded()){
				auto x2min = pdMS_TO_TICKS(120000);
				auto received = xStreamBufferReceive(loaderBufferHandle, receiver, receiver.size(), x2min);
				// read from stream buffer
				// copy msg
				// write back
			}
			// somewhere here code responsible for writing to flash
			// with crit section
		} else {
			// write back error no memory
		}

	}

	uint8_t* acceptBuffer = nullptr;
	while(true){
		xTaskNotifyWait(0, 0, NULL, portMAX_DELAY); // notify when comes first packet
		// create Loader
		// block on stream buffer in while, while msg size is not enough
		// if some error though this stream buffer abort all operation
		auto ptr = loaderChannel.buffer.load();
		auto size = loaderChannel.size.load();
		auto[id, wholeSize] = *reinterpret_cast<smp::LoadMsg*>(ptr + sizeof(smp::header));
		if(!wholeMessageSize){
			// first packet
			if(id == 0){
				wholeMessageSize = wholeSize;
				if(acceptBuffer != nullptr){
					// free buffer
					delete[] acceptBuffer;
					acceptBuffer = nullptr;
				}
				acceptBuffer = new (std::nothrow_t{}) uint8_t[wholeMessageSize];
				if(acceptBuffer != nullptr){
					std::copy(ptr + sizeof(smp::header) + sizeof(smp::LoadMsg), ptr + sizeof(smp::header) + sizeof(smp::LoadMsg) + size, acceptBuffer);
					loadedSize = size;
					lastProcessedId = id;

				} else {
					// no memory answer
				}
			} else {
				// packet lost, write back
			}
		} else {
			if(id == lastProcessedId + 1){
				std::copy(ptr + sizeof(smp::header) + sizeof(smp::LoadMsg), ptr + sizeof(smp::header) + sizeof(smp::LoadMsg) + size, acceptBuffer + loadedSize);
				loadedSize += size;
			} else {
				// packet lost
			}
		}
		loaderChannel.processed = true;
		// answer
		smp::BufferedLoadHeader answer{};
		std::copy(ptr, ptr + answer.buffer.size(), answer.buffer.data());
		answer.header.baseHeader.flags += 0x8000;
		auto hash = smp::djb2(packet.buffer.data(), smp::sizeBeforeHashField);
		hash = smp::djb2(ptr + sizeof(smp::header), sizeof(smp::LoadMsg),
								hash);
		answer.header.baseHeader.hash = hash;
		while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, answer.buffer.data(), answer.buffer.size())){
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}
}

#ifdef __cplusplus
}
#endif
