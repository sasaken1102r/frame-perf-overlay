// PR 2 readouts: /proc process scan (fixture trees) and the Wi-Fi channel / band / width / combine helpers. Linux only.
#include "hide_rules.h"
#include "process_scan.h"
#include "wifi_link.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace {

int failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                  \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

namespace fs = std::filesystem;

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

// ---- fixture /proc tree ----

class Fixture {
public:
    Fixture() {
        const char* base = std::getenv("TMPDIR");
        std::string pattern = std::string(base != nullptr ? base : "/tmp") + "/frame-perf-overlay-proc-XXXXXX";
        if (::mkdtemp(pattern.data()) == nullptr) {
            std::printf("FAIL: mkdtemp\n");
            std::exit(2);
        }
        root_ = pattern;
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    const std::string& root() const { return root_; }

    void write(const std::string& relative, const std::string& content) {
        const fs::path path = fs::path(root_) / relative;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
    }
    void mkdir(const std::string& relative) { fs::create_directories(fs::path(root_) / relative); }
    void remove(const std::string& relative) {
        std::error_code ec;
        fs::remove_all(fs::path(root_) / relative, ec);
    }
    void symlink(const std::string& target, const std::string& relative) {
        const fs::path path = fs::path(root_) / relative;
        fs::create_directories(path.parent_path());
        std::error_code ec;
        fs::remove(path, ec);
        fs::create_symlink(target, path);
    }

private:
    std::string root_;
};

/** A full /proc/[pid]/stat line (52 fields' worth is unnecessary: we stop after rsslim). */
std::string statLine(int pid, const std::string& comm, unsigned long utime, unsigned long stime,
                     unsigned long starttime, long rssPages) {
    std::string s = std::to_string(pid) + " (" + comm + ") S";
    // fields 4..13: ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt
    s += " 1 1 1 0 -1 4194304 10 0 0 0";
    s += " " + std::to_string(utime) + " " + std::to_string(stime);  // 14, 15
    s += " 0 0 20 0 1 0";                                            // 16..21 (cutime cstime prio nice threads itreal)
    s += " " + std::to_string(starttime);                            // 22
    s += " 1000000";                                                 // 23 vsize
    s += " " + std::to_string(rssPages);                             // 24
    s += " 18446744073709551615\n";                                  // 25 rsslim
    return s;
}

void writeProc(Fixture& f, int pid, const std::string& comm, unsigned long utime, unsigned long stime,
               unsigned long starttime, long rssPages) {
    f.write(std::to_string(pid) + "/stat", statLine(pid, comm, utime, stime, starttime, rssPages));
}

// ---- parseProcStat ----

void parseNormal() {
    const std::string line = statLine(42, "bash", 7, 3, 999, 256);
    ProcStat s;
    CHECK(parseProcStat(line.data(), line.size(), s));
    CHECK(s.comm == "bash");
    CHECK(s.utime == 7);
    CHECK(s.stime == 3);
    CHECK(s.starttime == 999);
    CHECK(s.rssPages == 256);
}

void parseOddComm() {
    for (const char* comm : {"Web Content", "a) b", "evil) S 1 2 3 (", "(x)"}) {
        const std::string line = statLine(7, comm, 11, 22, 333, 44);
        ProcStat s;
        CHECK(parseProcStat(line.data(), line.size(), s));
        CHECK(s.comm == comm);
        CHECK(s.utime == 11 && s.stime == 22 && s.starttime == 333 && s.rssPages == 44);
    }
}

void parseMalformed() {
    ProcStat s;
    CHECK(!parseProcStat("", 0, s));
    CHECK(!parseProcStat("123 no parens here", 18, s));
    CHECK(!parseProcStat("123 (open only", 14, s));
    const std::string full = statLine(1, "x", 1, 1, 1, 1);
    // cut off before field 24 (rss): too few fields
    const size_t cut = full.find(" 1000000");
    CHECK(!parseProcStat(full.data(), cut, s));
}

// ---- topRows ----

ProcessRow row(const std::string& name, int pid, double cpu, double rss) {
    ProcessRow r;
    r.name = name;
    r.pid = pid;
    r.cpuCorePct = cpu;
    r.rssMiB = rss;
    return r;
}

void topRowsOrdering() {
    const std::vector<ProcessRow> rows = {row("a", 1, 5, 100), row("b", 2, 50, 10), row("c", 3, 20, 300),
                                          row("d", 4, 1, 200)};
    const auto cpu = topRows(rows, false, 3, true);
    CHECK(cpu.size() == 3);
    CHECK(cpu[0].name == "b" && cpu[1].name == "c" && cpu[2].name == "a");
    const auto ram = topRows(rows, false, 3, false);
    CHECK(ram.size() == 3);
    CHECK(ram[0].name == "c" && ram[1].name == "d" && ram[2].name == "a");
    // fewer rows than max: all, still sorted
    const auto all = topRows(rows, false, 10, true);
    CHECK(all.size() == 4 && all[3].name == "d");
    CHECK(topRows(rows, false, 0, true).empty());
}

void topRowsTieBreak() {
    // equal keys: name, then pid
    const std::vector<ProcessRow> rows = {row("z", 1, 10, 10), row("a", 9, 10, 10), row("a", 3, 10, 10)};
    const auto out = topRows(rows, false, 3, true);
    CHECK(out[0].name == "a" && out[0].pid == 3);
    CHECK(out[1].name == "a" && out[1].pid == 9);
    CHECK(out[2].name == "z");
    // the same with the partial-sort path (max < size)
    const auto cut = topRows(rows, false, 2, false);
    CHECK(cut.size() == 2 && cut[0].pid == 3 && cut[1].pid == 9);
}

void topRowsSumSameNameValues() {
    const std::vector<ProcessRow> rows = {row("chrome", 1, 10, 100), row("chrome", 2, 15, 200), row("chrome", 3, 5, 50),
                                          row("vim", 4, 29, 20)};
    const auto cpu = topRows(rows, true, 5, true);
    CHECK(cpu.size() == 2);
    CHECK(cpu[0].name == "chrome");
    CHECK(cpu[0].pid == 0);
    CHECK(cpu[0].count == 3);
    CHECK(near(cpu[0].cpuCorePct, 30.0));
    CHECK(near(cpu[0].rssMiB, 350.0));
    CHECK(cpu[1].name == "vim");
    const auto ram = topRows(rows, true, 1, false);
    CHECK(ram.size() == 1 && ram[0].name == "chrome");
}

// ---- ProcessScanner ----

void scannerRamAndFirstScan() {
    Fixture f;
    writeProc(f, 100, "busy", 0, 0, 5000, 1024);   // 4 MiB at 4096 B pages
    writeProc(f, 200, "big", 0, 0, 6000, 25600);   // 100 MiB
    writeProc(f, 300, "idle", 0, 0, 7000, 256);    // 1 MiB
    f.mkdir("self");                                // non-pid dirs are ignored
    f.mkdir("12abc");
    f.mkdir("0");
    f.write("cpuinfo", "x");

    ProcessScanner scanner(f.root(), 100, 4096);
    ProcessListOptions opts;
    const ProcessLists& l = scanner.scan(10.0, opts);
    CHECK(!l.cpuValid);
    CHECK(l.byCpu.empty());
    CHECK(l.scanned == 3);
    CHECK(l.vanished == 0);
    CHECK(l.byRam.size() == 3);
    CHECK(l.byRam[0].name == "big" && near(l.byRam[0].rssMiB, 100.0));
    CHECK(l.byRam[1].name == "busy" && near(l.byRam[1].rssMiB, 4.0));
    CHECK(l.byRam[2].name == "idle" && near(l.byRam[2].rssMiB, 1.0));
}

void scannerCpuFromTwoScans() {
    Fixture f;
    writeProc(f, 100, "busy", 100, 0, 5000, 1024);
    writeProc(f, 200, "calm", 10, 0, 6000, 1024);
    writeProc(f, 300, "idle", 5, 5, 7000, 1024);

    ProcessScanner scanner(f.root(), 100, 4096);
    ProcessListOptions opts;
    scanner.scan(10.0, opts);
    // 1 s later: busy +50 ticks (utime 30 + stime 20), calm +10, idle +0
    writeProc(f, 100, "busy", 130, 20, 5000, 1024);
    writeProc(f, 200, "calm", 20, 0, 6000, 1024);
    const ProcessLists& l = scanner.scan(11.0, opts);
    CHECK(l.cpuValid);
    CHECK(l.byCpu.size() == 3);
    CHECK(l.byCpu[0].name == "busy" && near(l.byCpu[0].cpuCorePct, 50.0));
    CHECK(l.byCpu[1].name == "calm" && near(l.byCpu[1].cpuCorePct, 10.0));
    CHECK(l.byCpu[2].name == "idle" && near(l.byCpu[2].cpuCorePct, 0.0));

    // an interval of 2 s halves the percentage
    writeProc(f, 100, "busy", 230, 20, 5000, 1024);  // +100 ticks over 2 s = 50%
    const ProcessLists& l2 = scanner.scan(13.0, opts);
    CHECK(near(l2.byCpu[0].cpuCorePct, 50.0));

    // more than one core: 300 ticks in 1 s = 300%
    writeProc(f, 100, "busy", 530, 20, 5000, 1024);
    const ProcessLists& l3 = scanner.scan(14.0, opts);
    CHECK(near(l3.byCpu[0].cpuCorePct, 300.0));
}

void scannerCpuMaxAndSum() {
    Fixture f;
    for (int i = 0; i < 5; ++i) writeProc(f, 100 + i, "worker", 0, 0, 1000 + i, 256);
    writeProc(f, 200, "solo", 0, 0, 2000, 256);
    ProcessScanner scanner(f.root(), 100, 4096);
    ProcessListOptions opts;
    opts.cpuMax = 2;
    opts.ramMax = 3;
    scanner.scan(0.5, opts);
    for (int i = 0; i < 5; ++i) writeProc(f, 100 + i, "worker", 10, 0, 1000 + i, 256);
    writeProc(f, 200, "solo", 30, 0, 2000, 256);
    const ProcessLists& l = scanner.scan(1.5, opts);
    CHECK(l.byCpu.size() == 2);
    CHECK(l.byCpu[0].name == "solo");
    CHECK(l.byRam.size() == 3);

}

void scannerSumSameNameOrder() {
    Fixture f;
    for (int i = 0; i < 5; ++i) writeProc(f, 100 + i, "worker", 0, 0, 1000 + i, 256);
    writeProc(f, 200, "solo", 0, 0, 2000, 256);
    ProcessScanner scanner(f.root(), 100, 4096);
    ProcessListOptions opts;
    opts.sumSameName = true;
    scanner.scan(0.0 + 1.0, opts);
    for (int i = 0; i < 5; ++i) writeProc(f, 100 + i, "worker", 10, 0, 1000 + i, 256);  // 10% each, 50% total
    writeProc(f, 200, "solo", 30, 0, 2000, 256);                                         // 30%
    const ProcessLists& l = scanner.scan(2.0, opts);
    CHECK(l.byCpu.size() == 2);
    CHECK(l.byCpu[0].name == "worker" && l.byCpu[0].count == 5 && l.byCpu[0].pid == 0);
    CHECK(near(l.byCpu[0].cpuCorePct, 50.0));
    CHECK(near(l.byCpu[0].rssMiB, 5.0));
    CHECK(l.byCpu[1].name == "solo");
}

void scannerVanishedPids() {
    Fixture f;
    writeProc(f, 100, "ok", 0, 0, 1000, 256);
    f.mkdir("300");                          // pid dir, no stat file (exited between readdir and open)
    f.write("301/stat", "");                 // empty read
    f.write("302/stat", "302 (cut) S 1 1");  // truncated
    f.write("303/stat", "garbage with no parens\n");
    ProcessScanner scanner(f.root(), 100, 4096);
    const ProcessLists& l = scanner.scan(1.0, ProcessListOptions());
    CHECK(l.scanned == 1);
    CHECK(l.vanished == 4);
    CHECK(l.byRam.size() == 1 && l.byRam[0].name == "ok");

    // a pid that was there on the first scan and is gone on the second: no crash, not listed
    f.remove("100");
    writeProc(f, 101, "new", 0, 0, 1100, 256);
    const ProcessLists& l2 = scanner.scan(2.0, ProcessListOptions());
    CHECK(l2.cpuValid);
    CHECK(l2.byCpu.size() == 1 && l2.byCpu[0].name == "new" && near(l2.byCpu[0].cpuCorePct, 0.0));  // no baseline: 0%
}

void scannerReusedPid() {
    Fixture f;
    writeProc(f, 100, "old", 1000, 0, 5000, 256);
    ProcessScanner scanner(f.root(), 100, 4096);
    scanner.scan(1.0, ProcessListOptions());
    // same pid, another process (different starttime) with fewer ticks, then one with more ticks
    writeProc(f, 100, "reborn", 50, 0, 9000, 256);
    const ProcessLists& l = scanner.scan(2.0, ProcessListOptions());
    CHECK(l.byCpu.size() == 1 && near(l.byCpu[0].cpuCorePct, 0.0));
    writeProc(f, 100, "reborn2", 5000000, 0, 9500, 256);  // would read as a huge % if the baseline were reused
    const ProcessLists& l2 = scanner.scan(3.0, ProcessListOptions());
    CHECK(l2.byCpu.size() == 1 && near(l2.byCpu[0].cpuCorePct, 0.0));
}

void scannerReset() {
    Fixture f;
    writeProc(f, 100, "p", 0, 0, 5000, 256);
    ProcessScanner scanner(f.root(), 100, 4096);
    scanner.scan(1.0, ProcessListOptions());
    CHECK(scanner.scan(2.0, ProcessListOptions()).cpuValid);
    scanner.reset();
    const ProcessLists& l = scanner.scan(3.0, ProcessListOptions());
    CHECK(!l.cpuValid && l.byCpu.empty());
    CHECK(scanner.scan(4.0, ProcessListOptions()).cpuValid);
    CHECK(scanner.lists().cpuValid);
}

void scannerMissingRoot() {
    ProcessScanner scanner("/nonexistent/frame-perf-overlay-proc", 100, 4096);
    const ProcessLists& l = scanner.scan(1.0, ProcessListOptions());
    CHECK(l.scanned == 0 && l.byRam.empty() && !l.cpuValid);
}


// ---- processDisplayName: comm, unless comm is a cut-off start of the exe (or the other way round) ----

void displayNameFromCapturedList(const std::string& fixtureDir) {
    std::ifstream in(fixtureDir + "/frame_processes_2026-10-17.txt");
    CHECK(in.good());
    std::map<std::string, std::string> got;  // comm -> display name (first line wins)
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        const size_t a = line.find('|');
        const size_t b = line.find('|', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        const std::string comm = line.substr(0, a);
        const std::string exe = line.substr(a + 1, b - a - 1);
        const std::string name = processDisplayName(comm, exe);
        got.emplace(comm + "|" + exe, name);
        if (exe.empty()) CHECK(name == comm);
        ++lines;
    }
    CHECK(lines > 100);
    const std::pair<const char*, const char*> want[] = {
        {"run_vrlink.sh|bash", "run_vrlink.sh"},
        {"start-gamescope|bash", "start-gamescope"},
        {"at-spi-bus-laun|at-spi-bus-launcher", "at-spi-bus-launcher"},
        {"vrlinkrunthread|vrlink", "vrlink"},
        {"XRServiceLoopTh|XRService", "XRService"},
        {"dbus-broker-lau|dbus-broker-launch", "dbus-broker-launch"},
        {"V4L2Cam|v4l2cam", "V4L2Cam"},
        {"bash|bash", "bash"},
    };
    for (const auto& w : want) {
        const auto it = got.find(w.first);
        CHECK(it != got.end());
        if (it != got.end() && it->second != w.second) {
            std::printf("  %s -> %s (want %s)\n", w.first, it->second.c_str(), w.second);
            CHECK(it->second == w.second);
        }
    }
    CHECK(processDisplayName("UnityMain", "app_process64") == "UnityMain");
    CHECK(processDisplayName("exact", "exact") == "exact");
}

/** A process whose exe link points at <dir>/<exeName>. */
void writeProcWithExe(Fixture& f, int pid, const std::string& comm, const std::string& exeName,
                      unsigned long utime, unsigned long starttime, const std::string& cgroup = "/user.slice/x") {
    writeProc(f, pid, comm, utime, 0, starttime, 256);
    f.symlink("/usr/bin/" + exeName, std::to_string(pid) + "/exe");
    f.write(std::to_string(pid) + "/cgroup", "0::" + cgroup + "\n");
}

void scannerDisplayNames() {
    // two scripts run by bash: different comm, same exe. Summing groups by what is shown, so they stay two rows
    Fixture f;
    writeProcWithExe(f, 100, "run_vrlink.sh", "bash", 0, 1000);
    writeProcWithExe(f, 101, "start-gamescope", "bash", 0, 1001);
    ProcessScanner scanner(f.root(), 100, 4096);
    ProcessListOptions opts;
    opts.sumSameName = true;
    const ProcessLists& l = scanner.scan(1.0, opts);
    CHECK(l.byRam.size() == 2);
    std::set<std::string> names;
    for (const ProcessRow& r : l.byRam) {
        names.insert(r.name);
        CHECK(r.exe == "bash");
        CHECK(r.count == 1);
    }
    CHECK(names == (std::set<std::string> {"run_vrlink.sh", "start-gamescope"}));

    // two renamed main threads of one program: one row under the exe name
    Fixture g;
    writeProcWithExe(g, 200, "vrlinkrunthread", "vrlink", 0, 2000);
    writeProcWithExe(g, 201, "vrlinkrunthread", "vrlink", 0, 2001);
    ProcessScanner scannerG(g.root(), 100, 4096);
    const ProcessLists& lg = scannerG.scan(1.0, opts);
    CHECK(lg.byRam.size() == 1);
    CHECK(lg.byRam.size() == 1 && lg.byRam[0].name == "vrlink" && lg.byRam[0].count == 2);
    CHECK(lg.byRam.size() == 1 && lg.byRam[0].comm == "vrlinkrunthread");
}

void scannerHideRulesMatchCommNotDisplayName() {
    Fixture f;
    Fixture rulesDir;
    // a script under /system.slice: shows as "run_vrlink.sh" (comm), exe is "bash"
    writeProcWithExe(f, 100, "run_vrlink.sh", "bash", 0, 1000, "/system.slice/x.service");
    // a cut-off comm: shown as "vrlink" but rules must still see comm and exe
    writeProcWithExe(f, 101, "vrlinkrunthread", "vrlink", 0, 1001, "/user.slice/session-1.scope");
    writeProcWithExe(f, 102, "keepme", "keepme", 0, 1002, "/user.slice/session-1.scope");
    rulesDir.write("hide_rules.json",
                   "{\"version\": 1, \"hide\": [{\"name\": \"run_vrlink.sh\"}, {\"name\": \"vrlinkrunthread\"}]}");
    HideRules rules(rulesDir.root() + "/hide_rules.json", 0);
    ProcessScanner scanner(f.root(), 100, 4096);
    scanner.setHideRules(&rules);
    ProcessListOptions opts;
    opts.cpuHideSystem = true;
    opts.ramHideSystem = true;
    scanner.scan(1.0, opts);
    const ProcessLists& l = scanner.scan(2.0, opts);
    CHECK(l.byRam.size() == 1 && l.byRam[0].name == "keepme");
    CHECK(l.byCpu.size() == 1 && l.byCpu[0].name == "keepme");
    CHECK(!l.hideRulesError);

    // matching the exe still works for a cut-off comm: the display name "vrlink" is not what is matched,
    // but the exe basename is
    rulesDir.write("hide_rules.json", "{\"version\": 1, \"hide\": [{\"name\": \"vrlink\"}]}");
    struct timespec times[2];
    times[0].tv_sec = times[1].tv_sec = ::time(nullptr) + 100;
    times[0].tv_nsec = times[1].tv_nsec = 0;
    ::utimensat(AT_FDCWD, (rulesDir.root() + "/hide_rules.json").c_str(), times, 0);
    const ProcessLists& l2 = scanner.scan(3.0, opts);
    CHECK(l2.byRam.size() == 2);
    for (const ProcessRow& r : l2.byRam) CHECK(r.name != "vrlink");
}

// ---- wifi_link.h pure functions ----

void channels() {
    CHECK(channelFromFreq(2412) == 1);
    CHECK(channelFromFreq(2437) == 6);
    CHECK(channelFromFreq(2472) == 13);
    CHECK(channelFromFreq(2484) == 14);
    CHECK(channelFromFreq(5180) == 36);
    CHECK(channelFromFreq(5500) == 100);
    CHECK(channelFromFreq(5825) == 165);
    CHECK(channelFromFreq(5935) == 2);
    CHECK(channelFromFreq(5955) == 1);
    CHECK(channelFromFreq(6135) == 37);
    CHECK(channelFromFreq(7115) == 233);
    CHECK(channelFromFreq(58320) == 1);
    CHECK(channelFromFreq(0) == 0);
    CHECK(channelFromFreq(1234) == 0);
}

void bands() {
    CHECK(bandFromFreq(2412) == 1);
    CHECK(bandFromFreq(2484) == 1);
    CHECK(bandFromFreq(5180) == 2);
    CHECK(bandFromFreq(5885) == 2);
    CHECK(bandFromFreq(5935) == 3);
    CHECK(bandFromFreq(6135) == 3);
    CHECK(bandFromFreq(58320) == 4);
    CHECK(bandFromFreq(0) == 0);
    CHECK(bandFromFreq(3000) == 0);
}

void widths() {
    CHECK(widthMhzFromNl80211(0) == 20);
    CHECK(widthMhzFromNl80211(1) == 20);
    CHECK(widthMhzFromNl80211(2) == 40);
    CHECK(widthMhzFromNl80211(3) == 80);
    CHECK(widthMhzFromNl80211(4) == 160);
    CHECK(widthMhzFromNl80211(5) == 160);
    CHECK(widthMhzFromNl80211(13) == 320);
    CHECK(widthMhzFromNl80211(99) == 0);
}

AdapterInfo adapter(bool present, bool connected, double signal, double rx) {
    AdapterInfo a;
    a.present = present;
    a.connected = connected;
    a.signalDbm = signal;
    a.rxMbps = rx;
    return a;
}

void combine() {
    WifiAdapters both;
    both.dongle = adapter(true, true, -45, 200);
    both.home = adapter(true, true, -55, 10);
    WifiInfo i = combineAdapters(both);
    CHECK(i.interfaceUp && i.connected && !i.homeWifi);
    CHECK(i.signalDbm == -45 && i.rxMbps == 200);

    both.dongle.connected = false;
    i = combineAdapters(both);
    CHECK(i.connected && i.homeWifi && i.signalDbm == -55 && i.rxMbps == 10);

    both.home.connected = false;
    i = combineAdapters(both);
    CHECK(i.interfaceUp && !i.connected);
    CHECK(std::isnan(i.signalDbm));

    CHECK(!combineAdapters(WifiAdapters()).interfaceUp);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string fixtures = argc > 1 ? argv[1] : "tests/fixtures";
    parseNormal();
    parseOddComm();
    parseMalformed();
    topRowsOrdering();
    topRowsTieBreak();
    topRowsSumSameNameValues();
    scannerRamAndFirstScan();
    scannerCpuFromTwoScans();
    scannerCpuMaxAndSum();
    scannerSumSameNameOrder();
    scannerVanishedPids();
    scannerReusedPid();
    scannerReset();
    scannerMissingRoot();
    displayNameFromCapturedList(fixtures);
    scannerDisplayNames();
    scannerHideRulesMatchCommNotDisplayName();
    channels();
    bands();
    widths();
    combine();
    if (failures == 0) std::printf("process-wifi-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
