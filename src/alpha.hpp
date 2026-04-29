#ifndef ALPHA_HPP
#define ALPHA_HPP

#include "api.h"

class alpha final {
public:
    alpha() = delete;

    static uint32_t aug_dim(uint32_t base_dim, uint32_t k);
    static mars_status_t train_eval(mars_model_t *model, const mars_data_t *data,
                                 size_t train_start, size_t train_end,
                                 size_t val_start, size_t val_end,
                                 double lambda, mars_bt_stats_t *stats_out);
    static mars_status_t train_final(mars_model_t *model, const mars_data_t *data,
                                  size_t train_start, size_t train_end,
                                  double lambda);
    static mars_status_t predict_range(const mars_model_t *model, const mars_data_t *data,
                                    size_t start, size_t end, double *pred);
};

#endif
