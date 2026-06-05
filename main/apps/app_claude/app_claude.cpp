/*
 * SPDX-FileCopyrightText: 2026 Kai Xu
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_claude.h"
#include "assets/claude_big.h"
#include "assets/claude_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <hal.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <cstring>

using namespace mooncake;

namespace {
constexpr int INPUT_H      = 18;                     // top input bar height
constexpr int LINE_H       = FONT_REPL_HEIGHT;       // 16
constexpr int CHAR_W       = FONT_REPL_WIDTH;        // 8
constexpr uint32_t BLINK_MS = 500;

const char* prefix_for(int kind)
{
    switch (kind) {
        case 0:  return "> ";  // Sent
        case 1:  return "< ";  // Recv
        default: return "* ";  // Sys
    }
}
uint16_t color_for(int kind)
{
    switch (kind) {
        case 0:  return TFT_GREEN;
        case 1:  return TFT_CYAN;
        default: return TFT_DARKGRAY;
    }
}
}  // namespace

AppClaude::AppClaude()
{
    setAppInfo().name     = "Claude";
    setAppInfo().userData = new AppIcon_t(image_data_claude_big, image_data_claude_small);
}

AppClaude::~AppClaude()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppClaude::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    GetHAL().canvas.setTextScroll(false);
    GetHAL().canvas.setTextWrap(false);

    _key_event_slot_id =
        GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) { handle_key(e); });

    if (!GetHAL().isWifiConnected()) {
        add_message(Kind::Sys, "WiFi not connected.");
        add_message(Kind::Sys, "Open 'Set WiFi', then reopen.");
        _need_redraw = true;
        return;
    }

    add_message(Kind::Sys, "Connecting to bridge...");
    start_ws();
    _need_redraw = true;
}

void AppClaude::onRunning()
{
    pump_received();

    // Cursor blink
    if (GetHAL().millis() - _cursor_update_ms > BLINK_MS) {
        _cursor_update_ms = GetHAL().millis();
        _cursor_on        = !_cursor_on;
        _need_redraw      = true;
    }

    if (_need_redraw) {
        render();
        _need_redraw = false;
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppClaude::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
    stop_ws();
}

// --------------------------------------------------------------------------
// Transcript
// --------------------------------------------------------------------------

std::vector<std::string> AppClaude::wrap(const std::string& text, int width) const
{
    std::vector<std::string> out;
    if (width < 1) width = 1;

    // Split on explicit newlines first, then word-wrap each segment.
    size_t seg_start = 0;
    while (seg_start <= text.size()) {
        size_t nl  = text.find('\n', seg_start);
        std::string seg = text.substr(seg_start, nl == std::string::npos ? std::string::npos : nl - seg_start);
        seg_start  = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        std::string line;
        size_t i = 0;
        while (i < seg.size()) {
            // grab next word
            size_t ws = seg.find(' ', i);
            std::string word = seg.substr(i, ws == std::string::npos ? std::string::npos : ws - i);
            i = (ws == std::string::npos) ? seg.size() : ws + 1;

            // hard-split words longer than the line width
            while ((int)word.size() > width) {
                if (!line.empty()) {
                    out.push_back(line);
                    line.clear();
                }
                out.push_back(word.substr(0, width));
                word = word.substr(width);
            }
            if (line.empty()) {
                line = word;
            } else if ((int)(line.size() + 1 + word.size()) <= width) {
                line += " " + word;
            } else {
                out.push_back(line);
                line = word;
            }
        }
        out.push_back(line);  // keep blank lines too
    }
    if (out.empty()) out.push_back("");
    return out;
}

std::vector<std::pair<std::string, uint16_t>> AppClaude::build_lines() const
{
    const int cols  = GetHAL().canvas.width() / CHAR_W;  // 30
    const int width = cols - 2;                           // minus 2-char prefix/indent
    std::vector<std::pair<std::string, uint16_t>> lines;
    for (const auto& m : _messages) {
        const uint16_t color = color_for((int)m.kind);
        auto segs            = wrap(m.text, width);
        for (size_t k = 0; k < segs.size(); ++k) {
            const char* lead = (k == 0) ? prefix_for((int)m.kind) : "  ";
            lines.emplace_back(std::string(lead) + segs[k], color);
        }
    }
    return lines;
}

void AppClaude::add_message(Kind kind, const std::string& text)
{
    // If the user has scrolled up, keep their view anchored by pushing the
    // scroll offset down by however many lines this message adds.
    if (_scroll > 0) {
        const int width = GetHAL().canvas.width() / CHAR_W - 2;
        _scroll += (int)wrap(text, width).size();
    }
    _messages.push_back({kind, text});
    if (_messages.size() > MAX_MESSAGES) _messages.erase(_messages.begin());
    _need_redraw = true;
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

void AppClaude::render()
{
    auto& cv = GetHAL().canvas;
    cv.fillScreen(THEME_COLOR_BG);
    cv.setFont(FONT_REPL);
    cv.setTextSize(1);
    cv.setTextDatum(textdatum_t::top_left);

    const int rows  = (cv.height() - INPUT_H) / LINE_H;  // 7
    auto lines      = build_lines();
    const int total = (int)lines.size();
    const int maxScroll = total > rows ? total - rows : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    if (_scroll < 0) _scroll = 0;
    const int first = maxScroll - _scroll;

    for (int r = 0; r < rows; ++r) {
        int idx = first + r;
        if (idx < 0 || idx >= total) continue;
        cv.setTextColor(lines[idx].second, THEME_COLOR_BG);
        cv.drawString(lines[idx].first.c_str(), 0, INPUT_H + r * LINE_H);
    }

    // "more below" hint when scrolled up
    if (_scroll > 0) {
        cv.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        cv.drawString("v", cv.width() - CHAR_W, cv.height() - LINE_H);
    }

    draw_input_bar();
    GetHAL().pushCanvas();
}

void AppClaude::draw_input_bar()
{
    auto& cv                          = GetHAL().canvas;
    const uint32_t bar_color          = 0x606060;
    cv.fillRect(0, 0, cv.width(), INPUT_H, bar_color);

    // Connection indicator (right side)
    cv.fillSmoothCircle(cv.width() - 8, INPUT_H / 2, 3, _connected.load() ? TFT_GREEN : TFT_RED);

    // Prompt + input, scrolled to keep the tail (and cursor) visible.
    const int avail_cols = (cv.width() - 16) / CHAR_W;  // leave room for the dot
    std::string body     = ">" + _input + (_cursor_on ? "_" : " ");
    if ((int)body.size() > avail_cols) body = body.substr(body.size() - avail_cols);

    cv.setTextDatum(textdatum_t::top_left);
    cv.setTextColor(TFT_WHITE, bar_color);
    cv.drawString(body.c_str(), 2, 1);
}

// --------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------

void AppClaude::handle_key(const Keyboard::KeyEvent_t& e)
{
    if (!e.state || e.isModifier) return;

    switch (e.keyCode) {
        case KEY_ENTER:
            if (!_input.empty()) {
                send_message(_input);
                add_message(Kind::Sent, _input);
                _input.clear();
                _scroll      = 0;  // jump to latest
                _need_redraw = true;
            }
            break;
        case KEY_BACKSPACE:
            if (!_input.empty()) {
                _input.pop_back();
                _need_redraw = true;
            }
            break;
        case KEY_SPACE:
            _input += ' ';
            _need_redraw = true;
            break;
        case KEY_UP:  // Fn + ;
            _scroll += 1;  // clamped in render()
            _need_redraw = true;
            break;
        case KEY_DOWN:  // Fn + .
            if (_scroll > 0) _scroll -= 1;
            _need_redraw = true;
            break;
        default:
            if (e.keyName && strlen(e.keyName) == 1) {
                _input += e.keyName;
                _need_redraw = true;
            }
            break;
    }
}

// --------------------------------------------------------------------------
// WebSocket
// --------------------------------------------------------------------------

void AppClaude::start_ws()
{
    esp_websocket_client_config_t cfg = {};
    cfg.uri                           = CARDPUTER_CLAUDE_WS_URI;
    cfg.crt_bundle_attach             = esp_crt_bundle_attach;  // verify TLS for wss://
    cfg.ping_interval_sec             = 20;                     // keepalive through proxies

    _client = esp_websocket_client_init(&cfg);
    if (!_client) {
        enqueue_rx(Kind::Sys, "could not init websocket");
        return;
    }
    esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, ws_event_handler, this);
    if (esp_websocket_client_start(_client) != ESP_OK) {
        enqueue_rx(Kind::Sys, "websocket start failed");
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
        enqueue_rx(Kind::Sys, "not connected");
        return;
    }
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "msg");
    cJSON_AddStringToObject(obj, "text", text.c_str());
    char* payload = cJSON_PrintUnformatted(obj);
    if (payload) {
        if (esp_websocket_client_send_text(_client, payload, strlen(payload), pdMS_TO_TICKS(2000)) < 0) {
            enqueue_rx(Kind::Sys, "send failed");
        }
        cJSON_free(payload);
    }
    cJSON_Delete(obj);
}

void AppClaude::ws_event_handler(void* arg, esp_event_base_t, int32_t event_id, void* event_data)
{
    auto* self = static_cast<AppClaude*>(arg);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            self->_connected.store(true);
            self->enqueue_rx(Kind::Sys, "connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            self->_connected.store(false);
            self->enqueue_rx(Kind::Sys, "disconnected");
            break;
        case WEBSOCKET_EVENT_DATA:
            self->handle_ws_data(data);
            break;
        case WEBSOCKET_EVENT_ERROR:
            self->enqueue_rx(Kind::Sys, "ws error");
            break;
        default:
            break;
    }
}

void AppClaude::handle_ws_data(const esp_websocket_event_data_t* d)
{
    if (d->op_code != 0x01 && d->op_code != 0x00) return;  // text or continuation only

    if (d->payload_offset == 0) _frame_accum.clear();
    if (d->data_ptr && d->data_len > 0) _frame_accum.append(d->data_ptr, d->data_len);
    if (d->payload_offset + d->data_len < d->payload_len) return;  // more fragments

    std::string frame = std::move(_frame_accum);
    _frame_accum.clear();
    if (frame.empty()) return;

    cJSON* root = cJSON_Parse(frame.c_str());
    if (!root) {
        enqueue_rx(Kind::Recv, frame);
        return;
    }
    const cJSON* type  = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON* text  = cJSON_GetObjectItemCaseSensitive(root, "text");
    const char* type_s = cJSON_IsString(type) ? type->valuestring : nullptr;
    const char* text_s = cJSON_IsString(text) ? text->valuestring : nullptr;
    if (text_s) {
        if (type_s && strcmp(type_s, "status") == 0) {
            enqueue_rx(Kind::Sys, text_s);
        } else {
            enqueue_rx(Kind::Recv, text_s);
        }
    }
    cJSON_Delete(root);
}

void AppClaude::enqueue_rx(Kind kind, const std::string& text)
{
    std::lock_guard<std::mutex> lock(_rx_mutex);
    _rx_queue.push_back({kind, text});
}

void AppClaude::pump_received()
{
    for (;;) {
        Msg m;
        {
            std::lock_guard<std::mutex> lock(_rx_mutex);
            if (_rx_queue.empty()) break;
            m = std::move(_rx_queue.front());
            _rx_queue.pop_front();
        }
        add_message(m.kind, m.text);
    }
}
