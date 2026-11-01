# Task F: values to check your handwritten derivation against

The handwritten pages (at most 2) must be **your own work**. Do the derivation by hand first, then compare your numbers with this table, which was produced by the C++ implementation (`results/backprop_gradient_check.csv`). Keep at least 6 significant figures in your hand calculation.

**Setup:** x = 1, y = 2, w = 0.5, b = 0, v = 1, c = 0, learning rate η = 0.1.

**Steps your derivation should show:**
1. Forward pass: z = wx + b, h = tanh z, ŷ = vh + c, J = ½(ŷ − y)².
2. dJ/dŷ = (ŷ − y).
3. ∂J/∂c, then ∂J/∂v (needs h), then ∂J/∂h (needs v), then ∂J/∂z (needs d tanh/dz = 1 − tanh²z), then ∂J/∂w and ∂J/∂b.
4. The update θ ← θ − η·∂J/∂θ for all four parameters, using gradients from the **original** parameters (simultaneous update).
5. A sentence on direction: why each parameter moves up or down.

**C++ reference values:**

| Quantity | Value |
|---|---|
| z | 0.5 |
| h = tanh(0.5) | 0.4621171573 |
| ŷ | 0.4621171573 |
| ŷ − y | −1.5378828427 |
| J | 1.1825418190 |
| 1 − h² | 0.7864477329 |
| ∂J/∂c | −1.5378828427 |
| ∂J/∂v | −0.7106820475 |
| ∂J/∂w = ∂J/∂b | −1.2094644752 |
| w after one step | 0.6209464475 |
| b after one step | 0.1209464475 |
| v after one step | 1.0710682047 |
| c after one step | 0.1537882843 |
| J after the step (start of the 100-step plot) | 0.6857737650 |

**Direction:** every gradient is negative, because ŷ (0.462) is below y (2). Raising any of w, b, v or c raises ŷ, so gradient descent increases all four parameters.
