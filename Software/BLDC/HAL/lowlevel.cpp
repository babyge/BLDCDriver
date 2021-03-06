#include "lowlevel.hpp"

#include "stm32f3xx_hal.h"
#include "stm32f303x8.h"

static constexpr uint8_t PosFromMask(uint32_t mask) {
	uint8_t pos = 0;
	while (!(mask & 0x01)) {
		mask >>= 1;
		pos++;
	}
	return pos;
}

static constexpr uint32_t Channels[] = {TIM_CHANNEL_3, TIM_CHANNEL_2, TIM_CHANNEL_1};
static constexpr uint16_t Pins[] = { PosFromMask(PHASE_A_Pin), PosFromMask(
		PHASE_B_Pin), PosFromMask(PHASE_C_Pin) };
static constexpr GPIO_TypeDef *Ports[] = {PHASE_A_GPIO_Port, PHASE_B_GPIO_Port, PHASE_C_GPIO_Port};

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

constexpr uint16_t MaxPWM = 1600;
static uint16_t pwmVal;

void HAL::BLDC::LowLevel::Init() {
	__HAL_DBGMCU_FREEZE_TIM2();
	__HAL_DBGMCU_FREEZE_TIM6();
	__HAL_DBGMCU_FREEZE_TIM7();

	SetPhase(Phase::A, State::Idle);
	SetPhase(Phase::B, State::Idle);
	SetPhase(Phase::C, State::Idle);

	HAL_TIM_PWM_Start(&htim1, Channels[0]);
	HAL_TIM_PWM_Start(&htim1, Channels[1]);
	HAL_TIM_PWM_Start(&htim1, Channels[2]);

	HAL_TIM_Base_Start(&htim1);
	HAL_TIM_Base_Start(&htim2);
}

void HAL::BLDC::LowLevel::SetPWM(int16_t promille) {
	// directly modify PWM registers without HAL overhead
	pwmVal = (int32_t) promille * MaxPWM / 1000;
}

void HAL::BLDC::LowLevel::SetPhase(Phase p, State s) {
//	TIM_OC_InitTypeDef sConfigOC;
//	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_TypeDef *gpio = Ports[(int) p];
	uint16_t pin = Pins[(int) p];

	switch (s) {
	case State::High:
		// Enable alternate function (PWM generation)
		TIM1->CCR1 = pwmVal;
		TIM1->CCR2 = pwmVal;
		TIM1->CCR3 = pwmVal;
		gpio->MODER &= ~(0x01 << (pin * 2));
		gpio->MODER |= (0x02 << (pin * 2));
		break;
	case State::ConstLow:
		/* no break */
	case State::Low:
		// Set as low output
		gpio->MODER &= ~(0x02 << (pin * 2));
		gpio->MODER |= (0x01 << (pin * 2));
		gpio->BRR = (0x01 << pin);
		break;
	case State::Idle:
		// Set pin as input
		gpio->MODER &= ~(0x03 << (pin * 2));
		break;
	case State::ConstHigh:
		// Set as high output
		gpio->MODER &= ~(0x02 << (pin * 2));
		gpio->MODER |= (0x01 << (pin * 2));
		gpio->BSRR = (0x01 << pin);
		break;
	}
}

#include "Detector.hpp"
#include "PowerADC.hpp"

extern "C" {
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
	if(hadc->Instance == ADC1) {
		HAL::BLDC::Detector::DMAComplete();
	} else if(hadc->Instance == ADC2) {
		HAL::BLDC::PowerADC::DMAComplete();
	}
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
	if(hadc->Instance == ADC1) {
		HAL::BLDC::Detector::DMAHalfComplete();
	} else if(hadc->Instance == ADC2) {
		HAL::BLDC::PowerADC::DMAHalfComplete();
	}
}
}

