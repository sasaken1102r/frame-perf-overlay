// 無線（直通回線と家の Wi-Fi）の読み取りの実装。
#include "wifi_link.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <unistd.h>

#include <cstdlib>
#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
// インターフェースを探し直す間隔（秒）。直通回線のインターフェースは SteamVR が作り直すことがある
constexpr double kDiscoverIntervalSec = 10.0;

/** ステーションのダンプを受け取る間の入れ物。 */
struct StationResult {
    bool managed = false;  ///< 子機側（家の Wi-Fi）なら true。電波の選び方が変わる
    bool found = false;
    double signalDbm = kNaN;
    double txLinkMbps = kNaN;
    double rxLinkMbps = kNaN;
};

/** インターフェースのダンプを受け取る間の入れ物。 */
struct InterfaceResult {
    unsigned apIndex = 0;
    std::string apName;
    int apFreq = 0;
    int apWidth = 0;
    unsigned stationIndex = 0;
    std::string stationName;
    int stationFreq = 0;
    int stationWidth = 0;
};

/**
 * NL80211_STA_INFO_TX_BITRATE / RX_BITRATE の入れ子からリンク速度（Mbps）を取り出す。
 * @param attr ビットレートの属性
 * @return Mbps。取れなければ NaN
 */
double parseBitrate(nlattr* attr) {
    if (attr == nullptr) return kNaN;
    nlattr* rate[NL80211_RATE_INFO_MAX + 1] = {};
    if (nla_parse_nested(rate, NL80211_RATE_INFO_MAX, attr, nullptr) != 0) return kNaN;
    // どちらも 100kbps 単位
    if (rate[NL80211_RATE_INFO_BITRATE32] != nullptr) return nla_get_u32(rate[NL80211_RATE_INFO_BITRATE32]) / 10.0;
    if (rate[NL80211_RATE_INFO_BITRATE] != nullptr) return nla_get_u16(rate[NL80211_RATE_INFO_BITRATE]) / 10.0;
    return kNaN;
}

/**
 * 電波の強さを 1 つ選ぶ（0 dBm は「取れていない」扱いで飛ばす）。
 * - AP 側（直通回線）: signal が 0 dBm になって使えないので、ack signal の平均を優先する
 * - 子機側（家の Wi-Fi）: AP から届く電波の signal を優先する。実機では signal −49、beacon signal avg −48 に対し
 *   avg ack signal は −59 と 10dB 低く、signal avg は 0 だった。OS の表示と同じ signal にそろえる
 * @param info NL80211_STA_INFO_* の配列
 * @param managed 子機側なら true
 * @return dBm。取れなければ NaN
 */
double pickSignal(nlattr** info, bool managed) {
    const int apOrder[] = {NL80211_STA_INFO_ACK_SIGNAL_AVG, NL80211_STA_INFO_ACK_SIGNAL, NL80211_STA_INFO_SIGNAL_AVG,
                           NL80211_STA_INFO_SIGNAL};
    const int managedOrder[] = {NL80211_STA_INFO_SIGNAL, NL80211_STA_INFO_BEACON_SIGNAL_AVG,
                                NL80211_STA_INFO_SIGNAL_AVG, NL80211_STA_INFO_ACK_SIGNAL_AVG};
    for (const int key : managed ? managedOrder : apOrder) {
        if (info[key] == nullptr) continue;
        const int dbm = static_cast<int8_t>(nla_get_u8(info[key]));
        if (dbm != 0) return dbm;
    }
    return kNaN;
}

/**
 * nl80211 のメッセージの属性を読む。
 * @param message 受け取ったメッセージ
 * @param attrs NL80211_ATTR_MAX + 1 個の書き込み先
 */
void parseAttributes(nl_msg* message, nlattr** attrs) {
    auto* header = static_cast<genlmsghdr*>(nlmsg_data(nlmsg_hdr(message)));
    nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(header, 0), genlmsg_attrlen(header, 0), nullptr);
}

/**
 * GET_STATION のダンプで 1 ステーションごとに呼ばれる。最初のステーションだけ使う（MAC アドレスは読まない）。
 * @param message 受け取ったメッセージ
 * @param arg StationResult
 * @return NL_SKIP（続きも受け取る）
 */
