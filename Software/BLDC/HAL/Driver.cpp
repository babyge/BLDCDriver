#include "Driver.hpp"

#include "lowlevel.hpp"
#include "Detector.hpp"
#include "Timer.hpp"
#include "Logging.hpp"

#include "stm32f3xx_hal.h"
#include "stm32f303x8.h"
#include "InductanceSensing.hpp"

using namespace HAL::BLDC;

static Driver::IncCallback IncCB;
static void* IncPtr;

static uint8_t CommutationStep;

enum class State : uint8_t {
	Stopped,
	Starting,
	Running,
};

static State state;
static uint32_t StartTime;
static uint16_t StartSteps;

static constexpr uint32_t StartSequenceLength = 140000; //sizeof(StartSequence)/sizeof(StartSequence[0]);
static constexpr uint32_t StartFinalRPM = 800;
static constexpr uint8_t MotorPoles = 12;
static constexpr uint32_t StartMaxPeriod = 10000;
static constexpr uint16_t StartFinalPWM = 101;
static constexpr uint16_t StartMinPWM = 100;

static uint32_t StartSequence(uint32_t time) {
	constexpr uint32_t FinalPeriod = 1000000UL
			/ (StartFinalRPM * 6 * MotorPoles / 2 / 60);
	constexpr uint64_t dividend = FinalPeriod * StartSequenceLength;
	uint64_t period = dividend / time;
	if (time == 0 || period > StartMaxPeriod)
		return StartMaxPeriod;
	else
		return period;
}

static uint16_t StartPWM(uint32_t time) {
	constexpr uint16_t divisor = StartSequenceLength / (StartFinalPWM - StartMinPWM);
	return StartMinPWM + time / divisor;
}

static uint32_t timeBetweenCommutations;

static void CrossingCallback(uint32_t usSinceLast, uint32_t timeSinceCrossing);
static void SetStep(uint8_t step) {
//	HAL_GPIO_WritePin(TRIGGER_GPIO_Port, TRIGGER_Pin, GPIO_PIN_SET);
//	HAL_GPIO_TogglePin(TRIGGER_GPIO_Port, TRIGGER_Pin);
	switch (step) {
	case 0:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::High);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Low);
		Detector::SetPhase(Detector::Phase::B, false);
		break;
	case 1:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::High);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Low);
		Detector::SetPhase(Detector::Phase::A, true);
		break;
	case 2:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Low);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::High);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);
		Detector::SetPhase(Detector::Phase::C, false);
		break;
	case 3:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Low);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::High);
		Detector::SetPhase(Detector::Phase::B, true);
		break;
	case 4:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Low);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::High);
		Detector::SetPhase(Detector::Phase::A, false);
		break;
	case 5:
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::High);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Low);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);
		Detector::SetPhase(Detector::Phase::C, true);
		break;
	}

	if (state == State::Running) {
		Timer::Schedule(timeBetweenCommutations * 5,
				[]() {
					Detector::Disable();
					LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
					LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
					LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);

					state = State::Stopped;
					Log::Uart(Log::Lvl::Err, "Timed out while waiting for commutation, motor stopped");
				});
	}
}

static void NextStartStep() {
//	Log::Uart(Log::Lvl::Dbg, "Start step %d", StartStep);
	CommutationStep = (CommutationStep + 1) % 6;
	Detector::Disable();
	SetStep(CommutationStep);
	if(state == State::Running) {
		// Successfully started, enter normal operation mode
		Detector::Enable(CrossingCallback);
		return;
	}
	// Schedule next start step
	auto length = StartSequence(StartTime);
	LowLevel::SetPWM(StartPWM(StartTime));
	StartTime += length;

	if(StartSteps >= 10) {
		Detector::Enable(CrossingCallback, 10);
	}
	StartSteps++;

	if (StartTime >= StartSequenceLength) {
		// TODO Start attempt failed
		Detector::Disable();
		Log::Uart(Log::Lvl::Err, "Failed to start motor");
		LowLevel::SetPWM(0);
		LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
		LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);
		state = State::Stopped;
		return;
	}

	Timer::Schedule(length, NextStartStep);
}

static void CrossingCallback(uint32_t usSinceLast, uint32_t timeSinceCrossing) {
//	HAL_GPIO_WritePin(TRIGGER_GPIO_Port, TRIGGER_Pin, GPIO_PIN_RESET);
	if (state == State::Starting) {
		state = State::Running;
		Log::Uart(Log::Lvl::Inf, "Motor started after %luus", StartTime);
//		Detector::Disable();
//		timeBetweenCommutations = StartSequence(StartTime);
//		return;
		// abort next scheduled start step
		Timer::Abort();
		LowLevel::SetPWM(100);
		// no previous commutation known, take a guess from the start sequence
		usSinceLast = StartSequence(StartTime);
//		timeSinceCrossing = StartSequence(StartTime) / 2;
	} else if (IncCB) {
		// motor is already running, report back crossing intervals to controller
		IncCB(IncPtr, usSinceLast);
	}

	// Disable detector until next commutation step
	Detector::Disable();
	CommutationStep = (CommutationStep + 1) % 6;
	// Calculate time until next 30° rotation
	uint32_t TimeToNextCommutation = usSinceLast / 2;// - timeSinceCrossing;
	Timer::Schedule(TimeToNextCommutation, []() {
		SetStep(CommutationStep);
		Detector::Enable(CrossingCallback);
	});
//	Log::Uart(Log::Lvl::Inf, "next comm in %luus", TimeToNextCommutation);

	timeBetweenCommutations = usSinceLast;
}

HAL::BLDC::Driver::Driver() {
	LowLevel::SetPWM(0);
	LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
	LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
	LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);

	Detector::Disable();

	CommutationStep = 0;

	state = State::Stopped;
}

void HAL::BLDC::Driver::SetPWM(int16_t promille) {
	LowLevel::SetPWM(promille);
}

void HAL::BLDC::Driver::InitiateStart() {
	Log::Uart(Log::Lvl::Dbg, "Initiating start sequence");
	state = State::Starting;
	uint8_t sector = InductanceSensing::RotorPosition();
	StartTime = 0;
	StartSteps = 0;
	if (sector == 0) {
		// unable to determine rotor position, use align and go
		LowLevel::SetPWM(30);
		Detector::Disable();
		SetStep(CommutationStep);
		Timer::Schedule(1000000, NextStartStep);
	} else {
		// rotor position determined, modify next commutation step accordingly
		CommutationStep = (7 - sector) % 6;
		NextStartStep();
	}
}

void HAL::BLDC::Driver::RegisterIncCallback(IncCallback c, void* ptr) {
	IncCB = c;
	IncPtr = ptr;
}

void HAL::BLDC::Driver::RegisterADCCallback(ADCCallback c, void* ptr) {
}

static void Idle() {
	LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Idle);
	LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Idle);
	LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Idle);
	LowLevel::SetPWM(0);
}

void HAL::BLDC::Driver::FreeRunning() {
	Timer::Abort();
	Idle();
	state = State::Stopped;
}

void HAL::BLDC::Driver::Stop() {
	Timer::Abort();
	LowLevel::SetPhase(LowLevel::Phase::A, LowLevel::State::Low);
	LowLevel::SetPhase(LowLevel::Phase::B, LowLevel::State::Low);
	LowLevel::SetPhase(LowLevel::Phase::C, LowLevel::State::Low);
	state = State::Stopped;
	Timer::Schedule(500000, Idle);
}
