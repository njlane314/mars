#ifndef SCALE_HPP
#define SCALE_HPP

#include "api.h"

class scale final {
public:
    scale() = delete;

    static mars_status_t fit(mars_scaler_t *scaler, const double *x, size_t n, uint32_t d);
    static void apply_vec(const mars_scaler_t *scaler, const double *x, double *z);
    static void rows_to_matrix_reg(const mars_data_t *data, size_t start, size_t end, double *x);
    static void rows_to_matrix_base(const mars_data_t *data, size_t start, size_t end, double *x);
};

#endif
