#pragma once

#include "BLDCHAL.hpp"
#include "lowlevel.hpp"
#include <array>

namespace HAL {
namespace BLDC {
class Driver : public HALDriver {
public:
	Driver();
	~Driver();

	enum class State : uint8_t {
		None,
		Stopped,
		Align,
		Starting,
		Powered,
		Idle,
		Calibrating,
	};

	enum class Direction : uint8_t {
		Forward,
		Reverse,
	};

	void SetPWM(int16_t promille) override;

	void FreeRunning();
	void Stop();

	State GetState();


	void InitiateStart() override;

	void RegisterIncCallback(IncCallback c, void *ptr) override;
	void RegisterADCCallback(ADCCallback c, void *ptr) override;

	void ZeroCalibration();
	bool IsCalibrating();

	static void DMAComplete();
	static void DMAHalfComplete();

	void SetDirection(Direction d) {
		dir = d;
	}

private:
	static Driver *Inst;
	static constexpr uint32_t minPWM = 100;
	static constexpr uint32_t HzPWM = 20000;
	static constexpr uint32_t CommutationTimeoutms = 100;
	void NewPhaseVoltages(uint16_t *data);
	void SetStep(uint8_t step);
	void SetIdle();
	void IncRotorPos();

	std::array<uint16_t, 6> ZeroCal;

	State state, stateBuf;
	uint32_t cnt;

	LowLevel::Phase nPhaseHigh, nPhaseIdle;

	int8_t RotorPos;
	Direction dir;

};
}
}
