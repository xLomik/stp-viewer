// Limite de tiempo para el mallado. El Explorador no puede quedarse esperando:
// pasado el plazo se devuelve la geometria construida hasta ese momento.
#pragma once

#include <chrono>

namespace stp {

class Budget {
public:
    Budget() = default;
    explicit Budget(int milliseconds)
        : m_limited(milliseconds > 0),
          m_deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds)) {}

    bool expired() const {
        return m_limited && std::chrono::steady_clock::now() >= m_deadline;
    }

private:
    bool m_limited = false;
    std::chrono::steady_clock::time_point m_deadline;
};

}  // namespace stp