int onStation(nl_msg* message, void* arg) {
    auto* result = static_cast<StationResult*>(arg);
    if (result->found) return NL_SKIP;
    nlattr* attrs[NL80211_ATTR_MAX + 1] = {};
    parseAttributes(message, attrs);
    if (attrs[NL80211_ATTR_STA_INFO] == nullptr) return NL_SKIP;
    nlattr* info[NL80211_STA_INFO_MAX + 1] = {};
    if (nla_parse_nested(info, NL80211_STA_INFO_MAX, attrs[NL80211_ATTR_STA_INFO], nullptr) != 0) return NL_SKIP;
    result->found = true;
    result->signalDbm = pickSignal(info, result->managed);
    result->txLinkMbps = parseBitrate(info[NL80211_STA_INFO_TX_BITRATE]);
    result->rxLinkMbps = parseBitrate(info[NL80211_STA_INFO_RX_BITRATE]);
    return NL_SKIP;
}

/**
 * GET_INTERFACE のダンプで 1 インターフェースごとに呼ばれる。種類・番号・名前だけ読む（MAC アドレスと SSID は読まない）。
 * @param message 受け取ったメッセージ
 * @param arg InterfaceResult
 * @return NL_SKIP（続きも受け取る）
 */
int onInterface(nl_msg* message, void* arg) {
    auto* result = static_cast<InterfaceResult*>(arg);
    nlattr* attrs[NL80211_ATTR_MAX + 1] = {};
    parseAttributes(message, attrs);
    // P2P-device などネットワークのインターフェースを持たないものには IFINDEX が無い
    if (attrs[NL80211_ATTR_IFTYPE] == nullptr || attrs[NL80211_ATTR_IFINDEX] == nullptr ||
        attrs[NL80211_ATTR_IFNAME] == nullptr) {
        return NL_SKIP;
    }
    const unsigned type = nla_get_u32(attrs[NL80211_ATTR_IFTYPE]);
    const unsigned index = nla_get_u32(attrs[NL80211_ATTR_IFINDEX]);
    const char* name = nla_get_string(attrs[NL80211_ATTR_IFNAME]);
    // Only present while the interface is on a channel
    const int freq =
        attrs[NL80211_ATTR_WIPHY_FREQ] != nullptr ? static_cast<int>(nla_get_u32(attrs[NL80211_ATTR_WIPHY_FREQ])) : 0;
    const int width = attrs[NL80211_ATTR_CHANNEL_WIDTH] != nullptr
                          ? widthMhzFromNl80211(nla_get_u32(attrs[NL80211_ATTR_CHANNEL_WIDTH]))
                          : 0;
    if (type == NL80211_IFTYPE_AP && result->apIndex == 0) {
        result->apIndex = index;
        result->apName = name;
        result->apFreq = freq;
        result->apWidth = width;
    } else if (type == NL80211_IFTYPE_STATION && result->stationIndex == 0) {
        result->stationIndex = index;
        result->stationName = name;
        result->stationFreq = freq;
        result->stationWidth = width;
    }
    return NL_SKIP;
}

/** @return the interface's first IPv4 address ("" if none) */
std::string ipv4Of(const ifaddrs* list, const std::string& name) {
    for (const ifaddrs* a = list; a != nullptr; a = a->ifa_next) {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET || name != a->ifa_name) continue;
        char text[INET_ADDRSTRLEN] = {};
        const auto* in = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)) != nullptr) return text;
    }
    return "";
}

}  // namespace

WifiInfo::WifiInfo() : signalDbm(kNaN), txLinkMbps(kNaN), rxLinkMbps(kNaN), rxMbps(kNaN), txMbps(kNaN) {}

AdapterInfo::AdapterInfo() : rxMbps(kNaN), txMbps(kNaN), txLinkMbps(kNaN), rxLinkMbps(kNaN), signalDbm(kNaN) {}

