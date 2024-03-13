#include "audio_frame_ext.h"

namespace godot {

AudioFrame& operator+=(AudioFrame& lhs, const AudioFrame& rhs) {
    lhs.left += rhs.left;
    lhs.right += rhs.right;
    return lhs;
}

AudioFrame operator+(AudioFrame lhs, const AudioFrame& rhs)
{
    lhs += rhs;
    return lhs;
}

AudioFrame operator*(const AudioFrame& lhs, const AudioFrame& rhs)
{
    return {lhs.left * rhs.left, lhs.right * rhs.right};
}

AudioFrame operator*(const AudioFrame& lhs, float rhs)
{
    return {lhs.left * rhs, lhs.right * rhs};
}

}