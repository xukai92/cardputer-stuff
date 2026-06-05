/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.h>
#include <M5Unified.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps.h>
#include <hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <utility>

using namespace mooncake;
using namespace smooth_ui_toolkit;

// Auto-connect to the saved WiFi network at boot. Runs in a detached task so
// the UI comes up immediately even if the AP is slow or out of range (the
// wifiConnect call blocks for up to ~13s on failure).
static void start_wifi_autoconnect()
{
    std::string ssid = GetHAL().getSettings().GetString("wifi_ssid", "");
    std::string pass = GetHAL().getSettings().GetString("wifi_password", "");
    if (ssid.empty()) {
        return;
    }
    auto* creds = new std::pair<std::string, std::string>(std::move(ssid), std::move(pass));
    xTaskCreate(
        [](void* arg) {
            auto* c = static_cast<std::pair<std::string, std::string>*>(arg);
            GetHAL().wifiConnect(c->first, c->second);
            delete c;
            vTaskDelete(nullptr);
        },
        "wifi_autoconnect", 8192, creds, 5, nullptr);
}

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_debug);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Auto-connect to saved WiFi (non-blocking)
    start_wifi_autoconnect();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps
    GetMooncake().installApp(std::make_unique<Launcher>());
    GetMooncake().installApp(std::make_unique<AppWifiScan>());
    GetMooncake().installApp(std::make_unique<AppRecord>());
    GetMooncake().installApp(std::make_unique<AppChat>());
    GetMooncake().installApp(std::make_unique<AppRemote>());
    GetMooncake().installApp(std::make_unique<AppREPL>());
    GetMooncake().installApp(std::make_unique<AppSetWiFi>());
    GetMooncake().installApp(std::make_unique<AppClock>());
    GetMooncake().installApp(std::make_unique<AppKeyboard>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppSdcard>());
    GetMooncake().installApp(std::make_unique<AppStringIRToolKit>());
    GetMooncake().installApp(std::make_unique<AppLoraChat>());
    GetMooncake().installApp(std::make_unique<AppGPS>());
    GetMooncake().installApp(std::make_unique<AppClaude>());
    // GetMooncake().installApp(std::make_unique<AppDummy>());

    // Main loop
    audio::set_keyboard_sfx_enable(true);
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().update();
        GetMooncake().update();
    }
}