int channelFromFreq(int f) {
    if (f == 2484) return 14;
    if (f >= 2412 && f < 2484) return (f - 2407) / 5;
    if (f == 5935) return 2;  // the one 6 GHz channel below 5950 MHz
    if (f >= 5955 && f <= 7115) return (f - 5950) / 5;
    if (f >= 4910 && f <= 5895) return (f - 5000) / 5;
    if (f >= 58320 && f <= 70200) return (f - 56160) / 2160;
    return 0;
}

unsigned bandFromFreq(int f) {
    if (f >= 2400 && f < 2500) return 1;
    if (f >= 4900 && f < 5925) return 2;
    if (f >= 5925 && f <= 7125) return 3;
    if (f >= 57000 && f <= 71000) return 4;
    return 0;
}

int widthMhzFromNl80211(unsigned w) {
    // enum nl80211_chan_width, by number so older headers without the newest names still build
    switch (w) {
        case 0: return 20;   // 20_NOHT
        case 1: return 20;
        case 2: return 40;
        case 3: return 80;
        case 4: return 160;  // 80P80
        case 5: return 160;
        case 6: return 5;
        case 7: return 10;
        case 8: return 1;
        case 9: return 2;
        case 10: return 4;
        case 11: return 8;
        case 12: return 16;
        case 13: return 320;
        default: return 0;
    }
}

WifiInfo combineAdapters(const WifiAdapters& a) {
    WifiInfo info;
    info.interfaceUp = a.dongle.present || a.home.present;
    const AdapterInfo* used = a.dongle.connected ? &a.dongle : (a.home.connected ? &a.home : nullptr);
    if (used == nullptr) return info;
    info.connected = true;
    info.homeWifi = used == &a.home;
    info.signalDbm = used->signalDbm;
    info.txLinkMbps = used->txLinkMbps;
    info.rxLinkMbps = used->rxLinkMbps;
    info.rxMbps = used->rxMbps;
    info.txMbps = used->txMbps;
    return info;
}

WifiLink::Counters::Counters() : rxMbps(kNaN), txMbps(kNaN) {}

WifiLink::~WifiLink() {
    if (socket_ != nullptr) nl_socket_free(socket_);
    closeCounters(apCounters_);
    closeCounters(stationCounters_);
}

bool WifiLink::ensureSocket() {
    if (socket_ != nullptr) return true;
    socket_ = nl_socket_alloc();
    if (socket_ == nullptr) return false;
    if (genl_connect(socket_) != 0 || (family_ = genl_ctrl_resolve(socket_, "nl80211")) < 0) {
        nl_socket_free(socket_);
        socket_ = nullptr;
        return false;
    }
    return true;
}

bool WifiLink::dump(int command, unsigned ifindex, int (*callback)(struct nl_msg*, void*), void* arg) {
    if (!ensureSocket()) return false;
    nl_msg* message = nlmsg_alloc();
    if (message == nullptr) return false;
    genlmsg_put(message, NL_AUTO_PORT, NL_AUTO_SEQ, family_, 0, NLM_F_DUMP, static_cast<uint8_t>(command), 0);
    if (ifindex != 0) nla_put_u32(message, NL80211_ATTR_IFINDEX, ifindex);
    nl_socket_modify_cb(socket_, NL_CB_VALID, NL_CB_CUSTOM, callback, arg);
    const bool ok = nl_send_auto(socket_, message) >= 0 && nl_recvmsgs_default(socket_) >= 0;
    nlmsg_free(message);
    if (!ok) {
        // ソケットがおかしくなったら次回作り直す
        nl_socket_free(socket_);
        socket_ = nullptr;
    }
    return ok;
}

void WifiLink::discoverInterfaces() {
    InterfaceResult found;
    if (!dump(NL80211_CMD_GET_INTERFACE, 0, onInterface, &found)) return;  // 失敗したら前の結果のまま
    ap_ = {found.apIndex, found.apName, found.apFreq, found.apWidth, ""};
    station_ = {found.stationIndex, found.stationName, found.stationFreq, found.stationWidth, ""};
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        if (ap_.ifindex != 0) ap_.ip = ipv4Of(list, ap_.name);
        if (station_.ifindex != 0) station_.ip = ipv4Of(list, station_.name);
        freeifaddrs(list);
    }
}

