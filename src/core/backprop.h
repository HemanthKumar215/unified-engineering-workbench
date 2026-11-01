// One-hidden-unit regression network with hand-derived gradients (no autodiff).
//   z = w x + b,  h = tanh(z),  y_hat = v h + c,  J = 0.5 (y_hat - y)^2
#pragma once
#include <vector>

namespace wb {

struct Params {
    double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct Forward {
    double z, h, yHat, J;
};

struct Gradients {
    double dw, db, dv, dc;
};

Forward forward(const Params& p, double x, double y);

// Chain rule:
//   dJ/dy_hat = y_hat - y = e
//   dJ/dc = e                     dJ/dv = e h
//   dJ/dh = e v                   dJ/dz = e v (1 - h^2)
//   dJ/dw = e v (1 - h^2) x       dJ/db = e v (1 - h^2)
Gradients backward(const Params& p, double x, double y);

// Central differences: (J(theta+eps) - J(theta-eps)) / (2 eps), one parameter at a time.
Gradients numericalGradients(const Params& p, double x, double y, double eps = 1e-6);

// Simultaneous update: all four gradients are computed from the *old* parameters first.
Params sgdStep(const Params& p, const Gradients& g, double lr);

// Loss before step 0 followed by the loss after each of `steps` updates (size steps+1).
std::vector<double> train(Params p, double x, double y, double lr, int steps);

}  // namespace wb
