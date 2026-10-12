// Minimal WAV file writer (24-bit PCM). Ported from shnolk/monomodule src/core/util/Wav.{h,cpp} (AGPL-3.0).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace md::util
{
	// 24-bit PCM WAV from 24-bit signed integer samples (exact, the engine's own output words), channels interleaved.
	// Returns false when the file cannot be written.
	bool writeWav24(const std::string& _path, const std::vector<int32_t>& _interleaved24, int _channels, int _sampleRate);
}
