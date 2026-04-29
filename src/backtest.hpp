#ifndef BACKTEST_HPP
#define BACKTEST_HPP

#include "api.h"

class backtest final {
public:
    backtest() = delete;

    static mars_bt_stats_t evaluate(const mars_model_t *model, const mars_data_t *data,
                                  size_t start, size_t end, const double *pred,
                                  const char *trades_path);
};

#endif
