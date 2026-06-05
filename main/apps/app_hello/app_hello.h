/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

/**
 * @brief A minimal "hello world" app: draws a greeting and a live uptime
 *        counter, and exits when the Home button is clicked.
 */
class AppHello : public mooncake::AppAbility {
public:
    AppHello();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    uint32_t _open_time   = 0;
    uint32_t _last_update = 0;
};
