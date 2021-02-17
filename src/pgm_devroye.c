/* Copyright (c) 2020-2021, Zolisa Bleki
 *
 * SPDX-License-Identifier: BSD-3-Clause */
#include "pgm_devroye.h"


#ifndef PGM_GAMMA_LIMIT
#define PGM_GAMMA_LIMIT 200
#endif
/*
 * Sample from PG(h, z) using the Gamma convolution approximation method.
 *
 * The infinite sum is truncated to 200 terms.
 */
NPY_INLINE double
random_polyagamma_gamma_conv(bitgen_t* bitgen_state, double h, double z)
{
    const double z2 = z * z;
    double c, out = 0;

    for (size_t n = PGM_GAMMA_LIMIT; n--; ) {
        c = n + 0.5;
        out += random_standard_gamma(bitgen_state, h) / (PGM_PI2 * c * c + z2);
    }
    return 0.5 * out;
}

// 0.64, the truncation point
#define T NPY_2_PI

/* a struct to store frequently used values. This avoids unnecessary
 * recalculation of these values during a single call to the sampler.
 */
struct config {
    double mu;
    double k;
    double half_mu2;
    double ratio;
    double x;
    double logx;
};

/* 
 * Compute a_n(x|t), the nth term of the alternating sum S_n(x|t)
 */
static NPY_INLINE double
piecewise_coef(size_t n, struct config* cfg)
{
    double n_plus_half = n + 0.5;
    double n_plus_half2 = n_plus_half * n_plus_half;
    double n_plus_halfpi = NPY_PI * n_plus_half;
    double x = cfg->x;

    if (x > T) {
        return n_plus_halfpi * exp(-0.5 * x * n_plus_halfpi * n_plus_halfpi); 
    }
    else if (x > 0) {
        return n_plus_halfpi * exp(-1.5 * (PGM_LOGPI_2 + cfg->logx) - 2 * n_plus_half2 / x);
    }
    return 0;
}

/*
 * Generate a random sample from J*(1, 0) using algorithm described in
 * Devroye(2009), page 7.
 */
static NPY_INLINE double
random_jacobi_0(bitgen_t* bitgen_state, struct config* cfg)
{
    static const double p = 0.422599094; 
    static const double q = 0.57810262346829443;
    static const double ratio = p / (p + q);
    double e1, e2, s, u;
    size_t i;

    for (;;) {
        if (next_double(bitgen_state) < ratio) {
            do {
                e1 = random_standard_exponential(bitgen_state);
                e2 = random_standard_exponential(bitgen_state);
            } while ((e1 * e1) > (NPY_PI * e2));  // 2 / t = pi
            cfg->x = (1 + T * e1);
            cfg->x = T / (cfg->x * cfg->x);
        }
        else {
            cfg->x = T + 8 * random_standard_exponential(bitgen_state) / PGM_PI2;
        }
        cfg->logx = log(cfg->x);
        s = piecewise_coef(0, cfg);
        u = next_double(bitgen_state) * s;
        for (i = 1;; ++i) {
            if (i & 0x1) {
                s -= piecewise_coef(i, cfg);
                if (u < s)
                    return cfg->x;
            }
            else {
                s += piecewise_coef(i, cfg);
                if (u > s)
                    break;
            }
        }
    }
}

/*
 * Initialize the constant values that are used frequently and in more than one
 * place per call to `random_polyagamma_devroye`
 */
static NPY_INLINE void
initialize_config(struct config* cfg, double z)
{
    cfg->mu = 1 / z;
    cfg->k = PGM_PI2_8 + 0.5 * z * z;
    cfg->half_mu2 = -0.5 / (cfg->mu * cfg->mu);
}

/*
 * Generate a random sample J*(1, z) using method described in Polson et al (2013)
 */
static NPY_INLINE double
random_jacobi(bitgen_t* bitgen_state, struct config* cfg)
{
    double s, u;
    size_t i;

    for (;;) {
        if (next_double(bitgen_state) < cfg->ratio) {
            cfg->x = random_right_bounded_inverse_gaussian(bitgen_state, cfg->mu, 1, T);
        }
        else {
            cfg->x = T + random_standard_exponential(bitgen_state) / cfg->k;
        }
        /* Here we use S_n(x|t) instead of S_n(x|z,t) as explained in page 13 of
         * Polson et al.(2013) and page 14 of Windle et al. (2014). This
         * convenience avoids issues with S_n blowing up when z is very large.*/
        cfg->logx = log(cfg->x);
        s = piecewise_coef(0, cfg);
        u = next_double(bitgen_state) * s;
        for (i = 1;; ++i) {
            if (i & 0x1) {
                s -= piecewise_coef(i, cfg);
                if (u <= s)
                    return cfg->x;
            }
            else {
                s += piecewise_coef(i, cfg);
                if (u > s)
                    break;
            }
        }
    }
}

/*
 * Sample from Polya-Gamma PG(n, z) using the Devroye method, where n is a
 * positive integer.
 */
NPY_INLINE double
random_polyagamma_devroye(bitgen_t *bitgen_state, uint64_t n, double z)
{
    struct config cfg;
    double q, p;
    double out = 0;

    if (z == 0) {
        do {
            out += random_jacobi_0(bitgen_state, &cfg);
            n--;
        } while (n);
        return 0.25 * out; 
    }

    initialize_config(&cfg, z);

    q = NPY_PI_2 * exp(-cfg.k * T) / cfg.k;
    p = 2 * exp(-z) * inverse_gaussian_cdf(T, cfg.mu, 1);
    cfg.ratio = p / (p + q);
    
    do {
        out += random_jacobi(bitgen_state, &cfg);    
        n--;
    } while (n);
    return 0.25 * out; 
}

#undef T
