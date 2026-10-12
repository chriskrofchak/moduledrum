// The host-rate converter (src/core/dsp/Resampler): exact frame bookkeeping, unity DC gain, clean sines, alias and
// image rejection, the sample history. Needs no OS file. From shnolk/monomodule tests/test_resampler.cpp (AGPL-3.0).
#include "mini_test.h"
#include <cmath>
#include <random>
#include <vector>
#include "dsp/Resampler.h"
using namespace md::dsp;

// The pull scheme the plugin uses: output m is the band-limited input at position m * inRate / outRate.
static std::vector<double> convert(const std::vector<float>& x, double inRate, double outRate, int outCount)
{
    SincKernel k;
    k.design(inRate, outRate);
    RateMap map;
    map.set(inRate, outRate);
    History h;
    h.prepare(int(x.size()) + 64);
    h.push(x.data(), int(x.size()));
    std::vector<float> taps(size_t(k.numTaps()));
    std::vector<double> y;
    for (int m = 0; m < outCount; ++m) {
        const auto at = map.at(m);
        y.push_back(h.dot(at.index - k.halfTaps() + 1, k.taps(at, taps.data()), k.numTaps()));
    }
    return y;
}

static std::vector<float> sine(double hz, double rate, int n)
{
    std::vector<float> v(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) v[size_t(i)] = float(std::sin(2.0 * M_PI * hz * i / rate));
    return v;
}

// error of a converted sine against the ideal one at the output rate (dB re the signal), away from the edges
static double sineErrorDb(double hz, double inRate, double outRate)
{
    const int n = 16384;
    const int out = int(n * outRate / inRate) - 256;
    const auto y = convert(sine(hz, inRate, n), inRate, outRate, out);
    double e = 0, s = 0;
    for (int m = 512; m < out; ++m) {
        const double ideal = std::sin(2.0 * M_PI * hz * m / outRate);
        e += (y[size_t(m)] - ideal) * (y[size_t(m)] - ideal);
        s += ideal * ideal;
    }
    return 10.0 * std::log10(e / s);
}

TEST_CASE(resampler_ratemap_is_exact)
{
    // 48 kHz host: 160 host samples are exactly 147 engine frames, for ever
    RateMap engineAt;
    engineAt.set(44100.0, 48000.0);
    for (int64_t k : {int64_t(0), int64_t(1), int64_t(1000), int64_t(123456789)}) {
        const auto p = engineAt.at(k * 160);
        CHECK_EQ(p.index, k * 147);
        CHECK(p.frac == 0.0 && p.phase == 0);
    }
    auto p = engineAt.at(1);
    CHECK_EQ(p.index, int64_t(0));
    CHECK_EQ(p.phase, int64_t(147));
    CHECK(std::abs(p.frac - 147.0 / 160.0) < 1e-15);
    p = engineAt.at(-1);   // before the start: floor, not truncation
    CHECK_EQ(p.index, int64_t(-1));
    CHECK_EQ(p.phase, int64_t(13));
    CHECK_EQ(engineAt.ceilAt(160), int64_t(147));
    CHECK_EQ(engineAt.ceilAt(161), int64_t(148));
    CHECK_EQ(engineAt.floorAt(161), int64_t(147));
    // the frames a host's blocks need add up to the same count however the blocks are cut (no rounding per block)
    std::mt19937 rng(7);
    for (double rate : {48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 32000.0, 22050.0}) {
        RateMap m;
        m.set(44100.0, rate);
        int64_t host = 0, frames = 0;
        for (int b = 0; b < 2000; ++b) {
            const int64_t next = host + 1 + int64_t(rng() % 1024);
            frames += m.ceilAt(next) - m.ceilAt(host);
            host = next;
        }
        CHECK_EQ(frames, m.ceilAt(host));
        CHECK_EQ(m.ceilAt(host), int64_t(std::ceil(double(host) * 44100.0 / rate - 1e-9)));
    }
}

