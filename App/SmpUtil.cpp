#include "SmpUtil.h"
#include "usart.h"
#include "Protocol.h"
#include "FreeRTOS.h"
#include "task.h"


namespace smp{

void sendAnswer(smp::BufferedAnswer& answer, uint32_t start, uint16_t id, uint16_t flags, smp::StatusCode code) noexcept
{
	answer.answer = {.header =
		{
			.startWord = start,
			.packetLength = static_cast<uint32_t>(answer.buffer.size()),
			.connectionId = id,
			.flags = static_cast<uint16_t>(0x8000 + flags)
		},
		.code = code
	};
	auto hash = smp::djb2(answer.buffer.data(), smp::sizeBeforeHashField);
	hash = smp::djb2(answer.buffer.data() + sizeof(smp::header), answer.buffer.size() - sizeof(smp::header), hash);
	answer.answer.header.hash = hash;
	while(HAL_OK != HAL_UART_Transmit_DMA(&huart1, answer.buffer.data(), answer.buffer.size())){
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

}
