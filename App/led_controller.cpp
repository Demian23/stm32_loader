#include <FreeRTOS.h>
#include "SmpUtil.h"
#include <array>
#include <algorithm>
#include <utility>
#include <string_view>
#include "usart.h"
#include "gpio.h"
#include "task.h"
#include "Protocol.h"
#include "stream_buffer.h"

extern StreamBufferHandle_t ledBufferHandle;

namespace{

smp::StatusCode operateWithOneLed(uint8_t ledNumber, smp::led_ops op) noexcept;
smp::StatusCode operateWithAllLed(smp::led_ops op) noexcept;

}

#ifdef __cplusplus
extern "C"{
#endif

void StartLedController(void *argument) noexcept
{
	// read from stream buffer in loop
	// wait for signal, than run
	smp::BufferedManagerLedTaskMsg msg{};
	static smp::BufferedAnswer answer{};
	xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
	while(true){
		auto receivedBytes = xStreamBufferReceive(ledBufferHandle, msg.buffer.begin(), msg.buffer.size(), portMAX_DELAY);
		auto [startWordLocal, id, ledMsg] = msg.packet;
		if(receivedBytes == msg.buffer.size()){
			smp::StatusCode result {};
			if(msg.packet.msg.ledDevice != 0xF){
				result = operateWithOneLed(ledMsg.ledDevice, static_cast<smp::led_ops>(ledMsg.op));
			} else {
				result = operateWithAllLed(static_cast<smp::led_ops>(ledMsg.op));
			}
			smp::sendAnswer(answer, startWordLocal, id, smp::action::peripheral, result);
		}
	}
}

#ifdef __cplusplus
}
#endif


static smp::StatusCode ledOperation(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, smp::led_ops op){
	// TODO LED_Pin_All
	switch(op){
	case smp::led_ops::OFF:
		HAL_GPIO_WritePin(GPIOx, GPIO_Pin, GPIO_PIN_SET);
		break;
	case smp::led_ops::ON:
		HAL_GPIO_WritePin(GPIOx, GPIO_Pin, GPIO_PIN_RESET);
		break;
	case smp::led_ops::TOGGLE:
		HAL_GPIO_TogglePin(GPIOx, GPIO_Pin);
		break;
	default:
		return smp::NoSuchCommand;
	}
	return smp::Ok;
}

namespace{

std::array ledValues {
	std::pair{LED1_GPIO_Port, LED1_Pin},
	std::pair{LED2_GPIO_Port, LED2_Pin},
	std::pair{LED3_GPIO_Port, LED3_Pin},
	std::pair{LED4_GPIO_Port, LED4_Pin}
};

smp::StatusCode operateWithOneLed(uint8_t ledNumber, smp::led_ops op) noexcept
{
	if(ledNumber <= ledValues.size() && ledNumber > 0){
		auto [port, pin] = ledValues[ledNumber - 1];
		return ledOperation(port, pin, op);
	} else {
		return smp::NoSuchDevice;
	}
}

smp::StatusCode operateWithAllLed(smp::led_ops op) noexcept
{
	smp::StatusCode result{};
	std::for_each(ledValues.begin(), ledValues.end(), [&result, op](auto val){
		result = ledOperation(val.first, val.second, op);
	});
	return result;
}

}