TEST_CASE(resampler_taps_sum_to_one_and_mirror)
{
    for (double out : {48000.0, 96000.0, 192000.0, 32000.0}) {
        SincKernel k;
        k.design(44100.0, out);
        std::vector<float> a(size_t(k.numTaps())), b(size_t(k.numTaps()));
        for (double f : {0.0, 0.1, 0.25, 0.5, 0.731, 0.999}) {
            k.taps(f, a.data());
            double s = 0;
            for (float v : a) s += v;
            CHECK_MSG(std::abs(s - 1.0) < 1e-6, "sum " << s << " at " << f);
            if (f > 0.0) {   // fraction f from the left = 1 - f from the right
                k.taps(1.0 - f, b.data());
                for (int j = 0; j < k.numTaps(); ++j) CHECK(std::abs(a[size_t(j)] - b[size_t(k.numTaps() - 1 - j)]) < 2e-6f);
            }
        }
    }
    SincKernel k;
    k.design(44100.0, 48000.0);
    CHECK_EQ(k.halfTaps(), 35);   // 70 taps at 44.1 kHz: 100 dB from 24.1 kHz
    // the stored taps of every phase are exactly the ones worked out on demand
    RateMap map;
    map.set(44100.0, 48000.0);
    std::vector<float> scratch(size_t(k.numTaps())), direct(size_t(k.numTaps()));
    for (int64_t m = 0; m < 160; ++m) {
        const auto pos = map.at(m);
        const float* stored = k.taps(pos, scratch.data());
        CHECK(stored != scratch.data());
        k.taps(pos.frac, direct.data());
        for (int j = 0; j < k.numTaps(); ++j) CHECK(stored[j] == direct[size_t(j)]);
    }
}

TEST_CASE(resampler_sines_are_clean)
{
    // the engine -> host direction and back, across the band: better than -100 dB up to 19.5 kHz
    for (double out : {48000.0, 96000.0})
        for (double hz : {100.0, 1000.0, 10000.0, 19500.0}) {
            const double up = sineErrorDb(hz, 44100.0, out), down = sineErrorDb(hz, out, 44100.0);
            CHECK_MSG(up < -100.0, hz << " Hz 44.1 -> " << out << ": " << up << " dB");
            CHECK_MSG(down < -100.0, hz << " Hz " << out << " -> 44.1: " << down << " dB");
        }
}

TEST_CASE(resampler_rejects_aliases)
{
    // a 30 kHz tone at 96 kHz would fold to 14.1 kHz at 44.1 kHz: it must be gone (the band edge is 24.1 kHz)
    const int n = 32768;
    const auto y = convert(sine(30000.0, 96000.0, n), 96000.0, 44100.0, int(n * 44100.0 / 96000.0) - 256);
    double e = 0;
    for (size_t m = 512; m < y.size(); ++m) e += y[m] * y[m];
    const double db = 10.0 * std::log10(e / (0.5 * double(y.size() - 512)));
    CHECK_MSG(db < -98.0, "alias at " << db << " dB");
    // images: a 19 kHz tone from 44.1 to 48 kHz leaves nothing at its image (25.1 kHz folds to 22.9 kHz)
    const auto z = convert(sine(19000.0, 44100.0, n), 44100.0, 48000.0, int(n * 48000.0 / 44100.0) - 256);
    double i19 = 0, q19 = 0, i22 = 0, q22 = 0;
    for (size_t m = 512; m < z.size(); ++m) {
        const double w = std::sin(M_PI * double(m - 512) / double(z.size() - 512));   // window
        i19 += z[m] * w * std::cos(2 * M_PI * 19000.0 * double(m) / 48000.0); q19 += z[m] * w * std::sin(2 * M_PI * 19000.0 * double(m) / 48000.0);
        i22 += z[m] * w * std::cos(2 * M_PI * 22900.0 * double(m) / 48000.0); q22 += z[m] * w * std::sin(2 * M_PI * 22900.0 * double(m) / 48000.0);
    }
    const double image = 10.0 * std::log10((i22 * i22 + q22 * q22) / (i19 * i19 + q19 * q19));
    CHECK_MSG(image < -98.0, "image at " << image << " dB");
}

TEST_CASE(resampler_history_window)
{
    History h;
    h.prepare(64);   // capacity 64
    std::vector<float> taps(10, 1.0f), x(200);
    for (int i = 0; i < 200; ++i) x[size_t(i)] = float(i + 1);
    CHECK(h.dot(-5, taps.data(), 10) == 0.0f);   // nothing yet
    h.push(x.data(), 3);
    CHECK(h.dot(-5, taps.data(), 10) == 1.0f + 2.0f + 3.0f);   // indices < 0 read 0
    h.push(x.data() + 3, 197);
    CHECK_EQ(h.end(), int64_t(200));
    for (int64_t first : {int64_t(140), int64_t(150), int64_t(189), int64_t(190)}) {   // windows across the ring's wrap
        float want = 0;
        for (int k = 0; k < 10; ++k) want += float(first + k + 1);
        CHECK_EQ(h.dot(first, taps.data(), 10), want);
    }
}
