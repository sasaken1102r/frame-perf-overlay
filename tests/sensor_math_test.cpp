// headset/sensor_math tests: parsing and derivations for the system sensors, fed from fixture files.
// Usage: sensor-math-test <fixture dir>   (CTest passes tests/fixtures/sensors)
#include "sensor_math.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                  \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

using namespace sensor_math;

std::string fixtureDir;

std::string readFixture(const char* name) {
    std::ifstream in(fixtureDir + "/" + name, std::ios::binary);
    if (!in) {
        std::printf("FAIL cannot open fixture %s/%s\n", fixtureDir.c_str(), name);
        ++failures;
        return "";
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool isNan(double v) {
    return std::isnan(v);
}

CpuTicks parsed(const char* fixture) {
    CpuTicks t;
    CHECK(parseProcStat(readFixture(fixture).c_str(), t));
    return t;
}

void testProcStat() {
    const CpuTicks a = parsed("proc_stat_a.txt");
    CHECK(a.total.size() == 3);  // total + cpu0 + cpu1; intr/ctxt lines ignored
    CHECK(a.present[0] && a.present[1] && a.present[2]);
    // busy = user+nice+system+irq+softirq+steal; total = busy + idle + iowait
    CHECK(a.busy[0] == 367360u + 15068u + 256399u + 89570u + 79807u);
    CHECK(a.total[0] == a.busy[0] + 6245980u + 687u);
    CHECK(a.busy[1] == 46199u + 1714u + 33845u + 30759u + 5426u);
    CHECK(a.total[1] == a.busy[1] + 761685u + 19u);

    const CpuTicks b = parsed("proc_stat_b.txt");
    const std::vector<double> pct = cpuPercents(a, b);
    CHECK(pct.size() == 3);
    CHECK(near(pct[0], 25.0));  // +100 busy of +400
    CHECK(near(pct[1], 50.0));  // +50 of +100
    CHECK(near(pct[2], 0.0));   // idle only

    // Offline core: cpu1 has no line, so cpu2 must still land on index 3
    const CpuTicks oa = parsed("proc_stat_offline_a.txt");
    CHECK(oa.total.size() == 4);
    CHECK(oa.present[0] && oa.present[1] && !oa.present[2] && oa.present[3]);
    const std::vector<double> op = cpuPercents(oa, parsed("proc_stat_offline_b.txt"));
    CHECK(op.size() == 4);
    CHECK(near(op[0], 40.0));
    CHECK(near(op[1], 50.0));
    CHECK(isNan(op[2]));
    CHECK(near(op[3], 50.0));  // +100 busy of +200

    // Counter going backwards (b -> a) is NaN, not a negative or huge value
    const std::vector<double> back = cpuPercents(b, a);
    CHECK(back.size() == 3);
    for (double v : back) CHECK(isNan(v));

    // No time passed
    const std::vector<double> same = cpuPercents(a, a);
    for (double v : same) CHECK(isNan(v));

    // First read (empty prev) and size mismatch (core appeared / prev shorter)
    const std::vector<double> first = cpuPercents(CpuTicks(), a);
    CHECK(first.size() == 3);
    for (double v : first) CHECK(isNan(v));
    CpuTicks shortPrev;
    CHECK(parseProcStat("cpu  100 0 100 800 0 0 0 0 0 0\n", shortPrev));
    CpuTicks longCur = a;
    longCur.total[0] += 1000;
    longCur.busy[0] += 500;
    const std::vector<double> mism = cpuPercents(shortPrev, longCur);
    CHECK(mism.size() == 3);
    CHECK(!isNan(mism[0]));
    CHECK(isNan(mism[1]));
    CHECK(isNan(mism[2]));

    // Garbage
    CpuTicks g;
    CHECK(!parseProcStat("", g));
    CHECK(!parseProcStat(nullptr, g));
    CHECK(!parseProcStat("intr 1 2 3\n", g));
    CHECK(!parseProcStat("cpu0 1 2 3 4 5\n", g));  // a core line without the total line
}

RailReading rail(const char* label, double w, double mv) {
    RailReading r;
    r.label = label;
    r.watts = w;
    r.volts = mv / 1000.0;
    return r;
}

std::vector<RailReading> capturedRails() {
    return {rail("vph", 9.188324, 3804),   rail("apc0", 0.519995, 862), rail("apc2", 0.806278, 826),
            rail("apc1", 0.231063, 732),   rail("nsp1", 0.201112, 812), rail("gfx", 0.439515, 665),
            rail("nsp2", 0.085414, 852),   rail("bob", 0.491827, 3757), rail("s1c", 0.505967, 1249),
            rail("s3c", 0.292181, 1010),   rail("s6c", 0.898119, 1894)};
}

void testPowerTree() {
    const PowerTree t = powerTree(capturedRails());
    CHECK(near(t.vphW, 9.188324));
    CHECK(near(t.vphV, 3.804));
    CHECK(near(t.railsW, 4.471471));  // 10 rails, vph not included
    CHECK(near(t.unmeasuredW, 9.188324 - 4.471471));
    CHECK(!near(t.unmeasuredW, 9.188324 - (4.471471 + 9.188324)));  // never the all-channels sum

    // vph listed last gives the same tree
    std::vector<RailReading> reordered = capturedRails();
    const RailReading vph = reordered.front();
    reordered.erase(reordered.begin());
    reordered.push_back(vph);
    const PowerTree r = powerTree(reordered);
    CHECK(near(r.railsW, t.railsW) && near(r.unmeasuredW, t.unmeasuredW));

    // One rail NaN: both railsW and unmeasured are NaN (no partial sum), vph still there
    std::vector<RailReading> oneBad = capturedRails();
    oneBad[3].watts = kNaN;
    const PowerTree b = powerTree(oneBad);
    CHECK(near(b.vphW, 9.188324));
    CHECK(isNan(b.railsW));
    CHECK(isNan(b.unmeasuredW));

    // vph missing: rails sum is fine, unmeasured is NaN
    std::vector<RailReading> noVph = capturedRails();
    noVph.erase(noVph.begin());
    const PowerTree n = powerTree(noVph);
    CHECK(isNan(n.vphW));
    CHECK(near(n.railsW, 4.471471));
    CHECK(isNan(n.unmeasuredW));

    // vph unreadable
    std::vector<RailReading> vphNan = capturedRails();
    vphNan[0].watts = kNaN;
    const PowerTree vn = powerTree(vphNan);
    CHECK(isNan(vn.unmeasuredW));
    CHECK(near(vn.railsW, 4.471471));

    // Empty / vph only
    const PowerTree e = powerTree({});
    CHECK(isNan(e.vphW) && isNan(e.railsW) && isNan(e.unmeasuredW));
    const PowerTree only = powerTree({rail("vph", 5.0, 3800)});
    CHECK(near(only.vphW, 5.0));
    CHECK(isNan(only.railsW));
    CHECK(isNan(only.unmeasuredW));

    // Unmeasured isn't clamped
    const PowerTree neg = powerTree({rail("vph", 1.0, 3800), rail("apc0", 1.2, 800)});
    CHECK(near(neg.unmeasuredW, -0.2));

    CHECK(!railVoltsMeaningful("gfx"));
    CHECK(railVoltsMeaningful("vph"));
    CHECK(railVoltsMeaningful("apc0"));
    CHECK(railVoltsMeaningful("bob"));
    CHECK(railVoltsMeaningful("s6c"));
}

void testStatusAndUsb() {
    const std::map<std::string, int> f = parseStatusDetailed(readFixture("status_detailed.txt"));
    CHECK(f.size() == 25);
    CHECK(f.at("USB UV") == 1);
    CHECK(f.at("CC") == 0);
    CHECK(f.at("JEITA") == 0);
    CHECK(f.at("Input OK") == 0);
    CHECK(f.at("CHG Ctrl OK") == 0);
    CHECK(f.at("QPH VBAT1 OK") == 0);
    CHECK(f.at("QPH2") == 0);
    CHECK(f.count("USB OV") == 1);

    const std::map<std::string, int> odd = parseStatusDetailed("CC: 1\r\nbad line\nName: text\n: 3\nCV:   1  \nICL: 1 junk\nBoost: -1");
    CHECK(odd.at("CC") == 1);  // CRLF tolerated
    CHECK(odd.at("CV") == 1);
    CHECK(odd.at("Boost") == -1);
    CHECK(odd.count("bad line") == 0);
    CHECK(odd.count("Name") == 0);
    CHECK(odd.count("ICL") == 0);  // trailing junk after the integer: skipped
    CHECK(odd.size() == 3);
    CHECK(parseStatusDetailed("").empty());

    CHECK(activeUsbType("[C] PD PD_PPS") == "C");
    CHECK(activeUsbType("C [PD] PD_PPS") == "PD");
    CHECK(activeUsbType("Unknown SDP DCP [CDP] ACA") == "CDP");
    CHECK(activeUsbType("C PD PD_PPS") == "C PD PD_PPS");
    CHECK(activeUsbType("C PD\n") == "C PD");
    CHECK(activeUsbType("") == "");
    CHECK(activeUsbType("[C PD") == "[C PD");  // unclosed bracket
}

ChargeModeInputs plugged(const char* status) {
    ChargeModeInputs in;
    in.haveBattery = true;
    in.haveCharger = true;
    in.plugged = true;
    in.batteryStatus = status;
    return in;
}

void testChargeMode() {
    ChargeModeInputs none;
    CHECK(chargeMode(none) == "");

    ChargeModeInputs battOnly;
    battOnly.haveBattery = true;
    CHECK(chargeMode(battOnly) == "Unplugged");  // battery found, not plugged

    ChargeModeInputs un = plugged("Discharging");
    un.plugged = false;
    un.flags["Boost"] = 1;  // Unplugged beats everything
    CHECK(chargeMode(un) == "Unplugged");

    ChargeModeInputs in = plugged("Charging");
    in.flags["Boost"] = 1;
    in.flags["CC"] = 1;
    CHECK(chargeMode(in) == "Boost");

    in = plugged("Full");
    in.chargingPause = 1;
    in.chargingEnable = 0;
    in.flags["CC"] = 1;
    CHECK(chargeMode(in) == "Full");

    in = plugged("Charging");
    in.flags["Boost"] = 1;
    in.batteryStatus = "Full";
    CHECK(chargeMode(in) == "Boost");  // Boost before Full

    in = plugged("Not charging");
    in.chargingPause = 1;
    in.chargingEnable = 0;
    in.flags["CC"] = 1;
    CHECK(chargeMode(in) == "Paused");  // Paused before Off

    in = plugged("Not charging");
    in.chargingEnable = 0;
    in.flags["CC"] = 1;
    CHECK(chargeMode(in) == "Off");

    in = plugged("Charging");
    in.chargingEnable = 1;
    in.chargingPause = 0;
    in.flags["CC"] = 1;
    CHECK(chargeMode(in) == "CC");

    in.flags["CC"] = 0;
    in.flags["CV"] = 1;
    CHECK(chargeMode(in) == "CV");

    in.flags["CC"] = 1;
    in.flags["CV"] = 0;
    in.flags["ICL"] = 1;
    CHECK(chargeMode(in) == "CC+ICL");

    in = plugged("Charging");
    in.flags["ICL"] = 1;
    CHECK(chargeMode(in) == "ICL");

    in = plugged("Charging");
    in.flags["JEITA"] = 1;
    CHECK(chargeMode(in) == "JEITA");

    in = plugged("Charging");
    in.flags["CC"] = 0;
    in.flags["CV"] = 0;
    CHECK(chargeMode(in) == "Charging");

    in = plugged("Not charging");
    CHECK(chargeMode(in) == "Plugged");
    in = plugged("Discharging");
    CHECK(chargeMode(in) == "Plugged");

    // Charger found but no battery still gives a mode
    ChargeModeInputs chargerOnly;
    chargerOnly.haveCharger = true;
    chargerOnly.plugged = true;
    CHECK(chargeMode(chargerOnly) == "Plugged");
}

void testTimeLeft() {
    CHECK(near(timeLeftSeconds(true, "Charging", 1.0, 1534, 2730), (2730 - 1534) / 1000.0 / 1.0 * 3600.0));
    CHECK(near(timeLeftSeconds(true, "Charging", 1.0, 1534, 2730), 4305.6));
    CHECK(near(timeLeftSeconds(false, "Discharging", -1.33375, 1534, 2730), 1534 / 1000.0 / 1.33375 * 3600.0));
    CHECK(near(timeLeftSeconds(true, "Charging", 2.0, 2730, 2730), 0.0));
    CHECK(near(timeLeftSeconds(true, "Charging", 2.0, 2800, 2730), 0.0));  // clamped at 0

    // Wrong state
    CHECK(isNan(timeLeftSeconds(true, "Discharging", 1.0, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(true, "Full", 1.0, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(false, "Charging", -1.0, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(false, "Not charging", -1.0, 1534, 2730)));
    // Wrong current sign
    CHECK(isNan(timeLeftSeconds(true, "Charging", -1.0, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(false, "Discharging", 1.0, 1534, 2730)));
    // |A| < 0.05
    CHECK(isNan(timeLeftSeconds(true, "Charging", 0.04, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(false, "Discharging", -0.049, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(true, "Charging", 0.0, 1534, 2730)));
    CHECK(!isNan(timeLeftSeconds(true, "Charging", 0.05, 1534, 2730)));
    // NaN inputs
    CHECK(isNan(timeLeftSeconds(true, "Charging", kNaN, 1534, 2730)));
    CHECK(isNan(timeLeftSeconds(true, "Charging", 1.0, kNaN, 2730)));
    CHECK(isNan(timeLeftSeconds(true, "Charging", 1.0, 1534, kNaN)));
    CHECK(isNan(timeLeftSeconds(false, "Discharging", -1.0, kNaN, 2730)));
    CHECK(!isNan(timeLeftSeconds(false, "Discharging", -1.0, 1534, kNaN)));  // full isn't needed to empty
}

void testSag() {
    CHECK(near(sagVolts(9.0, 8.6), 0.4));
    CHECK(near(sagVolts(9.0, 9.0), 0.0));
    CHECK(near(sagVolts(8.9, 9.1), 0.0));  // clamped at 0
    CHECK(isNan(sagVolts(kNaN, 8.0)));
    CHECK(isNan(sagVolts(9.0, kNaN)));
    CHECK(isNan(sagVolts(kNaN, kNaN)));
}

void testGeneral() {
    double up = -1;
    CHECK(parseUptime(readFixture("uptime.txt").c_str(), up));
    CHECK(near(up, 8914.95));
    CHECK(!parseUptime("", up));
    CHECK(!parseUptime("abc", up));
    CHECK(!parseUptime(nullptr, up));
    CHECK(near(up, 8914.95));  // failures leave the result alone

    long long pages = -1;
    CHECK(parseStatmResident(readFixture("statm.txt").c_str(), pages));
    CHECK(pages == 292);
    CHECK(!parseStatmResident("", pages));
    CHECK(!parseStatmResident("617", pages));
    CHECK(!parseStatmResident("x y", pages));
    CHECK(!parseStatmResident(nullptr, pages));
    CHECK(pages == 292);

    CHECK(near(selfWatts(0.5, 2.0, 4.0), 1.0));
    CHECK(near(selfWatts(5.0, 2.0, 4.0), 4.0));  // share capped at 1
    CHECK(near(selfWatts(0.0, 2.0, 4.0), 0.0));
    CHECK(isNan(selfWatts(0.5, 0.0, 4.0)));  // idle system
    CHECK(isNan(selfWatts(kNaN, 2.0, 4.0)));
    CHECK(isNan(selfWatts(0.5, kNaN, 4.0)));
    CHECK(isNan(selfWatts(0.5, 2.0, kNaN)));
    CHECK(isNan(selfWatts(-0.1, 2.0, 4.0)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <fixture dir>\n", argv[0]);
        return 2;
    }
    fixtureDir = argv[1];
    testProcStat();
    testPowerTree();
    testStatusAndUsb();
    testChargeMode();
    testTimeLeft();
    testSag();
    testGeneral();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sensor-math: all checks passed\n");
    return 0;
}
