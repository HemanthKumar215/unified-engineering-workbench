#include "svg_plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace wb {

namespace {

std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '&') r += "&amp;";
        else r += c;
    }
    return r;
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.4g", v);
    return b;
}

// "Nice" linear ticks covering [lo, hi].
std::vector<double> linearTicks(double lo, double hi) {
    const double span = hi - lo;
    const double raw = span / 6.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * mag) {
            step = m * mag;
            break;
        }
    std::vector<double> t;
    for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9 * span; v += step) t.push_back(std::abs(v) < 1e-12 * step ? 0.0 : v);
    return t;
}

}  // namespace

std::string renderSvg(const PlotSpec& spec, const std::vector<Series>& series) {
    const double W = spec.width, H = spec.height;
    const double ml = 80, mr = 190, mt = 44, mb = 60;  // right margin hosts the legend
    const double pw = W - ml - mr, ph = H - mt - mb;

    auto tx = [&](double v) { return spec.logX ? std::log10(v) : v; };
    auto ty = [&](double v) { return spec.logY ? std::log10(v) : v; };

    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (const auto& s : series)
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
            x0 = std::min(x0, tx(s.x[i]));
            x1 = std::max(x1, tx(s.x[i]));
            y0 = std::min(y0, ty(s.y[i]));
            y1 = std::max(y1, ty(s.y[i]));
        }
    if (!std::isfinite(x0)) x0 = 0, x1 = 1, y0 = 0, y1 = 1;
    if (x1 == x0) x1 = x0 + 1;
    if (y1 == y0) y1 = y0 + 1;
    const double padY = 0.05 * (y1 - y0);
    if (!spec.logY) y0 -= padY, y1 += padY;
    if (spec.logX) x0 = std::floor(x0), x1 = std::ceil(x1);
    if (spec.logY) y0 = std::floor(y0), y1 = std::ceil(y1);

    auto px = [&](double v) { return ml + (tx(v) - x0) / (x1 - x0) * pw; };
    auto py = [&](double v) { return mt + ph - (ty(v) - y0) / (y1 - y0) * ph; };

    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H << "' viewBox='0 0 " << W << ' ' << H
      << "' font-family='Segoe UI, Arial, sans-serif' font-size='12'>\n";
    o << "<rect width='100%' height='100%' fill='white'/>\n";
    o << "<text x='" << W / 2 << "' y='24' text-anchor='middle' font-size='15' font-weight='600'>" << esc(spec.title) << "</text>\n";

    // Grid + tick labels.
    auto ticks = [&](double lo, double hi, bool log) {
        std::vector<double> t;
        if (log)
            for (double e = lo; e <= hi + 1e-9; e += 1) t.push_back(e);
        else
            t = linearTicks(lo, hi);
        return t;
    };
    for (double t : ticks(x0, x1, spec.logX)) {
        const double X = ml + (t - x0) / (x1 - x0) * pw;
        o << "<line x1='" << X << "' y1='" << mt << "' x2='" << X << "' y2='" << mt + ph << "' stroke='#e5e7eb'/>\n";
        o << "<text x='" << X << "' y='" << mt + ph + 18 << "' text-anchor='middle'>" << num(spec.logX ? std::pow(10, t) : t) << "</text>\n";
    }
    for (double t : ticks(y0, y1, spec.logY)) {
        const double Y = mt + ph - (t - y0) / (y1 - y0) * ph;
        o << "<line x1='" << ml << "' y1='" << Y << "' x2='" << ml + pw << "' y2='" << Y << "' stroke='#e5e7eb'/>\n";
        o << "<text x='" << ml - 8 << "' y='" << Y + 4 << "' text-anchor='end'>" << num(spec.logY ? std::pow(10, t) : t) << "</text>\n";
    }
    o << "<rect x='" << ml << "' y='" << mt << "' width='" << pw << "' height='" << ph << "' fill='none' stroke='#374151'/>\n";
    o << "<text x='" << ml + pw / 2 << "' y='" << H - 16 << "' text-anchor='middle' font-size='13'>" << esc(spec.xLabel) << "</text>\n";
    o << "<text transform='translate(20," << mt + ph / 2 << ") rotate(-90)' text-anchor='middle' font-size='13'>" << esc(spec.yLabel) << "</text>\n";

    for (std::size_t si = 0; si < series.size(); ++si) {
        const auto& s = series[si];
        if (s.markers) {
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
                o << "<circle cx='" << px(s.x[i]) << "' cy='" << py(s.y[i]) << "' r='3.5' fill='" << s.color << "'/>\n";
            }
        } else {
            o << "<polyline fill='none' stroke='" << s.color << "' stroke-width='1.8'" << (s.dashed ? " stroke-dasharray='6 4'" : "") << " points='";
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
                o << px(s.x[i]) << ',' << py(s.y[i]) << ' ';
            }
            o << "'/>\n";
        }
        const double ly = mt + 10 + 20 * static_cast<double>(si);
        const double lx = ml + pw + 14;
        if (s.markers)
            o << "<circle cx='" << lx + 12 << "' cy='" << ly << "' r='3.5' fill='" << s.color << "'/>\n";
        else
            o << "<line x1='" << lx << "' y1='" << ly << "' x2='" << lx + 24 << "' y2='" << ly << "' stroke='" << s.color
              << "' stroke-width='2'" << (s.dashed ? " stroke-dasharray='6 4'" : "") << "/>\n";
        o << "<text x='" << lx + 30 << "' y='" << ly + 4 << "'>" << esc(s.label) << "</text>\n";
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace wb
