#include "Motor.hpp"

#include "cast.hpp"
#include "Logging.hpp"

Core::Motor::Motor(Sysinfo& s) {
	sys = &s;
	inState.mode = ControlMode::Off;
	inState.value = 0;

	xTaskCreate(pmf_cast<void (*)(void*), Motor, &Motor::ControlTask>::cfn,
			"MotCtrl", ControllerStack, this, ControllerPriority,
			&ControlHandle);
}

Core::Motor::~Motor() {
	vTaskDelete(ControlHandle);
}

void Core::Motor::NewData() {
	BaseType_t yield = false;
	xTaskNotifyFromISR(ControlHandle, 0x00, eNoAction, &yield);
	portYIELD_FROM_ISR(yield);
}

void Core::Motor::ControlTask() {
	lastExecutionTime = xTaskGetTickCount();
	InState in = inState;
	while (1) {
		vTaskDelayUntil(&lastExecutionTime, ControllerPeriod);
//		if (xTaskNotifyWait(0x00, 0x00, NULL, 0) == pdPASS) {
		auto data = sys->driver->GetData();
		vPortEnterCritical();
		outState.Current = data.current;
		outState.RPM = data.rpm;
		outState.Voltage = data.voltage;
		outState.Torque = 0;
		outState.Thrust = 0;
		in = inState;
		vPortExitCritical();
//		static uint32_t lastLog = 0;
//		if(xTaskGetTickCount() - lastLog > 250) {
//			lastLog = xTaskGetTickCount();
//			Log::Uart(Log::Lvl::Dbg, "I %ld, V %ld", outState.Current, outState.Voltage);
//		}
//		}
		switch(sys->driver->GetState()) {
		case HAL::BLDC::HALDriver::State::Running:
			switch (in.mode) {
			case ControlMode::Off:
				sys->driver->Stop();
				break;
			case ControlMode::Promille:
				if (in.value >= 0 && in.value <= 1000) {
					sys->driver->SetPWM(in.value);
				}
				break;
			}
			break;
		case HAL::BLDC::HALDriver::State::Stopped:
			switch (in.mode) {
			case ControlMode::Off:
				break;
			case ControlMode::Promille:
				sys->driver->InitiateStart();
				break;
			}
			break;
		case HAL::BLDC::HALDriver::State::Starting:
			if (in.mode == ControlMode::Off) {
				sys->driver->Stop();
			}
			break;
		case HAL::BLDC::HALDriver::State::Stopping:
			if (in.mode != ControlMode::Off) {
				sys->driver->InitiateStart();
			}
			break;
		}
	}
}
