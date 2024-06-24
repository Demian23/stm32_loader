#include "Protocol.h"
#include "usart.h"
#include <FreeRTOS.h>
#include "stream_buffer.h"
#include <SmpUtil.h>
#include "MsgLoader.h"
#include <new>
#include "task.h"
#include "gpio.h"
#include "SmpUtil.h"

#ifdef __cplusplus
extern "C"{
#endif

// TODO if i move free rtos heap to ccmram
// need to check that all dma buffers in static memory or allocated with smth as malloc

extern StreamBufferHandle_t loaderBufferHandle;


void StartLoader(void*) noexcept
{
	static smp::BufferedAnswer packet{};
	while(true){
		smp::BufferedStartLoadHeader startHeader{};
		// get whole startLoad packet
		auto received = xStreamBufferReceive(loaderBufferHandle, startHeader.buffer.data(), startHeader.buffer.size(), portMAX_DELAY);
		auto [startWord, packetLength, id, flags, hash] = startHeader.content.baseHeader;
		if(received == startHeader.buffer.size()
			&& flags == smp::action::startLoad
			&& packetLength == startHeader.buffer.size())
		{
			smp::MsgLoader loader{startHeader.content.msg.wholeMsgSize, startHeader.content.msg.wholeMsgHash};
			if(loader){
				smp::BufferedLoadHeader headerReceiver{};
				bool error = false;
				while(!loader.loaded() && !error){
					auto x2min = pdMS_TO_TICKS(120000);
					received = xStreamBufferReceive(loaderBufferHandle, headerReceiver.buffer.data(), headerReceiver.buffer.size(), x2min);
					// more complex check of header startWord, id
					if(received != 0){
						auto [startWord, packetLength, id, flags, hash] = headerReceiver.header.baseHeader;
						if(received == headerReceiver.buffer.size() && flags == smp::action::loading){
							if(loader.isValidPacket(headerReceiver.header.msg)){
								auto msgSize = packetLength - headerReceiver.buffer.size();
								if(loader.getNextPacketFromStreamBuffer(loaderBufferHandle, msgSize)){
									smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::Ok);
								} else {
									xStreamBufferReset(loaderBufferHandle);
									// fails only if waiting to receive or send not null
									// in my architecture seems impossible
									smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::LoadExtraSize);
									error = true;
								}
							} else {
								xStreamBufferReset(loaderBufferHandle);
								smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::LoadWrongPacket);
								error = true;
							}
						} else {
							xStreamBufferReset(loaderBufferHandle);
							smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::WaitLoad);
							error = true;
						}
					} else {
						// timeout
						error = true;
					}
				}
				if(!error){
					// all loaded, ready for write to flash
				}
			} else {
				xStreamBufferReset(loaderBufferHandle);
				smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::NoMemory);
			}
		} else {
			// flush stream buffer
			// send smth as start load assumed
			xStreamBufferReset(loaderBufferHandle);
			smp::sendAnswer(packet, startWord, id, flags, smp::StatusCode::WaitStartLoad);

		}
	}
}

#ifdef __cplusplus
}
#endif

