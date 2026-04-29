#ifndef HMM_HPP
#define HMM_HPP

#include "api.h"

class hmm final {
public:
    hmm() = delete;

    static mars_status_t fit_preserve_scaler(mars_hmm_t *hmm, const mars_data_t *data,
                                         size_t start, size_t end, uint32_t k);
    static mars_status_t filter_range(const mars_hmm_t *hmm, const mars_data_t *data,
                                   size_t start, size_t end, double *pi_out);
    static double log_lik_range(const mars_hmm_t *hmm, const mars_data_t *data,
                              size_t start, size_t end);
};

#endif
