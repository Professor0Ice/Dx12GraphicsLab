#pragma once
#include "Common.hpp"

class Timer
{
public:
    Timer();
    void Reset();
    void Tick();
    float DeltaSeconds() const { return m_deltaSeconds; }
    float TotalSeconds() const { return m_totalSeconds; }

private:
    LARGE_INTEGER m_frequency{};
    LARGE_INTEGER m_previous{};
    float m_deltaSeconds = 0.0f;
    float m_totalSeconds = 0.0f;
};

