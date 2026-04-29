#ifndef SCALE_HPP
#define SCALE_HPP

#include "api.h"

class ScalerOps final {
public:
    ScalerOps() = delete;

    static mars_status_t fit(mars_scaler_t *scaler, const double *x, size_t n, uint32_t d);
    static void applyVec(const mars_scaler_t *scaler, const double *x, double *z);
    static void rowsToMatrixReg(const mars_data_t *data, size_t start, size_t end, double *x);
    static void rowsToMatrixBase(const mars_data_t *data, size_t start, size_t end, double *x);
};

#endif
