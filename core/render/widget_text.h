// UI strings for the widgets, in Japanese and English. Drawing code never contains UI text; it looks it up here.
// The words the fixed panel had (headset/i18n.cpp before the widget grid) keep their Japanese.
#pragma once

#include "reason.h"

#include <string>

namespace frame_perf_overlay {

/** The widgets' language (the headset maps its Language setting onto this). */
enum class WidgetLanguage { Ja, En };

struct WidgetText {
    WidgetLanguage language;
    const char* naPrefix;  ///< before a reason: "N/A: "
    // Titles
    const char* frame;
    const char* cpu;
    const char* gpuRam;
    const char* power;
    const char* battery;
    const char* temp;
    const char* network;
    const char* general;
    // Frame
    const char* waiting;    ///< connected to SteamVR, no frames yet
    const char* noSteamVr;  ///< not connected to SteamVR
    const char* reproj;     ///< followed by %
    const char* dropped;    ///< followed by a count
    // CPU
    const char* maxCoreOpen;   ///< before the busiest core's % (" (max core ")
    const char* maxCoreClose;  ///< after it (")")
    // GPU & RAM
    const char* gpuLoad;  ///< after the big %: frame GPU ms × app fps, not the whole GPU (core/gpu_load.h)
    const char* clock;
    const char* ram;
    // Power
    const char* rails;  ///< sum of measured rails, followed by W
    // Battery
    const char* batteryNote;  ///< the gauge % vs Steam's number
    const char* flowIn;       ///< W into the battery
    const char* flowOut;      ///< W out of it
    const char* statusCharging;
    const char* statusDischarging;
    const char* statusFull;
    const char* statusPlugged;
    const char* statusIdle;
    // Temp
    const char* cpuTempUnit;  ///< after the big CPU temperature ("°C CPU")
    const char* celsius;
    const char* fan;
    const char* thermalLimit;  ///< badge, followed by CPU / GPU / CPU+GPU
    const char* cpuAndGpu;     ///< both throttled
    const char* tDdr;
    const char* tBattery;
    const char* tDisplay;
    const char* tExhaust;
    const char* tHeatsink;
    // Network
    const char* linkDongle;  ///< the Steam Link dongle has a peer
    const char* linkHome;    ///< home Wi-Fi (also when neither is connected)
    const char* notConnected;
    const char* link;  ///< PHY rate, followed by tx/rx
    // General
    const char* controllers;
    const char* left;
    const char* right;
    const char* time;
    // The full (non-compact) rows and the process lists
    const char* cpuItems;   ///< title of widget 7
    const char* ramItems;   ///< title of widget 8
    const char* msMaxSep;   ///< Frame times: between the average and the worst ("ms · max")
    const char* capped;     ///< CPU cluster under a Playtime cap, followed by the hardware max GHz
    const char* unmeasured; ///< Power: vph minus the measured rails
    const char* charge;     ///< Battery rows
    const char* design;     ///< after the charge: "(design N)"
    const char* toFull;
    const char* toEmpty;
    const char* health;
    const char* cycles;
    const char* charger;
    const char* setCurrent;
    const char* usb;
    const char* unplugged;
    const char* pdOffered;
    const char* arriving;
    const char* sag;
    const char* sagHigh;    ///< after the sag volts when it's 10% or more of the offered volts
    const char* tNpu;       ///< Temp spots
    const char* tCamera;
    const char* tVideo;
    const char* tWifi;
    const char* tFlash;
    const char* tNrf;
    const char* tConnector;
    const char* pctOfCore;  ///< CPU items' column heading
    const char* rss;        ///< RAM items' column heading
    const char* ip;         ///< Network, followed by the address
    const char* uptime;     ///< General rows
    const char* app;
    const char* noApp;      ///< no scene app (SteamVR Home)
    const char* hudCost;
    const char* hideRulesError;  ///< CPU / RAM items with Hide system: hide_rules.json didn't load
};

/**
 * @param language the language
 * @return its strings (static)
 */
const WidgetText& widgetText(WidgetLanguage language);

/**
 * Why a reading is missing, in the strings' language (English is reasonText).
 * @param t the strings
 * @param reason the code
 * @return static text
 */
const char* reasonLabel(const WidgetText& t, Reason reason);

/**
 * The throttle badge: SteamVR is holding the app to 1/(throttled + 1) of the refresh rate.
 * @param t the strings
 * @param hz refresh rate
 * @param throttled throttle steps (1 or more)
 */
std::string throttleBadgeText(const WidgetText& t, double hz, int throttled);

}  // namespace frame_perf_overlay
