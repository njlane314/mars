#ifndef HMM_HPP
#define HMM_HPP

#include "mars_api.h"

class HmmModel final {
public:
    HmmModel() = delete;

    static rt_status_t fitPreserveScaler(rt_hmm_t *hmm, const rt_data_t *data,
                                         size_t start, size_t end, uint32_t k);
    static rt_status_t filterRange(const rt_hmm_t *hmm, const rt_data_t *data,
                                   size_t start, size_t end, double *pi_out);
    static double logLikRange(const rt_hmm_t *hmm, const rt_data_t *data,
                              size_t start, size_t end);
};

#endif
