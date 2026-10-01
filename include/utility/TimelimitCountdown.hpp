//
// Created by Laura on 6/30/26.
//

#ifndef GISCUPBONN_TIMELIMITCOUNTDOWN_HPP
#define GISCUPBONN_TIMELIMITCOUNTDOWN_HPP

#include <chrono>

// Class for easy timelimit and progress tracking

class Countdown {
public:
    Countdown(double total_ms)
        : start(std::chrono::steady_clock::now())
        , total_ms(total_ms)
    {}

    double remaining_ms() const {
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        return std::max(0.0, total_ms - elapsed_ms);
    }

    double passed_ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    double progress() const {
        return std::min(1.0 - remaining_ms() / total_ms, 1.0);
    }

    bool expired() const {
        return remaining_ms() == 0.0;
    }

private:
    std::chrono::steady_clock::time_point start;
    double total_ms;
};


#endif // GISCUPBONN_TIMELIMITCOUNTDOWN_HPP