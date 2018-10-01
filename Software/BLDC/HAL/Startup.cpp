#include "Startup.hpp"

#include "Logging.hpp"
#include "lowlevel.hpp"
#include "stm32f3xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "PowerADC.hpp"

#include "Tests.hpp"

void Start() {
	Log::Init(Log::Lvl::Dbg);

//	HAL::BLDC::Detector::Init();
	HAL::BLDC::PowerADC::Init();
	HAL::BLDC::LowLevel::Init();

	vTaskDelay(100);
	Log::Uart(Log::Lvl::Inf, "Start");

	vTaskDelay(500);
//	Test::PowerADC();
//	Test::ManualCommutation();
//	Test::InductanceSense();
	Test::MotorFunctions();
//	Test::MotorManualStart();
//	Test::TimerTest();
//	Test::SetMidPWM();
//	HAL::BLDC::Detector::Enable(nullptr);
//	Test::DifferentPWMs();
//	HAL::BLDC::Detector::Enable(HAL::BLDC::Detector::Phase::C, nullptr);
	while(1);
}
