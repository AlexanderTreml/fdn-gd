#pragma once
#include <godot_cpp/classes/audio_frame.hpp>

namespace godot {

    AudioFrame& operator+=(AudioFrame& lhs, const AudioFrame& rhs);
    AudioFrame operator+(AudioFrame lhs, const AudioFrame& rhs);
    AudioFrame operator*(const AudioFrame& lhs, const AudioFrame& rhs);
    AudioFrame operator*(const AudioFrame& lhs, float rhs);

}