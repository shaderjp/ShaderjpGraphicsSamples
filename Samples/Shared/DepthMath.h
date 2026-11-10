#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace tutorial::depth
{
// z is positive view-axis depth in metres, NOT Euclidean distance from the eye.
struct Mapping
{
    double nearZ = .1, farZ = 10000;
    bool reversed = false, orthographic = false, infinite = false;
    void Validate() const
    {
        if (!std::isfinite(nearZ) || !std::isfinite(farZ) || nearZ <= 0 || farZ <= nearZ ||
            (orthographic && infinite))
            throw std::runtime_error("Depth: require finite 0 < near < far; infinite requires perspective");
    }
    double Encode(double z) const
    {
        if (orthographic)
            return reversed ? (farZ - z) / (farZ - nearZ) : (z - nearZ) / (farZ - nearZ);
        if (infinite)
            return reversed ? nearZ / z : 1 - nearZ / z;
        // Do not subtract an already rounded forward depth from one on the GPU.
        return reversed ? nearZ * (farZ - z) / ((farZ - nearZ) * z)
                        : farZ * (z - nearZ) / ((farZ - nearZ) * z);
    }
    double Decode(double d) const
    {
        if (orthographic)
            return reversed ? farZ - d * (farZ - nearZ) : nearZ + d * (farZ - nearZ);
        if (infinite)
            return nearZ / (reversed ? d : 1 - d);
        return nearZ * farZ / (reversed ? nearZ + d * (farZ - nearZ) : farZ - d * (farZ - nearZ));
    }
};
inline double Quantize(double d, int format)
{
    d = std::clamp(d, 0.0, 1.0);
    if (format == 2)
        return double(float(d));
    const double scale = format == 0 ? 65535.0 : 16777215.0;
    return std::round(d * scale) / scale;
}
inline double Spacing(const Mapping &m, double z, int format)
{
    const double d = Quantize(m.Encode(z), format);
    double next;
    if (format == 2)
        next = std::nextafter(float(d), m.reversed ? 0.f : 1.f);
    else
        next = d + (m.reversed ? -1 : 1) / (format == 0 ? 65535.0 : 16777215.0);
    if (next == d || next < 0 || next > 1)
        return std::numeric_limits<double>::quiet_NaN();
    return std::abs(m.Decode(next) - m.Decode(d));
}
// A teaching model, not a hardware format: E3M3, bias=3, including subnormals.
inline std::vector<double> ToyFloat()
{
    std::vector<double> values{0};
    for (int exponent = 0; exponent < 7; ++exponent)
        for (int fraction = 0; fraction < 8; ++fraction)
        {
            double d =
                exponent == 0 ? std::ldexp(fraction / 8.0, -2) : std::ldexp(1 + fraction / 8.0, exponent - 3);
            if (d > 0 && d <= 1)
                values.push_back(d);
        }
    std::sort(values.begin(), values.end());
    return values;
}
} // namespace tutorial::depth
