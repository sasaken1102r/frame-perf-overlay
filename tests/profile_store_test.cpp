// Layout profiles. ProfileStore (round trip, the 8 cap, rename / delete, atomic writes, unsaved tracking) and
// its use with LayoutAuthority (what a PC's LAYOUT_SET and the Reset button do). POSIX only; works in a temp dir and
// never touches ~/.config.
#include "config.h"
#include "layout.h"
#include "layout_authority.h"
#include "layout_edit.h"
#include "profile_store.h"

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

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

using frame_perf_overlay::Layout;
using frame_perf_overlay::WidgetInstance;
using W = frame_perf_overlay::WidgetType;

std::string makeTempDir() {
    char tmpl[] = "/tmp/frame-perf-overlay-profiles-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    return dir != nullptr ? dir : "";
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") names.push_back(n);
        }
        closedir(d);
    }
    return names;
}

bool hasTmp(const std::string& dir) {
    for (const std::string& n : listDir(dir)) {
        if (n.find(".tmp") != std::string::npos) return true;
    }
    return false;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

bool exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

void rmTree(const std::string& dir) {
    for (const std::string& n : listDir(dir)) std::remove((dir + "/" + n).c_str());
    rmdir(dir.c_str());
}

WidgetInstance inst(W type) {
    WidgetInstance w;
    w.type = type;
    return w;
}

Layout make(const std::vector<std::vector<W>>& columns) {
    Layout layout;
    for (const std::vector<W>& types : columns) {
        frame_perf_overlay::LayoutColumn column;
        for (const W t : types) column.widgets.push_back(inst(t));
        layout.columns.push_back(column);
    }
    return layout;
}

/** A layout that is different from every other `make({{...}})` below by its column count. */
Layout variant(size_t n) {
    // one column each, every widget at most once (a layout file repeats none)
    const W types[] = {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::Network, W::General};
    Layout layout;
    for (size_t i = 0; i < n; ++i) layout.columns.push_back({{inst(types[i % 8])}});
    return layout;
}

/** Every widget type, in two columns, with the per-widget setting layout.h has (Compact) switched on. */
Layout fullLayout() {
    Layout layout;
    layout.version = 7;
    frame_perf_overlay::LayoutColumn a;
    for (const W t : {W::Cpu, W::GpuRam, W::Network}) a.widgets.push_back(inst(t));
    layout.columns.push_back(a);
    frame_perf_overlay::LayoutColumn b;
    for (const W t : {W::Frame, W::Power, W::Battery, W::Temp, W::General}) {
        WidgetInstance w = inst(t);
        w.compact = frame_perf_overlay::hasCompactView(t);
        b.widgets.push_back(w);
    }
    layout.columns.push_back(b);
    return layout;
}

// 1. toJson -> fromJson
void roundTrip(const Layout& layout, const char* what) {
    Profile in;
    in.id = "profile-1";
    in.name = std::string("Round ") + what;
    in.layout = layout;
    Profile out;
    std::vector<std::string> warnings;
    std::string error;
    const bool ok = ProfileStore::fromJson(ProfileStore::toJson(in), out, warnings, error);
    if (!ok) std::printf("  (%s: %s)\n", what, error.c_str());
    CHECK(ok);
    CHECK(out.layout.sameWidgets(in.layout));
    CHECK(out.name == in.name);
    if (!warnings.empty()) std::printf("  (%s warning: %s)\n", what, warnings.front().c_str());
    CHECK(warnings.empty());
}

void testRoundTrip() {
    roundTrip(frame_perf_overlay::defaultLayout(), "default");
    const Layout full = fullLayout();
    roundTrip(full, "full");
    // The full layout really carries the settings (so a round trip that dropped them would fail above)
    CHECK(full.columns[1].widgets[2].compact && full.columns[1].widgets[3].compact);

    // an old file (no compact / name keys) still loads with defaults
    const std::string old =
        "{ \"layout\": { \"version\": 3, \"columns\": [ [ {\"type\":\"frame\"}, {\"type\":\"battery\"} ] ] } }";
    Profile p;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(ProfileStore::fromJson(old, p, warnings, error));
    CHECK(p.name.empty());  // load() names it "Profile N"
    CHECK(p.layout.columns.size() == 1 && p.layout.columns[0].widgets.size() == 2);
    const WidgetInstance& items = p.layout.columns[0].widgets[1];
    CHECK(items.type == W::Battery && !items.compact);
    CHECK(p.layout.columns[0].widgets[0].compact);  // a missing key: Frame compact, Battery / Temp full

    // Not a profile at all
    CHECK(!ProfileStore::fromJson("{ \"name\": \"x\" }", p, warnings, error));
    CHECK(!ProfileStore::fromJson("not json", p, warnings, error));
    CHECK(!ProfileStore::fromJson("[1,2]", p, warnings, error));
    // A name with quotes / backslash / control chars survives (jsonString escapes; cleanName keeps ", \ and drops control)
    Profile q;
    q.id = "profile-1";
    q.name = "a\"b\\c";
    q.layout = make({{W::Frame}});
    Profile r;
    CHECK(ProfileStore::fromJson(ProfileStore::toJson(q), r, warnings, error));
    CHECK(r.name == q.name);
}

// 2. the 8 cap
void testCap() {
    const std::string dir = makeTempDir();
    ProfileStore store(dir);
    std::vector<std::string> warnings;
    store.load(warnings);
    CHECK(store.profiles().empty() && store.canCreate());
    for (size_t n = 1; n <= 8; ++n) {
        std::string id, error;
        CHECK(store.create(variant(n), id, error));
        CHECK(id == "profile-" + std::to_string(n));
        CHECK(store.find(id) != nullptr && store.find(id)->name == "Profile " + std::to_string(n));
        CHECK(exists(dir + "/" + id + ".json"));
    }
    CHECK(store.profiles().size() == 8 && !store.canCreate());
    const std::vector<std::string> before = listDir(dir);
    std::string id = "untouched", error;
    CHECK(!store.create(variant(9), id, error));
    CHECK(!error.empty());
    CHECK(id == "untouched");
    CHECK(store.profiles().size() == 8);
    CHECK(listDir(dir).size() == before.size());  // nothing written (and no stray temp)

    // delete 3, the next create reuses it
    CHECK(store.remove("profile-3", error));
    CHECK(store.canCreate() && store.find("profile-3") == nullptr);
    CHECK(store.create(variant(3), id, error));
    CHECK(id == "profile-3" && store.find(id)->name == "Profile 3");
    for (size_t i = 0; i < store.profiles().size(); ++i) {  // kept sorted by id number
        CHECK(store.profiles()[i].id == "profile-" + std::to_string(i + 1));
    }

    // a fresh store sees all 8, in order
    ProfileStore again(dir);
    again.load(warnings);
    CHECK(again.profiles().size() == 8);
    CHECK(again.profiles()[2].layout.sameWidgets(variant(3)));
    CHECK(warnings.empty());
    rmTree(dir);
}

// 3. rename / delete
void testRenameDelete() {
    const std::string dir = makeTempDir();
    std::vector<std::string> warnings;
    std::string id, error;
    {
        ProfileStore store(dir);
        store.load(warnings);
        CHECK(store.create(make({{W::Frame}}), id, error));
        const uint64_t rev = store.revision();
        CHECK(store.rename(id, "Gaming", error));
        CHECK(store.revision() > rev);
    }
    {
        ProfileStore fresh(dir);
        fresh.load(warnings);
        CHECK(fresh.find("profile-1") != nullptr && fresh.find("profile-1")->name == "Gaming");
    }

    ProfileStore store(dir);
    store.load(warnings);
    CHECK(store.rename(id, "   padded \t ", error) && store.find(id)->name == "padded");
    // empty / blank = unchanged, and still "success"
    CHECK(store.rename(id, "", error) && store.find(id)->name == "padded");
    CHECK(store.rename(id, "  \n ", error) && store.find(id)->name == "padded");
    // control chars inside become spaces
    CHECK(store.rename(id, "a\nb", error) && store.find(id)->name == "a b");
    // 31 bytes max
    const std::string longName(60, 'x');
    CHECK(store.rename(id, longName, error) && store.find(id)->name == std::string(31, 'x'));
    // multi-byte: 20 x "é" (2 bytes) = 40 bytes -> 15 whole characters = 30 bytes, not 31 with half a character
    std::string accents;
    for (int i = 0; i < 20; ++i) accents += "\xC3\xA9";
    CHECK(store.rename(id, accents, error));
    CHECK(store.find(id)->name.size() == 30);
    CHECK(store.find(id)->name.substr(0, 2) == "\xC3\xA9");
    // exactly 31 ASCII bytes stays whole; 32 is cut
    CHECK(ProfileStore::cleanName(std::string(31, 'y')).size() == 31);
    CHECK(ProfileStore::cleanName(std::string(32, 'y')).size() == 31);
    // 3-byte chars: 11 x "€" = 33 bytes -> 10 chars = 30 bytes
    std::string euros;
    for (int i = 0; i < 11; ++i) euros += "\xE2\x82\xAC";
    CHECK(ProfileStore::cleanName(euros).size() == 30);
    // the saved file has the cut name too
    {
        ProfileStore fresh(dir);
        fresh.load(warnings);
        CHECK(fresh.find(id)->name.size() == 30);
    }
    CHECK(!store.rename("profile-6", "x", error) && !error.empty());

    // delete
    CHECK(!store.remove("profile-6", error) && !error.empty());
    CHECK(!store.remove("nonsense", error));
    CHECK(exists(store.pathOf(id)));
    CHECK(store.remove(id, error));
    CHECK(!exists(store.pathOf(id)));
    CHECK(store.profiles().empty() && store.find(id) == nullptr);
    CHECK(!store.remove(id, error));  // already gone
    rmTree(dir);
}

// 4. atomic writes, garbage files, strangers
void testFilesInDir() {
    const std::string dir = makeTempDir();
    std::vector<std::string> warnings;
    std::string id, error;
    ProfileStore store(dir);
    store.load(warnings);
    CHECK(store.create(make({{W::Frame}}), id, error));
    CHECK(!hasTmp(dir));
    CHECK(store.save(id, make({{W::Power}}), error));
    CHECK(!hasTmp(dir));
    CHECK(store.rename(id, "Renamed", error));
    CHECK(!hasTmp(dir));
    CHECK(listDir(dir).size() == 1);
    CHECK(!store.save("profile-2", make({{W::Power}}), error) && !error.empty());  // nothing to overwrite
    CHECK(!exists(dir + "/profile-2.json"));

    spit(dir + "/profile-5.json", "this is { not json");
    spit(dir + "/profile-6.json", "{ \"name\": \"no layout\" }");
    spit(dir + "/notes.txt", "hi");
    spit(dir + "/profile-9.json", slurp(store.pathOf(id)));  // 9 is out of range
    spit(dir + "/profile-0.json", slurp(store.pathOf(id)));
    spit(dir + "/profile-1.json.bak", slurp(store.pathOf(id)));

    ProfileStore fresh(dir);
    std::vector<std::string> warn;
    fresh.load(warn);
    CHECK(fresh.profiles().size() == 1 && fresh.profiles()[0].id == "profile-1");
    CHECK(fresh.profiles()[0].name == "Renamed");
    CHECK(fresh.find("profile-5") == nullptr && fresh.find("profile-6") == nullptr);
    CHECK(warn.size() == 2);
    bool mention5 = false, mention6 = false, mention9 = false, mentionNotes = false;
    for (const std::string& w : warn) {
        mention5 = mention5 || w.find("profile-5") != std::string::npos;
        mention6 = mention6 || w.find("profile-6") != std::string::npos;
        mention9 = mention9 || w.find("profile-9") != std::string::npos;
        mentionNotes = mentionNotes || w.find("notes") != std::string::npos;
    }
    CHECK(mention5 && mention6 && !mention9 && !mentionNotes);
    // The skipped garbage file is left alone, and creating takes the lowest free number (2), not 5
    CHECK(slurp(dir + "/profile-5.json") == "this is { not json");
    CHECK(fresh.create(make({{W::Battery}}), id, error) && id == "profile-2");
    CHECK(!hasTmp(dir));
    // A folder that doesn't exist yet: load is fine (empty), the first create makes it (or fails with an error)
    ProfileStore missing(dir + "/sub/profiles");
    std::vector<std::string> w2;
    missing.load(w2);
    CHECK(missing.profiles().empty() && w2.empty());
    rmTree(dir);
}

// 5. unsaved tracking, with LayoutAuthority as the PC's LAYOUT_SET reaches it
void testUnsaved() {
    const std::string dir = makeTempDir();
    std::vector<std::string> warnings;
    std::string id, error;
    ProfileStore store(dir);
    store.load(warnings);

    Config config;
    config.layout = make({{W::Frame, W::Cpu}, {W::Power}});
    int saves = 0;
    LayoutAuthority auth(
        config, [&](const Config&, std::string&) { return ++saves > 0; }, [](const Layout&) {});

    // no profiles at all: the live layout is in none of them
    CHECK(store.unsaved(auth.layout(), ""));
    CHECK(store.create(auth.layout(), id, error));
    config.activeProfile = id;
    CHECK(!store.unsaved(auth.layout(), id));

    // an edit through trySetLayout (a PC's LAYOUT_SET)
    Layout edited = auth.layout();
    edited.columns[0].widgets.push_back(inst(W::Temp));
    const auto r = auth.trySetLayout(auth.version(), edited);
    CHECK(r.applied());
    CHECK(store.unsaved(auth.layout(), id));
    // an edit that only bumps the version is not a change
    Layout same = auth.layout();
    CHECK(auth.trySetLayout(auth.version(), same).applied());
    CHECK(store.unsaved(auth.layout(), id));  // still the Temp edit
    // Save -> clean again
    CHECK(store.save(id, auth.layout(), error));
    CHECK(!store.unsaved(auth.layout(), id));
    // a pure version bump after saving stays clean
    CHECK(auth.trySetLayout(auth.version(), auth.layout()).applied());
    CHECK(!store.unsaved(auth.layout(), id));

    // no / missing active id: unsaved unless some profile equals the live layout
    CHECK(!store.unsaved(auth.layout(), ""));
    CHECK(!store.unsaved(auth.layout(), "profile-8"));  // gone id behaves like none
    CHECK(!store.unsaved(auth.layout(), "garbage"));
    Layout other = make({{W::Battery}});
    CHECK(auth.trySetLayout(auth.version(), other).applied());
    CHECK(store.unsaved(auth.layout(), ""));
    CHECK(store.unsaved(auth.layout(), "profile-8"));
    CHECK(store.unsaved(auth.layout(), id));
    // equal to a profile that is NOT the active one: active id decides (still unsaved against the active one)
    std::string id2;
    CHECK(store.create(other, id2, error));
    CHECK(!store.unsaved(auth.layout(), ""));       // matches profile-2
    CHECK(store.unsaved(auth.layout(), id));        // active = profile-1, which differs
    CHECK(!store.unsaved(auth.layout(), id2));
    rmTree(dir);
}

// A profile saved with every widget removed comes back empty (through the editor's path: LayoutAuthority)
void testEmptyProfile() {
    const std::string dir = makeTempDir();
    std::vector<std::string> warnings;
    std::string id, error;
    ProfileStore store(dir);
    store.load(warnings);
    Config config;
    config.layout = make({{W::Frame, W::Cpu}, {W::Power}});
    LayoutAuthority auth(config, [](const Config&, std::string&) { return true; }, [](const Layout&) {});
    CHECK(store.create(auth.layout(), id, error));

    Layout emptied = auth.layout();
    while (!emptied.columns.empty()) CHECK(frame_perf_overlay::removeWidget(emptied, {0, 0}));
    CHECK(auth.trySetLayout(auth.version(), emptied).applied());
    CHECK(auth.layout().columns.empty());
    CHECK(store.save(id, auth.layout(), error));
    CHECK(!store.unsaved(auth.layout(), id));
    std::string id2;
    CHECK(store.create(auth.layout(), id2, error));  // a new profile from the empty layout too

    ProfileStore fresh(dir);
    warnings.clear();
    fresh.load(warnings);
    CHECK(fresh.profiles().size() == 2);
    CHECK(fresh.find(id) != nullptr && fresh.find(id)->layout.columns.empty());
    CHECK(fresh.find(id2) != nullptr && fresh.find(id2)->layout.columns.empty());
    CHECK(warnings.empty());
    rmTree(dir);
}

// 6. Reset leaves the profile files alone
void testResetLeavesProfiles() {
    const std::string dir = makeTempDir();
    std::vector<std::string> warnings;
    std::string id, error;
    ProfileStore store(dir);
    store.load(warnings);
    Config config;
    config.layout = fullLayout();
    LayoutAuthority auth(config, [](const Config&, std::string&) { return true; }, [](const Layout&) {});
    CHECK(store.create(auth.layout(), id, error));
    config.activeProfile = id;
    std::string id2;
    CHECK(store.create(make({{W::Frame}}), id2, error));
    const std::string a = slurp(store.pathOf("profile-1")), b = slurp(store.pathOf("profile-2"));
    CHECK(!a.empty() && !b.empty());
    CHECK(!store.unsaved(auth.layout(), id));

    const auto r = auth.trySetLayout(auth.version(), frame_perf_overlay::defaultLayout());
    CHECK(r.applied());
    CHECK(auth.layout().sameWidgets(frame_perf_overlay::defaultLayout()));
    CHECK(slurp(store.pathOf("profile-1")) == a);
    CHECK(slurp(store.pathOf("profile-2")) == b);
    CHECK(store.unsaved(auth.layout(), id));
    CHECK(store.find(id)->layout.sameWidgets(fullLayout()));
    CHECK(config.activeProfile == id);  // Reset doesn't deselect it
    // a stale base is refused and leaves the live layout (Use / Reset sent a stale version)
    CHECK(auth.trySetLayout(auth.version() - 1, fullLayout()).outcome == LayoutAuthority::Outcome::Stale);
    CHECK(auth.layout().sameWidgets(frame_perf_overlay::defaultLayout()));
    rmTree(dir);
}

}  // namespace

int main() {
    testRoundTrip();
    testCap();
    testRenameDelete();
    testFilesInDir();
    testUnsaved();
    testEmptyProfile();
    testResetLeavesProfiles();
    if (failures == 0) std::printf("profile-store-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
