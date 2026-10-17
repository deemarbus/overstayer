#pragma once

#include <array>

namespace ivory
{
struct BandParameters
{
    double preDb = 0.0;
    double postDb = 0.0;
    bool compensate = true;
};

// Independent design choice: the reference specifies 21 positions and endpoints,
// but not intermediate centres. These centres are logarithmic: 50 * 400^(i/20).
inline constexpr std::array<double, 21> presenceFrequencies {
    50.0, 67.4641423837, 91.0282101513, 122.8228026116,
    165.722700866999, 223.606797749979, 301.708816827258,
    407.090531536904, 549.280271653059, 741.134449106948,
    1000.0, 1349.282847673563, 1820.564203026079,
    2456.456052231581, 3314.454017339987, 4472.13595499958,
    6034.176336545163, 8141.810630738091, 10985.605433061178,
    14822.688982138954, 20000.0
};

struct Parameters
{
    double inputDb = 0.0;
    double outputDb = 0.0;
    double mix = 1.0;
    bool autoGain = true;
    bool toDry = false;
    bool dual = false;
    bool second = false;
    bool lowCut = false;
    bool lowShelf = false;
    bool highShelf = false;
    bool emphasis = true;
    bool extended = false;
    int classicMode = 0;
    int presence1 = 10;
    int presence2 = 14;
    bool hpOn = false;
    bool lpOn = false;
    double hpHz = 50.0;
    double hpRes = 0.0;
    double lpHz = 16000.0;
    double lpRes = 0.0;
    std::array<BandParameters, 4> bands {};
};
} // namespace ivory
