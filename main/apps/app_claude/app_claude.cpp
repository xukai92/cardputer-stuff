/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_claude.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <hal.h>
#include <cJSON.h>
#include <cstring>

using namespace mooncake;

AppClaude::AppClaude()
{
    setAppInfo().name = "Claude";
    // No icon -> launcher shows the name only.
}

AppClaude::~AppClaude() = default;

void AppClaude::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _chat_view            = std::make_unique<ChatView>();
    _chat_view->onSendMsg = [this](const std::string& message) { send_message(message); };
    _chat_view->init();

    if (!GetHAL().isWifiConnected()) {
        _chat_view->addMessage("WiFi not connected.", false);
        _chat_view->addMessage("Open 'Set WiFi' first, then reopen Claude.", false);
        return;
    }

    _chat_view->addMessage(std::string("Connecting to ") + CARDPUTER_CLAUDE_WS_URI, false);
    start_ws();
}

void AppClaude::onRunning()
{
    if (_chat_view) {
        _chat_view->update();
    }

    pump_received();

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppClaude::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    stop_ws();
    if (_chat_view) {
        _chat_view.reset();
    }
}

// --------------------------------------------------------------------------
// WebSocket
// --------------------------------------------------------------------------

void AppClaude::start_ws()
{
    esp_websocket_client_config_t cfg = {};
    cfg.uri                           = CARDPUTER_CLAUDE_WS_URI;

    _client = esp_websocket_client_init(&cfg);
    if (!_client) {
        enqueue_rx("[error] could not init websocket");
        return;
    }
    esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, ws_event_handler, this);
    esp_err_t err = esp_websocket_client_start(_client);
    if (err != ESP_OK) {
        enqueue_rx("[error] websocket start failed");
    }
}

void AppClaude::stop_ws()
{
    if (_client) {
        esp_websocket_client_close(_client, pdMS_TO_TICKS(1000));
        esp_websocket_client_destroy(_client);
        _client = nullptr;
    }
}

void AppClaude::send_message(const std::string& text)
{
    if (!_client || !esp_websocket_client_is_connected(_client)) {
        enqueue_rx("[not connected]");
        return;
    }

    // Build {"type":"msg","text":"..."} with proper JSON escaping.
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "msg");
    cJSON_AddStringToObject(obj, "text", text.c_str());
    char* payload = cJSON_PrintUnformatted(obj);

    if (payload) {
        int sent = esp_websocket_client_send_text(_client, payload, strlen(payload), pdMS_TO_TICKS(2000));
        if (sent < 0) {
            enqueue_rx("[send failed]");
        }
        cJSON_free(payload);
    }
    cJSON_Delete(obj);
}

// Runs on the websocket task — keep it light; just queue work for the main loop.
void AppClaude::ws_event_handler(void* arg, esp_event_base_t /*base*/, int32_t event_id, void* event_data)
{
    auto* self = static_cast<AppClaude*>(arg);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            self->enqueue_rx("[connected]");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            self->enqueue_rx("[disconnected]");
            break;
        case WEBSOCKET_EVENT_DATA:
            self->handle_ws_data(data);
            break;
        case WEBSOCKET_EVENT_ERROR:
            self->enqueue_rx("[ws error]");
            break;
        default:
            break;
    }
}

void AppClaude::handle_ws_data(const esp_websocket_event_data_t* d)
{
    // op_code 0x1 = text, 0x0 = continuation. Ignore ping/pong/close/binary.
    if (d->op_code != 0x01 && d->op_code != 0x00) {
        return;
    }

    if (d->payload_offset == 0) {
        _frame_accum.clear();
    }
    if (d->data_ptr && d->data_len > 0) {
        _frame_accum.append(d->data_ptr, d->data_len);
    }

    // Whole message assembled?
    if (d->payload_offset + d->data_len < d->payload_len) {
        return;  // more fragments coming
    }

    std::string frame = _frame_accum;
    _frame_accum.clear();
    if (frame.empty()) {
        return;
    }

    // Parse {"type":..,"text":..}; fall back to raw text if not JSON.
    cJSON* root = cJSON_Parse(frame.c_str());
    if (!root) {
        enqueue_rx(frame);
        return;
    }
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
    const char* text_s = cJSON_IsString(text) ? text->valuestring : nullptr;
    const char* type_s = cJSON_IsString(type) ? type->valuestring : nullptr;

    if (text_s) {
        if (type_s && strcmp(type_s, "status") == 0) {
            enqueue_rx(std::string("[") + text_s + "]");
        } else {
            enqueue_rx(text_s);
        }
    }
    cJSON_Delete(root);
}

// --------------------------------------------------------------------------
// Receive queue (thread-safe handoff to the main loop)
// --------------------------------------------------------------------------

void AppClaude::enqueue_rx(const std::string& line)
{
    std::lock_guard<std::mutex> lock(_rx_mutex);
    _rx_queue.push_back(line);
}

void AppClaude::pump_received()
{
    for (;;) {
        std::string line;
        {
            std::lock_guard<std::mutex> lock(_rx_mutex);
            if (_rx_queue.empty()) {
                break;
            }
            line = std::move(_rx_queue.front());
            _rx_queue.pop_front();
        }
        if (_chat_view) {
            _chat_view->addMessage(line, false);
        }
    }
}