bool WifiLink::readCounter(int fd, unsigned long long& value) {
    if (fd < 0) return false;
    char buffer[32];
    const ssize_t n = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return false;
    buffer[n] = '\0';
    value = std::strtoull(buffer, nullptr, 10);
    return true;
}

void WifiLink::closeCounters(Counters& counters) {
    if (counters.rxFd >= 0) ::close(counters.rxFd);
    if (counters.txFd >= 0) ::close(counters.txFd);
    counters.rxFd = -1;
    counters.txFd = -1;
    counters.lastTime = -1.0;
    counters.rxMbps = kNaN;
    counters.txMbps = kNaN;
}

void WifiLink::updateCounters(Counters& counters, const std::string& name, double now) {
    if (name != counters.name) {
        closeCounters(counters);
        counters.name = name;
    }
    if (name.empty()) return;
    if (counters.rxFd < 0) {
        const std::string base = "/sys/class/net/" + name + "/statistics/";
        counters.rxFd = ::open((base + "rx_bytes").c_str(), O_RDONLY | O_CLOEXEC);
        counters.txFd = ::open((base + "tx_bytes").c_str(), O_RDONLY | O_CLOEXEC);
    }
    unsigned long long rx = 0;
    unsigned long long tx = 0;
    if (!readCounter(counters.rxFd, rx) || !readCounter(counters.txFd, tx)) {
        closeCounters(counters);  // インターフェースが作り直されたなどで読めないときは、次回開き直す
        return;
    }
    const double elapsed = now - counters.lastTime;
    if (counters.lastTime >= 0 && elapsed > 0 && rx >= counters.lastRx && tx >= counters.lastTx) {
        counters.rxMbps = static_cast<double>(rx - counters.lastRx) * 8.0 / 1e6 / elapsed;
        counters.txMbps = static_cast<double>(tx - counters.lastTx) * 8.0 / 1e6 / elapsed;
    } else {
        counters.rxMbps = kNaN;
        counters.txMbps = kNaN;
    }
    counters.lastRx = rx;
    counters.lastTx = tx;
    counters.lastTime = now;
}

WifiInfo WifiLink::read(double now) {
    return combineAdapters(readAdapters(now));
}

WifiAdapters WifiLink::readAdapters(double now) {
    WifiAdapters out;
    // インターフェースは種類で探す（ときどき探し直す。番号が消えていたらすぐ探し直す）
    const bool gone = (ap_.ifindex != 0 && if_nametoindex(ap_.name.c_str()) != ap_.ifindex) ||
                      (station_.ifindex != 0 && if_nametoindex(station_.name.c_str()) != station_.ifindex);
    if (lastDiscover_ < 0 || now - lastDiscover_ >= kDiscoverIntervalSec || gone) {
        lastDiscover_ = now;
        discoverInterfaces();
    }
    // 流量は両方とも毎回数える（切り替わった直後もすぐ出せるように）
    updateCounters(apCounters_, ap_.name, now);
    updateCounters(stationCounters_, station_.name, now);

    // Both stations every read: a Network widget may show either adapter (combineAdapters picks the dongle first)
    const auto fill = [this](AdapterInfo& a, const Interface& itf, const Counters& counters, bool managed) {
        a.present = itf.ifindex != 0;
        if (!a.present) return;
        a.ifName = itf.name;
        a.ip = itf.ip;
        a.freqMhz = itf.freqMhz;
        a.widthMhz = itf.widthMhz;
        a.rxMbps = counters.rxMbps;
        a.txMbps = counters.txMbps;
        StationResult s;
        s.managed = managed;
        if (dump(NL80211_CMD_GET_STATION, itf.ifindex, onStation, &s) && s.found) {
            a.connected = true;
            a.signalDbm = s.signalDbm;
            a.txLinkMbps = s.txLinkMbps;
            a.rxLinkMbps = s.rxLinkMbps;
        }
    };
    fill(out.dongle, ap_, apCounters_, false);
    fill(out.home, station_, stationCounters_, true);
    return out;
}
