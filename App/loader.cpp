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
#include "FlashWriter.h"

namespace {
	smp::StatusCode flashResultCodeToSmpStatusCode(FlashWriter::ResCode code) noexcept;
}

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
				FlashWriter writer{};
				auto code = writer.erase();
				if(code == FlashWriter::ResCode::Ok){
					bool error = false;
					smp::sendAnswer(packet, startWord,  id, flags, smp::StatusCode::Ok);
					while(!loader.loaded() && !error){
						auto x2min = pdMS_TO_TICKS(10000);
						received = xStreamBufferReceive(loaderBufferHandle, headerReceiver.buffer.data(), headerReceiver.buffer.size(), x2min);
						if(received != 0){
							auto [startWord, packetLength, id, flags, hash] = headerReceiver.header.baseHeader;
							if(received == headerReceiver.buffer.size() && flags == smp::action::loading){
								if(loader.isValidPacket(headerReceiver.header.msg)){
									auto msgSize = packetLength - headerReceiver.buffer.size();
									if(loader.getNextPacketFromStreamBuffer(loaderBufferHandle, msgSize)){
										if(!loader.allAllocated()){ // save in buffer
											code = writer.write(loader, msgSize);
											if(code != FlashWriter::ResCode::Ok){
												xStreamBufferReset(loaderBufferHandle);
												smp::sendAnswer(packet, startWord, id, flags, flashResultCodeToSmpStatusCode(code));
												error = true;
											}
										}
										if(!error)
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
					if(loader.allAllocated() && !error){
						code = writer.write(loader.data(), loader.size());
						smp::sendAnswer(packet, startWord, id, smp::action::loading, flashResultCodeToSmpStatusCode(code));
					}
				} else {
					xStreamBufferReset(loaderBufferHandle);
					smp::sendAnswer(packet, startWord, id, flags, flashResultCodeToSmpStatusCode(code));
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

namespace {

smp::StatusCode flashResultCodeToSmpStatusCode(FlashWriter::ResCode code) noexcept
{
	switch(code){
	case FlashWriter::ResCode::Ok:
		return smp::StatusCode::Ok;
	case FlashWriter::ResCode::CantWrite:
		return smp::StatusCode::FailedWrite;
	case FlashWriter::ResCode::FlashBusy:
		return smp::StatusCode::DeviceBusy;
	case FlashWriter::ResCode::ErasingMoreThanOneSector:
		return smp::StatusCode::WrongMsgSize;
	default: return smp::StatusCode::Invalid;
	}
}

}
