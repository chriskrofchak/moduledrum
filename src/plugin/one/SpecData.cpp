// Display description of the Machinedrum's knobs (see SpecData.h).
#include "SpecData.h"
#include <algorithm>
#include <cstring>
#include "MachineRunner.h"

namespace md::uispec {

void synParams(const engine::MachineInfo* m, Param out[8])
{
    for (int k = 0; k < 8; ++k) {
        out[k] = kBlankParam;
        if (!m || m->params[size_t(k)].empty()) continue;
        const auto& label = m->params[size_t(k)];
        std::memcpy(out[k].label, label.data(), std::min(label.size(), sizeof out[k].label - 1));
        out[k].display = Display::Numeric;
        out[k].defaultRaw = uint8_t(std::min<int>(m->defaults[size_t(k)], 127));
    }
}

std::string machineGroup(const std::string& osName) { return osName.substr(0, std::min<size_t>(3, osName.size())); }
std::string machineShortName(const std::string& osName) { return osName.size() > 3 ? osName.substr(3) : std::string(); }

bool groupPlayable(const std::string& group)
{
    return group == "GND" || group == "TRX" || group == "EFM" || group == "E12" || group == "P-I";
}

const char* const kTrackNames[kNumTracks] = {"BD", "SD", "HT", "MT", "LT", "CP", "RS", "CB", "CH", "OH", "RC", "CC", "M1", "M2", "M3", "M4"};
const char* const kLfoTrackNames[kNumTracks] = {"01-BD", "02-SD", "03-HT", "04-MT", "05-LT", "06-CP", "07-RS", "08-CB",
                                                "09-CH", "10-OH", "11-RC", "12-CC", "13-M1", "14-M2", "15-M3", "16-M4"};

const Param kEffectsPage[8] = {
    {"AMD", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"AMF", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"EQF", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
    {"EQG", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    {"FLTF", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"FLTW", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
    {"FLTQ", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"SRR", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
};

const Param kRoutingPage[8] = {
    {"DIST", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"VOL", Display::Numeric, false, 100, 127, 128, Icons::None, nullptr},
    {"PAN", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    {"DEL", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"REV", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"LFOS", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"LFOD", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"LFOM", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
};

// The shapes as the cell draws them (Lcd.cpp, drawLfoShape)
const char* const kLfoShapeNames[kLfoShapes] = {"TRI", "SAW", "SQR", "RAMP", "EXP", "RAND"};
static const char* const kLfoUpdateNames[3] = {"FREE", "TRIG", "HOLD"};

const Param kLfoPage[8] = {
    {"TRACK", Display::Text, false, 0, kNumTracks - 1, kNumTracks, Icons::None, kLfoTrackNames},
    {"PARAM", Display::Text, false, 0, 23, 24, Icons::None, nullptr},   // named after the destination track's machine
    {"SHP1", Display::Shape, false, 0, kLfoShapes - 1, kLfoShapes, Icons::None, kLfoShapeNames},
    {"SHP2", Display::Shape, false, 0, kLfoShapes - 1, kLfoShapes, Icons::None, kLfoShapeNames},
    {"UPDTE", Display::Text, false, 0, 2, 3, Icons::None, kLfoUpdateNames},
    {"SPEED", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"DEPTH", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"SHMIX", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
};

} // namespace md::uispec
