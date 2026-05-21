// AquaControl — Solar calculator implementation.
//
// Reference: NOAA Solar Calculator equations,
//   https://gml.noaa.gov/grad/solcalc/calcdetails.html
//
// All trig in degrees (converted internally).
#include "solar_calc.h"

#include <atomic>
#include <cmath>

namespace aqua::solar {

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;

inline double sind(double d) { return std::sin(d * kDeg2Rad); }
inline double cosd(double d) { return std::cos(d * kDeg2Rad); }
inline double tand(double d) { return std::tan(d * kDeg2Rad); }

// Julian day for a Y/M/D at 00:00 UTC (Gregorian).
double julian_day(int year, int month, int day) {
    if (month <= 2) { year -= 1; month += 12; }
    int A = year / 100;
    int B = 2 - A + A / 4;
    return std::floor(365.25 * (year + 4716)) +
           std::floor(30.6001 * (month + 1)) +
           day + B - 1524.5;
}

}  // namespace

DayResult compute(int year, int month, int day,
                  float lat_f, float lon_f, int16_t utc_offset_min) {
    DayResult r{};

    double lat = lat_f;
    double lon = lon_f;

    // Julian century for the day at 0h local (= UTC + offset).
    double jd = julian_day(year, month, day) - (double)utc_offset_min / 1440.0;
    double T  = (jd - 2451545.0) / 36525.0;

    // Geometric mean longitude (deg).
    double L0 = std::fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
    if (L0 < 0) L0 += 360.0;
    // Geometric mean anomaly (deg).
    double M  = 357.52911 + T * (35999.05029 - 0.0001537 * T);
    // Eccentricity.
    double e  = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
    // Equation of center.
    double C  = sind(M)     * (1.914602 - T * (0.004817 + 0.000014 * T))
              + sind(2 * M) * (0.019993 - 0.000101 * T)
              + sind(3 * M) * 0.000289;
    // True longitude / anomaly.
    double trueLong = L0 + C;
    // Apparent longitude.
    double omega   = 125.04 - 1934.136 * T;
    double appLong = trueLong - 0.00569 - 0.00478 * sind(omega);
    // Mean obliquity of ecliptic.
    double seconds = 21.448 - T * (46.815 + T * (0.00059 - T * 0.001813));
    double e0      = 23.0 + (26.0 + seconds / 60.0) / 60.0;
    // Corrected obliquity.
    double eps = e0 + 0.00256 * cosd(omega);
    // Sun declination.
    double decl = std::asin(sind(eps) * sind(appLong)) * kRad2Deg;
    // Equation of time (in minutes).
    double y = tand(eps / 2.0); y *= y;
    double eot = 4.0 * kRad2Deg * (
          y * sind(2 * L0)
        - 2 * e * sind(M)
        + 4 * e * y * sind(M) * cosd(2 * L0)
        - 0.5 * y * y * sind(4 * L0)
        - 1.25 * e * e * sind(2 * M)
    );

    // Hour angle for standard sunrise/sunset (altitude = -0.833°).
    double cosH = (cosd(90.833) - sind(lat) * sind(decl))
                / (cosd(lat) * cosd(decl));
    if (cosH > 1.0)  { r.valid = false; return r; }  // never rises
    if (cosH < -1.0) { r.valid = false; return r; }  // never sets
    double H = std::acos(cosH) * kRad2Deg;  // degrees

    // Solar noon (minutes UTC).
    double solar_noon_utc = 720.0 - 4.0 * lon - eot;
    double sunrise_utc    = solar_noon_utc - 4.0 * H;
    double sunset_utc     = solar_noon_utc + 4.0 * H;

    auto wrap = [](double m) {
        // wrap into [0, 1440)
        m = std::fmod(m, 1440.0);
        if (m < 0) m += 1440.0;
        return m;
    };

    double sunrise_local = wrap(sunrise_utc + (double)utc_offset_min);
    double sunset_local  = wrap(sunset_utc  + (double)utc_offset_min);

    r.sunrise_min = (int16_t)std::lround(sunrise_local);
    r.sunset_min  = (int16_t)std::lround(sunset_local);
    r.valid       = true;
    return r;
}

static std::atomic<bool> s_recalc_needed{false};

void request_recalc() { s_recalc_needed.store(true, std::memory_order_relaxed); }
bool consume_recalc() { return s_recalc_needed.exchange(false, std::memory_order_acq_rel); }

}  // namespace aqua::solar
