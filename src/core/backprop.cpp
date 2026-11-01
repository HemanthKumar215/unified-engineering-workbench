#include "backprop.h"

#include <cmath>

namespace wb {

Forward forward(const Params& p, double x, double y) {
    Forward f;
    f.z = p.w * x + p.b;
    f.h = std::tanh(f.z);
    f.yHat = p.v * f.h + p.c;
    const double e = f.yHat - y;
    f.J = 0.5 * e * e;
    return f;
}

Gradients backward(const Params& p, double x, double y) {
    const Forward f = forward(p, x, y);
    const double e = f.yHat - y;              // dJ/dy_hat
    const double dz = e * p.v * (1.0 - f.h * f.h);  // tanh'(z) = 1 - tanh(z)^2
    return {dz * x, dz, e * f.h, e};
}

Gradients numericalGradients(const Params& p, double x, double y, double eps) {
    auto central = [&](double Params::*m) {
        Params plus = p, minus = p;
        plus.*m += eps;
        minus.*m -= eps;
        return (forward(plus, x, y).J - forward(minus, x, y).J) / (2.0 * eps);
    };
    return {central(&Params::w), central(&Params::b), central(&Params::v), central(&Params::c)};
}

Params sgdStep(const Params& p, const Gradients& g, double lr) {
    return {p.w - lr * g.dw, p.b - lr * g.db, p.v - lr * g.dv, p.c - lr * g.dc};
}

std::vector<double> train(Params p, double x, double y, double lr, int steps) {
    std::vector<double> loss;
    loss.reserve(static_cast<std::size_t>(steps) + 1);
    loss.push_back(forward(p, x, y).J);
    for (int i = 0; i < steps; ++i) {
        p = sgdStep(p, backward(p, x, y), lr);
        loss.push_back(forward(p, x, y).J);
    }
    return loss;
}

}  // namespace wb
