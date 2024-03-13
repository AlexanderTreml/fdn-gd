#pragma once

#include <array>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

template <typename T, size_t N>
class DelayLine {
private:
    std::array<T, N> buffer;
    size_t pos;

public:
    DelayLine() : buffer{}, pos(0) {}

	// TODO optimize bulk insertion
	// TODO linearize memory for vector access (double write method)
    void push(const T& item) {
        buffer[pos] = item;
        pos = (pos + 1) % N;
    }

    const T& operator[](size_t index) const {
        return buffer[(pos + N - index - 1) % N];
    }

	T& operator[](size_t index) {
        return buffer[(pos + N - index - 1) % N];
    }
};

class FDN;

// TODO the max delay is dependent on the sample rate
// depending on how the FDN is configured this might be an issue in practice
// TODO also figure out sensible ranges and adjust property hints accordingly
static const size_t MAX_PREDELAY_SAMPLES = 9600; // 200ms at 48000Hz
static const size_t MAX_DELAY_SAMPLES = 48000; // 1s at 48000Hz
static const size_t BUFFER_SIZE = 512; // TODO This is hardcoded in the AudioServer, but this might change in the future.
static const size_t BLOCK_SIZE = 8; // Block size for vectorized processing.

class FDNInstance : public AudioEffectInstance {
	GDCLASS(FDNInstance, AudioEffectInstance)

    friend class FDN;
	Ref<FDN> base;

	std::vector<int> loop_delays; // in samples
	// TODO use filters instead
	std::vector<float> loop_gains;
	std::vector<std::vector<float>> mixing_matrix;

	// +1 for current sample
	DelayLine<AudioFrame, MAX_PREDELAY_SAMPLES + BUFFER_SIZE + 1> pre_delay_line;
	std::vector<DelayLine<float, MAX_DELAY_SAMPLES + 1>> delay_line_bufs;

	// Processing buffers
	std::vector<std::array<float, BLOCK_SIZE>> delayed_buf; 
	std::vector<std::array<float, BLOCK_SIZE>> mixed_buf; 

	void build_hadamard_matrix(int N);
	void calc_mixing_matrix();
	void calc_delays();
	void calc_gains();

protected:
	static void _bind_methods();

public:
	virtual void _process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) override;
};

class FDN : public AudioEffect {
	GDCLASS(FDN, AudioEffect)

	friend class FDNInstance;

	// TODO find out how to mix the output down to two channels
	int delay_lines = 4;

	float dry = 1.0f;
	float pre_delay_ms = 0;
	float pre_gain = 0;

	float t_60 = 1;

protected:
	static void _bind_methods();

public:
	void set_delay_lines(int p_delay_lines);
	int get_delay_lines() const;

	void set_dry(float p_dry);
	float get_dry() const;

	void set_pre_delay_ms(float p_pre_delay);
	float get_pre_delay_ms() const;

	void set_pre_gain(float p_pre_gain);
	float get_pre_gain() const;

	void set_t_60(float p_t_60);
	float get_t_60() const;

	Ref<AudioEffectInstance> _instantiate() override;

	FDN() {}
};

}