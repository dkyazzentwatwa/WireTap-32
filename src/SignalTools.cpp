#include "SignalTools.h"

bool signalIsValidGpio(int pin) {
    return pin >= 0 && pin <= 39;
}

bool signalIsSafeDigitalPin(int pin) {
    return signalIsValidGpio(pin) && !(pin >= 6 && pin <= 11);
}

bool signalIsOutputCapablePin(int pin) {
    return signalIsSafeDigitalPin(pin) && !(pin >= 34 && pin <= 39);
}

bool signalIsAdcCapablePin(int pin) {
    static const int adcPins[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    for(size_t i = 0; i < sizeof(adcPins) / sizeof(adcPins[0]); i++) {
        if(pin == adcPins[i]) return true;
    }
    return false;
}

SignalFrequencyResult signalMeasureFrequency(int pin, uint32_t windowMs) {
    SignalFrequencyResult result = {};
    if(!signalIsSafeDigitalPin(pin)) return result;

    windowMs = constrain(windowMs, (uint32_t)10, (uint32_t)5000);
    pinMode(pin, INPUT);

    bool state = digitalRead(pin) == HIGH;
    bool lastState = state;
    uint32_t start = micros();
    uint32_t lastChange = start;
    uint32_t highUs = 0;
    uint32_t lowUs = 0;

    if(state) result.highSegments = 1;
    else result.lowSegments = 1;

    while((uint32_t)(micros() - start) < windowMs * 1000UL) {
        state = digitalRead(pin) == HIGH;
        if(state != lastState) {
            uint32_t now = micros();
            uint32_t span = now - lastChange;
            if(lastState) {
                highUs += span;
                result.fallingEdges++;
                result.lowSegments++;
            } else {
                lowUs += span;
                result.risingEdges++;
                result.highSegments++;
            }
            lastState = state;
            lastChange = now;
        }
        yield();
    }

    uint32_t end = micros();
    uint32_t tail = end - lastChange;
    if(lastState) highUs += tail;
    else lowUs += tail;

    result.valid = true;
    result.durationUs = end - start;
    result.avgHighUs = result.highSegments ? highUs / result.highSegments : 0;
    result.avgLowUs = result.lowSegments ? lowUs / result.lowSegments : 0;
    result.frequencyHz = result.durationUs ? (result.risingEdges * 1000000.0f) / result.durationUs : 0.0f;
    result.dutyPct = result.durationUs ? (highUs * 100.0f) / result.durationUs : 0.0f;
    return result;
}

SignalEdgeResult signalCountEdges(int pin, uint32_t windowMs) {
    SignalEdgeResult result = {};
    if(!signalIsSafeDigitalPin(pin)) return result;

    windowMs = constrain(windowMs, (uint32_t)10, (uint32_t)10000);
    pinMode(pin, INPUT);

    bool lastState = digitalRead(pin) == HIGH;
    result.startedHigh = lastState;
    uint32_t start = micros();

    while((uint32_t)(micros() - start) < windowMs * 1000UL) {
        bool state = digitalRead(pin) == HIGH;
        if(state != lastState) {
            if(state) result.risingEdges++;
            else result.fallingEdges++;
            lastState = state;
        }
        yield();
    }

    result.valid = true;
    result.durationUs = micros() - start;
    result.endedHigh = lastState;
    return result;
}

bool signalBuildScope(int pin, uint16_t samples, uint32_t intervalUs, String& wave, String& ruler) {
    if(!signalIsSafeDigitalPin(pin)) return false;

    samples = constrain(samples, (uint16_t)1, (uint16_t)160);
    intervalUs = max(intervalUs, (uint32_t)1);
    pinMode(pin, INPUT);

    wave = "";
    ruler = "";
    wave.reserve(samples + 1);
    ruler.reserve(samples + 1);

    uint16_t markEvery = max((uint16_t)1, (uint16_t)(samples / 10));
    for(uint16_t i = 0; i < samples; i++) {
        wave += digitalRead(pin) ? '-' : '_';
        ruler += (i % markEvery == 0) ? '|' : '.';
        delayMicroseconds(intervalUs);
    }
    return true;
}

SignalAdcStats signalMeasureAdc(int pin, uint16_t samples) {
    SignalAdcStats result = {};
    if(!signalIsAdcCapablePin(pin)) return result;

    samples = constrain(samples, (uint16_t)1, (uint16_t)512);
    analogSetAttenuation(ADC_11db);

    uint32_t sum = 0;
    result.minRaw = 4095;
    result.maxRaw = 0;
    for(uint16_t i = 0; i < samples; i++) {
        int raw = analogRead(pin);
        result.minRaw = min(result.minRaw, raw);
        result.maxRaw = max(result.maxRaw, raw);
        sum += raw;
        delay(1);
    }

    result.valid = true;
    result.samples = samples;
    result.avgRaw = (float)sum / samples;
    result.minVoltage = result.minRaw * 3.3f / 4095.0f;
    result.maxVoltage = result.maxRaw * 3.3f / 4095.0f;
    result.avgVoltage = result.avgRaw * 3.3f / 4095.0f;
    return result;
}

bool signalStartPwm(int pin, uint32_t freq, uint8_t dutyPct, String& error) {
    if(!signalIsOutputCapablePin(pin)) {
        error = "Invalid or input-only pin for PWM";
        return false;
    }

    freq = constrain(freq, (uint32_t)1, (uint32_t)40000000);
    dutyPct = constrain(dutyPct, (uint8_t)0, (uint8_t)100);
    if(!ledcAttach((uint8_t)pin, freq, 8)) {
        error = "LEDC attach failed";
        return false;
    }

    uint32_t duty = (uint32_t)dutyPct * 255 / 100;
    if(!ledcWrite((uint8_t)pin, duty)) {
        error = "LEDC duty write failed";
        return false;
    }
    return true;
}
