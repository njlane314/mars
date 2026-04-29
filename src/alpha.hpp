#ifndef ALPHA_HPP
#define ALPHA_HPP

#include "api.h"

class AlphaModel final {
public:
    AlphaModel() = delete;

    static uint32_t augDim(uint32_t base_dim, uint32_t k);
    static mars_status_t trainEval(mars_model_t *model, const mars_data_t *data,
                                 size_t train_start, size_t train_end,
                                 size_t val_start, size_t val_end,
                                 double lambda, mars_bt_stats_t *stats_out);
    static mars_status_t trainFinal(mars_model_t *model, const mars_data_t *data,
                                  size_t train_start, size_t train_end,
                                  double lambda);
    static mars_status_t predictRange(const mars_model_t *model, const mars_data_t *data,
                                    size_t start, size_t end, double *pred);
};

#endif
