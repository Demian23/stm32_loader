#include <FlashWriter.h>
#include <stm32f4xx_hal.h>
#include <stm32f4xx_hal_flash.h>
#include <stm32f4xx_hal_flash_ex.h>

FlashWriter::FlashWriter() {

}

FlashWriter::~FlashWriter() {
}

FlashWriter::ResCode FlashWriter::write(const uint8_t* buffer, uint32_t size) noexcept
{
	static FLASH_EraseInitTypeDef  eraseInfo;
	uint32_t sectorError{};
	ResCode result = ResCode::Ok;
	constexpr uint32_t fifthSectorSize = 128 * 1024;
	if(size < fifthSectorSize){
		eraseInfo.VoltageRange = FLASH_VOLTAGE_RANGE_3;
		eraseInfo.TypeErase = FLASH_TYPEERASE_SECTORS;
		eraseInfo.Sector = FLASH_SECTOR_5;
		eraseInfo.NbSectors = 1;
		if(HAL_FLASH_Unlock() == HAL_OK){
			if(HAL_FLASHEx_Erase(&eraseInfo, &sectorError) == HAL_OK){
				for(uint32_t i = 0; i < size; i++){
					auto res = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, baseWriteAddress + i, buffer[i]);
					bool written = res == HAL_OK && *reinterpret_cast<uint8_t*>(baseWriteAddress + i) == buffer[i];
					if(!written){
						result = ResCode::CantWrite;
					}
				}
			} else {
				result = ResCode::FlashBusy;
			}
			HAL_FLASH_Lock();
		} else {
			result = ResCode::AlreadyUnlocked;
		}
	} else {
		result = ResCode::ErasingMoreThanOneSector;
	}
	return result;
}
