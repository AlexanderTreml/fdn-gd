#include "fdn.h"
#include "audio_frame_ext.h"
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <string>
#include <sstream>

using namespace godot;

void FDNInstance::build_hadamard_matrix(int N) {
    if (N == 1) {
        mixing_matrix[0][0] = 1.0f / ceil(sqrt(base->delay_lines));
        return;
    }

    int M = N / 2;
    build_hadamard_matrix(M);

    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < M; ++j) {
			mixing_matrix[i + M][j] = mixing_matrix[i][j];
			mixing_matrix[i][j + M] = mixing_matrix[i][j];
			mixing_matrix[i + M][j + M] = -mixing_matrix[i][j];
        }
    }
}

// I use a hadamard matrix, which is easy to construct for dimensions that are a power of 2
void FDNInstance::calc_mixing_matrix() {
	int N = base->delay_lines;
	mixing_matrix.resize(N, std::vector<float>(N, 0.0f));

	build_hadamard_matrix(N);
}

// TODO configuration with mean free path and absorption coefficients will be needed
// currently t_60 is used as a simpler alternative.
void FDNInstance::calc_delays() {
	int N = base->delay_lines;
	float t_60 = base->t_60;
	int fs = AudioServer::get_singleton()->get_mix_rate();

	// Minimum sum of delays (modal density requirement)
	float delay_sum = 0.15f * t_60 * fs;

	float delay_avg = delay_sum / N;

	// Calculate the first N prime numbers
	std::vector<int> p_i;
	for (int num = 2; p_i.size() < N; ++num) {
		bool is_prime = true;
		for (int prime : p_i) {
			if (num % prime == 0) {
				is_prime = false;
				break;
			}
		}
		if (is_prime) p_i.push_back(num);
	}

	// Calculate exponents
	std::vector<int> m_i;
	for (int prime : p_i) {
		m_i.push_back(static_cast<int>(std::round(std::log(delay_avg) / std::log(prime))));
	}

	// Calculate delays as powers of primes
	loop_delays.clear();
	for (int i = 0; i < N; ++i) {
		loop_delays.push_back(static_cast<int>(std::pow(p_i[i], m_i[i])));
	}
}

void FDNInstance::calc_gains() {
	int N = base->delay_lines;
	float t_60 = base->t_60;
	int fs = AudioServer::get_singleton()->get_mix_rate();

	// Calculate gains based on the formula g = 10^((-3 * M) / (fs * t60))
	loop_gains.clear();
	for (int i = 0; i < N; ++i) {
		float gain = std::pow(10, (-3 * loop_delays[i]) / (fs * t_60));
		loop_gains.push_back(gain);
	}
}

void FDNInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	// TODO make sure that only valid configiurations are possible 
	const AudioFrame* src_frames = static_cast<const AudioFrame *>(src_buffer);

	float mix_rate = AudioServer::get_singleton()->get_mix_rate();
	unsigned int pre_delay_frames = int((base->pre_delay_ms / 1000.0) * mix_rate);
	
	float pre_gain_linear = UtilityFunctions::db_to_linear(base->pre_gain);
	float dry_gain = base->dry;

	// Apply pre-delay
	for (int i = 0; i < frame_count; ++i) {
		pre_delay_line.push(src_frames[i]);
	}

	for (int i = 0; i < frame_count; i += BLOCK_SIZE) {
		for (size_t line_idx = 0; line_idx < base->delay_lines; ++line_idx) {
			// TODO can not vectorize here because of disconnected memory
			// Retrieve delay line outputs
            for (size_t j = 0; j < BLOCK_SIZE; ++j) {
				delayed_buf[line_idx][j] = delay_line_bufs[line_idx][loop_delays[line_idx] - j];
			}

			// TODO this should be filtered instead of having a flat gain
			// Apply decay
			for (size_t j = 0; j < BLOCK_SIZE; ++j) {
				delayed_buf[line_idx][j] *= loop_gains[line_idx];
			}

			// Add to output
			for (size_t j = 0; j < BLOCK_SIZE; ++j) {
				dst_buffer[i + j] = src_frames[i + j] * dry_gain;
				// TODO mix properly to retain signal energy
				dst_buffer[i + j].right += delayed_buf[0][j];
				dst_buffer[i + j].left += delayed_buf[1][j];
			}
        }

		// Apply mixing matrix
		// Optimization would be especially beneficial here
		for (size_t dst_line = 0; dst_line < base->delay_lines; ++dst_line) {
			for (size_t j = 0; j < BLOCK_SIZE; ++j) {
				mixed_buf[dst_line][j] = 0;
				for (size_t src_line = 0; src_line < base->delay_lines; ++src_line) {
					float factor = mixing_matrix[dst_line][src_line];		
					float src = delayed_buf[src_line][j];
					mixed_buf[dst_line][j] += src * factor;
				}
			}
		}

		// TODO this would be a vectorized version, but it would only work on architectures that support AVX
		/*
		for (size_t dst_line = 0; dst_line < base->delay_lines; ++dst_line) {
			__m256 result = _mm256_setzero_ps();
			for (size_t src_line = 0; src_line < base->delay_lines; ++src_line) {
				// TODO alignment could increase preformance (this uses the unaligned load instruction)
				__m256 src = _mm256_loadu_ps(&(delayed_buf[src_line][0]));
				__m256 factor = _mm256_set1_ps(mixing_matrix[dst_line][src_line]);
				_mm256_fmadd_ps(src, factor, result);
			}

			_mm256_storeu_ps(&(mixed_buf[dst_line][0]), result);
		}
		*/

		// Add new input
		for (size_t j = 0; j < BLOCK_SIZE; ++j) {
			AudioFrame input = pre_delay_line[pre_delay_frames + BUFFER_SIZE - i - j] * pre_gain_linear;
			mixed_buf[0][j] += input.right;
			mixed_buf[1][j] += input.left;
		}

		// Insert into delay lines
		for (size_t line_idx = 0; line_idx < base->delay_lines; ++line_idx) {
			for (size_t j = 0; j < BLOCK_SIZE; ++j) {
				delay_line_bufs[line_idx].push(mixed_buf[line_idx][j]);
			}
		}
	}
}

