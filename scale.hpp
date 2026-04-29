#ifndef SCALE_HPP
#define SCALE_HPP

#include "mars_api.h"

class ScalerOps final {
public:
    ScalerOps() = delete;

    static rt_status_t fit(rt_scaler_t *scaler, const double *x, size_t n, uint32_t d);
    static void applyVec(const rt_scaler_t *scaler, const double *x, double *z);
    static void rowsToMatrixReg(const rt_data_t *data, size_t start, size_t end, double *x);
    static void rowsToMatrixBase(const rt_data_t *data, size_t start, size_t end, double *x);
};

#endif
