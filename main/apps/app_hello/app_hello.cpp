/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_hello.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <hal.h>

using namespace mooncake;

AppHello::AppHello()
{
    setAppInfo().name = "Hello";
    // No icon set -> the launcher renders the name only (userData stays null).
}

void AppHello::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _open_time   = GetHAL().millis();
    _last_update = 0;
}

void AppHello::onRunning()
{
    // Redraw a few times per second to keep the uptime counter live.
    if (GetHAL().millis() - _last_update > 200) {
        _last_update = GetHAL().millis();

        GetHAL().canvas.fillScreen(THEME_COLOR_BG);
        GetHAL().canvas.setFont(FONT_BASIC);

        // Greeting
        GetHAL().canvas.setTextSize(2);
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("Hello, Kai!", GetHAL().canvas.width() / 2,
                                         GetHAL().canvas.height() / 2 - 34);

        // Live uptime counter (demonstrates onRunning being called repeatedly)
        uint32_t secs = (GetHAL().millis() - _open_time) / 1000;
        auto uptime   = fmt::format("up {} s", secs);
        GetHAL().canvas.setTextSize(1);
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString(uptime.c_str(), GetHAL().canvas.width() / 2,
                                         GetHAL().canvas.height() / 2 + 2);

        // Exit hint
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("Press Home to exit", GetHAL().canvas.width() / 2,
                                         GetHAL().canvas.height() / 2 + 28);

        GetHAL().pushCanvas();
    }

    // Close the app when the Home button is clicked.
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppHello::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
