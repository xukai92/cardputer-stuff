/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include <esp_websocket_client.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

/*
 * ====== CONFIGURE ME (at build time — do NOT hardcode secrets here) ======
 * The WebSocket URL of the cardputer-claude-bridge server (see ../server).
 * The shared secret travels as a ?token= query param and must match the
 * server's CARDPUTER_TOKEN env var.
 *
 * Set it via an environment variable when you build (see main/CMakeLists.txt):
 *
 *   CARDPUTER_CLAUDE_WS_URI='wss://host/?token=SECRET' idf.py build
 *
 * Forms:
 *   - Cloudflare Tunnel (TLS):  wss://<host>/?token=<TOKEN>
 *   - LAN / localhost (no TLS): ws://<ip>:<port>/?token=<TOKEN>
 *
 * The fallback below is only a non-functional placeholder so the firmware
 * still builds without the env var; it will not connect anywhere.
 */
#ifndef CARDPUTER_CLAUDE_WS_URI
#define CARDPUTER_CLAUDE_WS_URI "ws://0.0.0.0:8787/?token=unset"
#endif

/**
 * @brief Chat client that talks to a real Claude Code session via the
 *        cardputer-claude-bridge WebSocket server.
 *
 * Features a retained, word-wrapped, scrollable transcript (Fn+;/Fn+. to scroll)
 * and a live connection indicator.
 */
class AppClaude : public mooncake::AppAbility {
public:
    AppClaude();
    ~AppClaude();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Kind { Sent, Recv, Sys };
    struct Msg {
        Kind kind;
        std::string text;
    };

    // --- transcript ---
    std::vector<Msg> _messages;
    int _scroll = 0;  // lines scrolled up from the bottom (0 = latest)
    static constexpr size_t MAX_MESSAGES = 120;

    // --- input ---
    std::string _input;
    int _key_event_slot_id      = -1;
    bool _need_redraw           = true;
    bool _cursor_on             = false;
    uint32_t _cursor_update_ms  = 0;

    // --- websocket ---
    esp_websocket_client_handle_t _client = nullptr;
    std::atomic<bool> _connected{false};
    std::mutex _rx_mutex;
    std::deque<Msg> _rx_queue;
    std::string _frame_accum;

    // transcript helpers
    void add_message(Kind kind, const std::string& text);
    std::vector<std::string> wrap(const std::string& text, int width) const;
    std::vector<std::pair<std::string, uint16_t>> build_lines() const;

    // rendering
    void render();
    void draw_input_bar();

    // input
    void handle_key(const Keyboard::KeyEvent_t& keyEvent);

    // websocket
    void start_ws();
    void stop_ws();
    void send_message(const std::string& text);
    void pump_received();
    void enqueue_rx(Kind kind, const std::string& text);
    void handle_ws_data(const esp_websocket_event_data_t* d);
    static void ws_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
};
