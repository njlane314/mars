#ifndef ALPHA_HPP
#define ALPHA_HPP

#include "mars_api.h"

class AlphaModel final {
public:
    AlphaModel() = delete;

    static uint32_t augDim(uint32_t base_dim, uint32_t k);
    static rt_status_t trainEval(rt_model_t *model, const rt_data_t *data,
                                 size_t train_start, size_t train_end,
                                 size_t val_start, size_t val_end,
                                 double lambda, rt_bt_stats_t *stats_out);
    static rt_status_t trainFinal(rt_model_t *model, const rt_data_t *data,
                                  size_t train_start, size_t train_end,
                                  double lambda);
    static rt_status_t predictRange(const rt_model_t *model, const rt_data_t *data,
                                    size_t start, size_t end, double *pred);
};

#endif
