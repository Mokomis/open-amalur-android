#ifndef OA_BINK_AUDIO_IDCT_H
#define OA_BINK_AUDIO_IDCT_H

#include <math.h>
#include <stdint.h>

enum { OA_BINK_MAX_COEFFICIENTS = 2048 };

static int oa_bink_idct_supported(uint32_t count)
{
    return count >= 2 && count <= OA_BINK_MAX_COEFFICIENTS && (count & (count - 1)) == 0;
}

/* Bink DCT audio uses a full-weight DC coefficient. A conjugate spectrum of
 * length 2N yields x[0] + sum(x[k] * cos(pi*k*(i+0.5)/N)) after the inverse FFT. */
static void oa_bink_audio_idct(float *output, const float *input, uint32_t count)
{
    const double pi = 3.14159265358979323846;
    const uint32_t size = 2 * count;
    double real[2 * OA_BINK_MAX_COEFFICIENTS];
    double imag[2 * OA_BINK_MAX_COEFFICIENTS];

    real[0] = 2.0 * input[0];
    imag[0] = 0;
    real[count] = 0;
    imag[count] = 0;
    const double phase = pi / size;
    const double step_real = cos(phase);
    const double step_imag = sin(phase);
    double phase_real = step_real;
    double phase_imag = step_imag;
    for (uint32_t k = 1; k < count; ++k) {
        real[k] = real[size - k] = input[k] * phase_real;
        imag[k] = input[k] * phase_imag;
        imag[size - k] = -imag[k];
        const double next_real = phase_real * step_real - phase_imag * step_imag;
        phase_imag = phase_real * step_imag + phase_imag * step_real;
        phase_real = next_real;
    }

    for (uint32_t i = 1, j = 0; i < size; ++i) {
        uint32_t bit = size / 2;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            const double r = real[i], z = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = r;
            imag[j] = z;
        }
    }

    for (uint32_t length = 2; length <= size; length *= 2) {
        const uint32_t half = length / 2;
        const double rotation = 2.0 * pi / length;
        const double rotation_real = cos(rotation);
        const double rotation_imag = sin(rotation);
        double w_real = 1, w_imag = 0;
        for (uint32_t j = 0; j < half; ++j) {
            for (uint32_t i = j; i < size; i += length) {
                const uint32_t other = i + half;
                const double r = real[other] * w_real - imag[other] * w_imag;
                const double z = real[other] * w_imag + imag[other] * w_real;
                real[other] = real[i] - r;
                imag[other] = imag[i] - z;
                real[i] += r;
                imag[i] += z;
            }
            const double next_real = w_real * rotation_real - w_imag * rotation_imag;
            w_imag = w_real * rotation_imag + w_imag * rotation_real;
            w_real = next_real;
        }
    }
    for (uint32_t i = 0; i < count; ++i)
        output[i] = (float)(0.5 * real[i]);
}

#endif
