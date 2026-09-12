#include "Timer.hpp"

Timer::Timer()
{
    QueryPerformanceFrequency(&m_frequency);
    Reset();
}

void Timer::Reset()
{
    QueryPerformanceCounter(&m_previous);
    m_deltaSeconds = 0.0f;
    m_totalSeconds = 0.0f;
}

void Timer::Tick()
{
    LARGE_INTEGER current{};
    QueryPerformanceCounter(&current);
    m_deltaSeconds = static_cast<float>(current.QuadPart - m_previous.QuadPart) /
                     static_cast<float>(m_frequency.QuadPart);
    m_deltaSeconds = std::min(m_deltaSeconds, 0.1f);
    m_totalSeconds += m_deltaSeconds;
    m_previous = current;
}

