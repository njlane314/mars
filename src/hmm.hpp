#ifndef HMM_HPP
#define HMM_HPP

#include "api.h"

class HmmModel final {
public:
    HmmModel() = delete;

    static mars_status_t fitPreserveScaler(mars_hmm_t *hmm, const mars_data_t *data,
                                         size_t start, size_t end, uint32_t k);
    static mars_status_t filterRange(const mars_hmm_t *hmm, const mars_data_t *data,
                                   size_t start, size_t end, double *pi_out);
    static double logLikRange(const mars_hmm_t *hmm, const mars_data_t *data,
                              size_t start, size_t end);
};

#endif