void FDNInstance::_bind_methods() {}

Ref<AudioEffectInstance> FDN::_instantiate() {
    Ref<FDNInstance> ins;
	ins.instantiate();

	ins->base = Ref<FDN>(this);

	ins->calc_mixing_matrix();
	ins->calc_delays();
	ins->calc_gains();

	ins->delay_line_bufs.resize(delay_lines, DelayLine<float, MAX_DELAY_SAMPLES + 1>());
	ins->delayed_buf.resize(delay_lines, std::array<float, BLOCK_SIZE>{});
	ins->mixed_buf.resize(delay_lines, std::array<float, BLOCK_SIZE>{});

	return ins;
}

void FDN::set_delay_lines(int p_delay_lines) {
	if (p_delay_lines < delay_lines) {
		delay_lines = previous_power_of_2(p_delay_lines);
	} else {
		delay_lines = next_power_of_2(p_delay_lines);
	}
}

int FDN::get_delay_lines() const {
	return delay_lines;
}

void FDN::set_dry(float p_dry) {
	dry = p_dry;
}

float FDN::get_dry() const {
	return dry;
}

void FDN::set_pre_delay_ms(float p_pre_delay_ms) {
	pre_delay_ms = p_pre_delay_ms;
}

float FDN::get_pre_delay_ms() const {
	return pre_delay_ms;
}

void FDN::set_pre_gain(float p_pre_gain) {
	pre_gain = p_pre_gain;
}

float FDN::get_pre_gain() const {
	return pre_gain;
}

void FDN::set_t_60(float p_t_60) {
	t_60 = p_t_60;
}

float FDN::get_t_60() const {
	return t_60;
}


void FDN::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_delay_lines", "amount"), &FDN::set_delay_lines);
	ClassDB::bind_method(D_METHOD("get_delay_lines"), &FDN::get_delay_lines);

	ClassDB::bind_method(D_METHOD("set_dry", "amount"), &FDN::set_dry);
	ClassDB::bind_method(D_METHOD("get_dry"), &FDN::get_dry);

	ClassDB::bind_method(D_METHOD("set_pre_delay_ms", "amount"), &FDN::set_pre_delay_ms);
	ClassDB::bind_method(D_METHOD("get_pre_delay_ms"), &FDN::get_pre_delay_ms);

	ClassDB::bind_method(D_METHOD("set_pre_gain", "amount"), &FDN::set_pre_gain);
	ClassDB::bind_method(D_METHOD("get_pre_gain"), &FDN::get_pre_gain);

	ClassDB::bind_method(D_METHOD("set_t_60", "amount"), &FDN::set_t_60);
	ClassDB::bind_method(D_METHOD("get_t_60"), &FDN::get_t_60);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "delay_lines", PROPERTY_HINT_RANGE, "4,32,4"), "set_delay_lines", "get_delay_lines");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dry", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dry", "get_dry");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pre_delay_ms", PROPERTY_HINT_RANGE, "0,200,2"), "set_pre_delay_ms", "get_pre_delay_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pre_gain", PROPERTY_HINT_RANGE, "-60,0,0.01,suffix:dB"), "set_pre_gain", "get_pre_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "t_60", PROPERTY_HINT_RANGE, "0.01,4,0.04"), "set_t_60", "get_t_60");
}