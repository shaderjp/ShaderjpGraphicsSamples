#include "../Samples/Shared/DepthMath.h"
#include <iostream>
using namespace tutorial::depth;
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        for (bool ortho : {false, true})
            for (bool reverse : {false, true})
                for (bool infinite : {false, true})
                {
                    if (ortho && infinite)
                        continue;
                    Mapping m{.1, 10000, reverse, ortho, infinite};
                    m.Validate();
                    Require(std::abs(m.Encode(.1) - (reverse ? 1 : 0)) < 1e-12, "Near endpoint");
                    if (!infinite)
                        Require(std::abs(m.Encode(10000) - (reverse ? 0 : 1)) < 1e-12, "Far endpoint");
                    double prev = m.Encode(.1);
                    for (int i = 1; i <= 1000; ++i)
                    {
                        const double z = .1 * std::pow(100000.0, i / 1000.0), d = m.Encode(z);
                        Require(reverse ? d <= prev : d >= prev, "Depth ordering");
                        Require(std::abs(m.Decode(d) - z) < z * 1e-9, "Analytic round trip");
                        prev = d;
                    }
                }
        Mapping f{1, 101, false, false, false}, r = f;
        r.reversed = true;
        Require(std::abs(f.Encode(2) - .505) < 1e-15, "Known perspective value");
        Mapping ortho{1, 101, false, true, false};
        Require(ortho.Encode(26) == .25, "Known orthographic value");
        Mapping inf{.1, 10000, true, false, true};
        Require(std::abs(inf.Encode(1000) - .0001) < 1e-15, "Infinite reversed n/z");
        for (int format : {0, 1})
            for (int i = 1; i < 999; ++i)
                Require(std::abs(Quantize(i / 999.0, format) + Quantize(1 - i / 999.0, format) - 1) < 1e-12,
                        "UNORM reversal does not add precision");
        Mapping normal{.1, 10000, false, false, false}, reverse = normal;
        reverse.reversed = true;
        Require(Spacing(reverse, 1000, 2) < Spacing(normal, 1000, 2) / 1000,
                "D32 reverse far-distance spacing");
        Require(Quantize(normal.Encode(1000), 2) == Quantize(normal.Encode(1000.002), 2),
                "Forward D32 merges stress planes");
        Require(Quantize(reverse.Encode(1000), 2) != Quantize(reverse.Encode(1000.002), 2),
                "Reverse D32 distinguishes stress planes");
        auto toy = ToyFloat();
        Require(toy.front() == 0 && toy.back() == 1, "Toy endpoints");
        Require(std::adjacent_find(toy.begin(), toy.end()) == toy.end(), "Unique toy values");
        for (auto bad : std::vector<Mapping>{{0, 10}, {1, 1}, {-1, 10}, {.1, 10, false, true, true}})
        {
            bool rejected = false;
            try
            {
                bad.Validate();
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            Require(rejected, "Invalid mapping rejected");
        }
        std::cout << "Depth CPU tests passed: analytic values, ordering, inverse, UNORM, float spacing, "
                     "invalid inputs\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
