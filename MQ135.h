#ifndef MQ135_WRAPPER_H
#define MQ135_WRAPPER_H

#include <Arduino.h>
#include <MQUnifiedsensor.h>

// Configuration defaults (override these before including this header if needed)
#ifndef MQ135_BOARD
#define MQ135_BOARD "Arduino MEGA 2560"
#endif

#ifndef MQ135_VOLTAGE_RESOLUTION
#define MQ135_VOLTAGE_RESOLUTION 5.0f
#endif

#ifndef MQ135_ADC_BIT_RESOLUTION
#define MQ135_ADC_BIT_RESOLUTION 10
#endif

#ifndef MQ135_ANALOG_PIN
#define MQ135_ANALOG_PIN A0
#endif

#ifndef MQ135_DIGITAL_PIN
#define MQ135_DIGITAL_PIN 3
#endif

#ifndef MQ135_RATIO_CLEAN_AIR
#define MQ135_RATIO_CLEAN_AIR 3.6f // Rs/R0 for clean air (typical)
#endif

// Load resistor value in kilo-ohms (depends on module; most MQ-135 boards use 10k)
#ifndef MQ135_RL
#define MQ135_RL 10.0f
#endif

class MQ135Sensor {
private:
	MQUnifiedsensor mq;
	int digitalPin;
	bool calibrated;

public:
	MQ135Sensor()
		: mq(MQ135_BOARD, MQ135_VOLTAGE_RESOLUTION, MQ135_ADC_BIT_RESOLUTION, MQ135_ANALOG_PIN, "MQ-135"),
			digitalPin(MQ135_DIGITAL_PIN),
			calibrated(false) {}

	// Perform basic initialization and calibration. Place the sensor in clean air.
	void begin(unsigned long calibrationSamples = 10, unsigned long sampleIntervalMs = 100) {
		pinMode(digitalPin, INPUT);
		mq.setRegressionMethod(1); // Exponential curve
		mq.init();
		mq.setRL(MQ135_RL);

		float r0 = 0.0f;
		for (unsigned long i = 0; i < calibrationSamples; i++) {
			mq.update();
			r0 += mq.calibrate(MQ135_RATIO_CLEAN_AIR);
			delay(sampleIntervalMs);
		}
		r0 /= (float)calibrationSamples;
		mq.setR0(r0);
		calibrated = true;
	}

	// Update the sensor reading (reads the analog voltage internally)
	void update() {
		mq.update();
	}

	// Convenience getters for different target gases (ppm). Choose the one you need.
	float readCO2() {
		// Common MQ-135 CO2 approximation curve
		mq.setA(110.47f); mq.setB(-2.862f);
		return mq.readSensor();
	}

	float readNH3() {
		mq.setA(102.2f); mq.setB(-2.473f);
		return mq.readSensor();
	}

	float readAlcohol() {
		mq.setA(77.255f); mq.setB(-3.18f);
		return mq.readSensor();
	}

	float readCO() {
		mq.setA(605.18f); mq.setB(-3.937f);
		return mq.readSensor();
	}

	float readToluene() {
		mq.setA(44.947f); mq.setB(-3.445f);
		return mq.readSensor();
	}

	float readAcetone() {
		mq.setA(34.668f); mq.setB(-3.369f);
		return mq.readSensor();
	}

	// Digital output from the module's onboard comparator (threshold adjustable via potentiometer)
	bool isAboveThreshold() const {
		return digitalRead(digitalPin) == HIGH;
	}

	bool isCalibrated() const { return calibrated; }
	float getR0() { return mq.getR0(); }

	int calculateOdorScore(int airQuality) {
		// MQ-135: Lower value = worse air quality
		// Convert 0-1023 → 0-100 score
		int aqScore = map(airQuality, 0, 1023, 0, 100);
		return aqScore;
	}

	String detectOdorType(int airquality, float humidity) {
		if (humidity > 80 && airquality < 300) return "LEMBAB";
		if (airquality < 200) return "ASAP";
		if (airquality > 800) return "FRESH";
		return "NORMAL";
	}
};

#endif // MQ135_WRAPPER_H

