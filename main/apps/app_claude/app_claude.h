/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "app_chat/view/chat_view.h"
#include <mooncake.h>
#include <esp_websocket_client.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

/*
 * ====== CONFIGURE ME ======
 * The WebSocket URL of the cardputer-claude-bridge server (see ../server).
 * Point this at the machine running `claude` + the bridge, then rebuild.
 *   ws://<host-or-ip>:<CARDPUTER_WS_PORT>
 */
#ifndef CARDPUTER_CLAUDE_WS_URI
#define CARDPUTER_CLAUDE_WS_URI "ws://192.168.1.100:8787"
#endif

/**
 * @brief Chat client that talks to a real Claude Code session via the
 *        cardputer-claude-bridge WebSocket server. Reuses ChatView for the UI.
 */
class AppClaude : public mooncake::AppAbility {
public:
    AppClaude();
    ~AppClaude();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<ChatView> _chat_view;
    esp_websocket_client_handle_t _client = nullptr;

    // Receive queue: WS events run on the websocket task, so incoming lines are
    // queued here and drained on the main loop (pump_received).
    std::mutex _rx_mutex;
    std::deque<std::string> _rx_queue;
    std::string _frame_accum;  // reassembles fragmented text frames

    void start_ws();
    void stop_ws();
    void send_message(const std::string& text);
    void pump_received();
    void enqueue_rx(const std::string& line);
    void handle_ws_data(const esp_websocket_event_data_t* d);

    static void ws_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
};
