/*******************************************************************************
* Copyright 2017-2020 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_SIMPLE_Q10N_HPP
#define CPU_SIMPLE_Q10N_HPP

#include <assert.h>

#include "common/c_types_map.hpp"
#include "common/math_utils.hpp"
#include "common/nstl.hpp"
#include "common/type_helpers.hpp"
#include "common/utils.hpp"

#include "cpu/platform.hpp"

#if DNNL_X64
#include "immintrin.h"
#endif

namespace dnnl {
namespace impl {
namespace cpu {

/** rounds @p f to an integer according to the mxcsr register */
inline int mxcsr_round(float f) ATTR_NO_MSAN {
#if DNNL_X64
    return _mm_cvtss_si32(_mm_load_ss(&f));
#else
    return (int)nearbyintf(f); // optimism
#endif
}

template <typename data_t, typename acc_t>
inline typename utils::enable_if<!nstl::is_integral<data_t>::value,
        typename utils::remove_reference<data_t>::type>::type
saturate(const acc_t &x) {
    return (typename utils::remove_reference<data_t>::type)x;
}

template <typename data_t, typename acc_t>
inline typename utils::enable_if<nstl::is_integral<data_t>::value,
        typename utils::remove_reference<data_t>::type>::type
saturate(const acc_t &x) {
    acc_t v = x;
    if (v < (acc_t)nstl::numeric_limits<data_t>::lowest())
        v = (acc_t)nstl::numeric_limits<data_t>::lowest();
    if (v > (acc_t)nstl::numeric_limits<data_t>::max())
        v = (acc_t)nstl::numeric_limits<data_t>::max();
    return (typename utils::remove_reference<data_t>::type)v;
}

template <typename data_t>
double saturate(const double &x) {
    double v = x;
    if (v < (double)nstl::numeric_limits<data_t>::lowest())
        v = (double)nstl::numeric_limits<data_t>::lowest();
    if (v > (double)nstl::numeric_limits<data_t>::max())
        v = (double)nstl::numeric_limits<data_t>::max();
    return v;
}

template <>
inline int8_t saturate<int8_t, uint8_t>(const uint8_t &x) {
    return x <= 127u ? x : 127;
}

template <>
inline uint8_t saturate<uint8_t, int8_t>(const int8_t &x) {
    return x >= 0 ? x : 0;
}

template <typename out_t>
typename utils::enable_if<nstl::is_integral<out_t>::value, out_t>::type
out_round(float v) {
    return (out_t)mxcsr_round(v);
}

template <typename out_t>
typename utils::enable_if<nstl::is_integral<out_t>::value, out_t>::type
out_round(double v) {
    return (out_t)mxcsr_round((float)v);
}

template <typename out_t>
typename utils::enable_if<!nstl::is_integral<out_t>::value, out_t>::type
out_round(float v) {
    return v;
}

template <typename out_t>
inline out_t round_and_saturate(float f) {
    return saturate<out_t>(out_round<int>(f));
}

/* Quantization with alpha == 1 and beta == 0 */
template <typename in_t, typename out_t, typename enabled = void>
struct qz_a1b0 {
    out_t operator()(in_t in) { return round_and_saturate<out_t>((float)in); }
};

template <typename in_t, typename out_t>
struct qz_a1b0<in_t, out_t,
        typename utils::enable_if<true && nstl::is_integral<in_t>::value
                && !is_subset<in_t, out_t>::value>::type> {
    out_t operator()(in_t in) { return saturate<out_t>(in); }
};

template <typename in_t, typename out_t>
struct qz_a1b0<in_t, out_t,
        typename utils::enable_if<is_subset<in_t, out_t>::value>::type> {
    out_t operator()(in_t in) { return (out_t)in; }
};

/* Quantization with alpha == 1 */
template <typename in_t, typename out_t>
struct qz_a1 {
    out_t operator()(in_t in, out_t out, float beta) {
        return round_and_saturate<out_t>((float)in + beta * out);
    }
};

template <typename in_t>
struct qz_a1<in_t, float> {
    float operator()(in_t in, float out, float beta) {
        return (float)in + beta * out;
    }
};

/* Quantization with beta == 0 */
template <typename in_t, typename out_t>
struct qz_b0 {
    out_t operator()(in_t in, float alpha) {
        return round_and_saturate<out_t>(alpha * in);
    }
};

template <typename in_t>
struct qz_b0<in_t, float> {
    float operator()(in_t in, float alpha) { return alpha * in; }
};

/* Quantization */
template <typename in_t, typename out_t>
struct qz {
    out_t operator()(in_t in, out_t out, float alpha, float beta) {
        return round_and_saturate<out_t>(alpha * in + (beta ? beta * out : 0));
    }
};

template <typename in_t>
struct qz<in_t, float> {
    float operator()(in_t in, float out, float alpha, float beta) {
        return alpha * in + (beta ? beta * out : 0);
    }
};

template <>
struct qz<bfloat16_t, bfloat16_t> {
    float operator()(bfloat16_t in, bfloat16_t out, float alpha, float beta) {
        return (bfloat16_t)(alpha * (float)in + (beta ? beta * (float)out : 0));
    }
};

template <>
struct qz<float, bfloat16_t> {
    float operator()(float in, bfloat16_t out, float alpha, float beta) {
        return (bfloat16_t)(alpha * in + (beta ? beta * out : 0));
    }
};

template <>
struct qz<float16_t, float16_t> {
    float operator()(float16_t in, float16_t out, float alpha, float beta) {
        return (float16_t)(alpha * (float)in + (beta ? beta * (float)out : 0));
    }
};

template <>
struct qz<float, float16_t> {
    float operator()(float in, float16_t out, float alpha, float beta) {
        return (float16_t)(alpha * in + (beta ? beta * out : 0));
    }
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif
