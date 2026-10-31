// 無線の状態: Steam Link の直通回線（Frame が AP になるインターフェース）と、家の Wi-Fi（Frame が子機になる
// インターフェース）。nl80211 を libnl で直接読む（iw は起動しない）。インターフェースは名前ではなく
// nl80211 の種類（AP / 子機）で見分ける。MAC アドレスと SSID は取り出さない。
#pragma once

#include <string>

struct nl_sock;

/**
 * 無線の読み取り結果。値が取れないときは NaN。
 * 直通回線に相手がいればそちら、いなければ家の Wi-Fi の接続先（AP）の値を入れる。
 */
struct WifiInfo {
    bool interfaceUp = false;   ///< 直通回線か家の Wi-Fi のインターフェースがある
    bool connected = false;     ///< どちらかでつながっている
    bool homeWifi = false;      ///< true なら家の Wi-Fi（子機として AP につながっている）、false なら直通回線
    double signalDbm;           ///< 電波の強さ（dBm。直通は avg ack signal、家の Wi-Fi は signal）
    double txLinkMbps;          ///< Frame → 相手（PC / AP）のリンク速度
    double rxLinkMbps;          ///< 相手 → Frame のリンク速度
    double rxMbps;              ///< 実際に受け取っている量（相手 → Frame、映像はこちら）
    double txMbps;              ///< 実際に送っている量（Frame → 相手）

    WifiInfo();
};

/** One interface (a Network widget set to Dongle or Home). NaN / 0 / empty where there's no value. */
struct AdapterInfo {
    bool present = false;    ///< the interface exists
    bool connected = false;  ///< it has a peer (a station on the AP, or the access point we're on)
    std::string ifName;
    std::string ip;          ///< first IPv4 address, empty if none
    double rxMbps;           ///< from /sys/class/net/<if>/statistics, NaN on the first read
    double txMbps;
    double txLinkMbps;
    double rxLinkMbps;
    double signalDbm;
    int freqMhz = 0;   ///< control channel; 0 = unknown
    int widthMhz = 0;  ///< 0 = unknown

    AdapterInfo();
};

/** Both interfaces from one read. */
struct WifiAdapters {
    AdapterInfo dongle;  ///< nl80211 type AP
    AdapterInfo home;    ///< nl80211 type station
};

/** @return the channel number for a frequency (2.4 / 5 / 6 / 60 GHz), 0 if unknown */
int channelFromFreq(int freqMhz);

/**
 * @return the band for a frequency, as a frame_perf_overlay::WifiBand value (core/metric_registry.h): 0 unknown,
 *         1 2.4 GHz, 2 5 GHz, 3 6 GHz, 4 60 GHz. A plain number so this file needs nothing from core/.
 */
unsigned bandFromFreq(int freqMhz);

/**
 * @param nl80211Width an enum nl80211_chan_width value
 * @return the width in MHz, 0 if unknown
 */
int widthMhzFromNl80211(unsigned nl80211Width);

/** @return the combined link: the dongle if it has a peer, else home Wi-Fi (what WifiLink::read returns) */
WifiInfo combineAdapters(const WifiAdapters& adapters);

/**
 * nl80211 のソケットを 1 本持ち続けて、インターフェースとステーションの情報を問い合わせる。書き込み（設定変更）はしない。
 */
class WifiLink {
public:
    WifiLink() = default;
    ~WifiLink();
    WifiLink(const WifiLink&) = delete;
    WifiLink& operator=(const WifiLink&) = delete;

    /**
     * 直通回線、なければ家の Wi-Fi のステーションの情報と、流れている量（前回からの差分）を読む。
     * @param now 現在時刻（秒、単調増加）
     * @return 読み取り結果
     */
    WifiInfo read(double now);

    /**
     * Reads both interfaces: each one's station (signal, link rates), byte counters, and from the last interface
     * scan its channel, width and IPv4 address. read() is combineAdapters() of this.
     * @param now monotonic seconds
     */
    WifiAdapters readAdapters(double now);

    /**
     * 流れている量の計算をやり直す（読み取りを止めていたあと、止めていた間の平均を出さないため）。次の read の量は NaN。
     */
    void resetCounters() {
        apCounters_.lastTime = -1.0;
        stationCounters_.lastTime = -1.0;
    }

private:
    /** 1 つのインターフェースの statistics の読み取りと、前回の値。 */
    struct Counters {
        std::string name;  ///< インターフェース名（変わったら開き直す）
        int rxFd = -1;
        int txFd = -1;
        double lastTime = -1.0;
        unsigned long long lastRx = 0;
        unsigned long long lastTx = 0;
        double rxMbps;
        double txMbps;

        Counters();
    };

    /** nl80211 の種類で見つけたインターフェース。 */
    struct Interface {
        unsigned ifindex = 0;  ///< 0 なら無い
        std::string name;
        int freqMhz = 0;   ///< only while the interface is on a channel
        int widthMhz = 0;
        std::string ip;    ///< first IPv4 address at the last scan
    };

    nl_sock* socket_ = nullptr;
    int family_ = -1;
    double lastDiscover_ = -1.0;  ///< 最後にインターフェースを探した時刻
    Interface ap_;                ///< 直通回線（AP）
    Interface station_;           ///< 家の Wi-Fi（子機）
    Counters apCounters_;
    Counters stationCounters_;

    /**
     * nl80211 のソケットを用意する（失敗したら次の read で作り直す）。
     * @return 使えるなら true
     */
    bool ensureSocket();

    /**
     * nl80211 のインターフェースの一覧（GET_INTERFACE のダンプ）から、AP と子機を 1 つずつ探す。
     */
    void discoverInterfaces();

    /**
     * nl80211 のメッセージを送って、返事を callback で受け取る。失敗したらソケットを作り直す印を付ける。
     * @param command NL80211_CMD_*
     * @param ifindex 0 でなければ NL80211_ATTR_IFINDEX に入れる
     * @param callback 1 件ごとに呼ばれる
     * @param arg callback に渡す
     * @return 送って受け取れたら true
     */
    bool dump(int command, unsigned ifindex, int (*callback)(struct nl_msg*, void*), void* arg);

    /**
     * statistics のバイト数を読んで、前回からの流量を計算する。
     * @param counters 書き換える読み取りの状態
     * @param name インターフェース名（空なら何もしない）
     * @param now 現在時刻（秒）
     */
    static void updateCounters(Counters& counters, const std::string& name, double now);

    /**
     * statistics の数値ファイルを先頭から読み直す。
     * @param fd 開いてあるファイル
     * @param value 書き込み先
     * @return 読めたら true
     */
    static bool readCounter(int fd, unsigned long long& value);

    /**
     * statistics のファイルを閉じて、流量の計算をやり直しにする。
     * @param counters 書き換える読み取りの状態
     */
    static void closeCounters(Counters& counters);
};
