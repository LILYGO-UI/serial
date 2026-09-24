#include "pages/serial/serial_view_model.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <limits>
#include <utility>

namespace serial {
namespace {

bool same_settings(const Settings& a, const Settings& b)
{
    return a.baud_rate == b.baud_rate && a.data_bits == b.data_bits && a.parity == b.parity &&
           a.stop_bits == b.stop_bits && a.flow_control == b.flow_control;
}

bool same_ports(const std::vector<PortInfo>& a, const std::vector<PortInfo>& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const PortInfo& left, const PortInfo& right) {
               return left.path == right.path && left.name == right.name;
           });
}

std::string timestamp_text(std::chrono::system_clock::time_point timestamp)
{
    const auto seconds = std::chrono::system_clock::to_time_t(timestamp);
    std::tm local{};
    localtime_r(&seconds, &local);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() % 1000;
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "[%02d:%02d:%02d.%03d] ", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(milliseconds));
    return buffer;
}

std::string line_name(Line line)
{
    switch (line) {
        case Line::Rts:
            return "RTS";
        case Line::Dtr:
            return "DTR";
        case Line::Break:
            return "BREAK";
    }
    return "Line";
}

}  // namespace

ViewModel::ViewModel()
{
    scan_ports();
}

void ViewModel::publish()
{
    observed_model_revision_ = model_.revision();
    const auto value         = revision_.value();
    revision_.set(value == std::numeric_limits<std::int32_t>::max() ? 0 : value + 1);
}

bool ViewModel::set_status(std::string status, bool error)
{
    if (status_ == status && error_ == error) return false;
    status_ = std::move(status);
    error_  = error;
    return true;
}

void ViewModel::report_error(const std::string& message)
{
    const bool changed = set_status(message, true);
    if (changed || model_.revision() != observed_model_revision_) publish();
}

void ViewModel::scan_ports()
{
    const std::string previous = selected_port_ < ports_.size() ? ports_[selected_port_].path : "";
    auto next                  = model_.scan_ports();
    // A connected endpoint remains selectable if discovery briefly misses it.
    if (model_.connected() && !is_hidden_port_path(model_.device()) &&
        std::none_of(next.begin(), next.end(), [&](const PortInfo& port) { return port.path == model_.device(); })) {
        next.push_back({model_.device(), model_.device()});
    }
    std::size_t selected = 0;
    const auto found =
        std::find_if(next.begin(), next.end(), [&](const PortInfo& port) { return port.path == previous; });
    if (found != next.end()) selected = static_cast<std::size_t>(found - next.begin());
    const bool ports_changed = !same_ports(ports_, next) || selected_port_ != selected;
    ports_                   = std::move(next);
    selected_port_           = selected;
    bool status_changed      = false;
    if (!model_.last_error().empty())
        status_changed = set_status(model_.last_error(), true);
    else if (!connected()) {
        status_changed =
            set_status(ports_.empty() ? "No serial ports found. Connect a device and scan again." : "Ready to connect");
    }
    if (ports_changed || status_changed || model_.revision() != observed_model_revision_) publish();
}

void ViewModel::select_port(std::size_t index)
{
    if (connected()) {
        report_error("Disconnect before selecting another port.");
        return;
    }
    if (index >= ports_.size()) {
        report_error("Select an available serial port.");
        return;
    }
    const bool changed        = selected_port_ != index;
    selected_port_            = index;
    const bool status_changed = set_status("Ready to connect");
    if (changed || status_changed) publish();
}

void ViewModel::select_path(const std::string& path)
{
    if (connected()) {
        report_error("Disconnect before selecting another port.");
        return;
    }
    if (path.empty()) {
        report_error("Select an available serial port.");
        return;
    }
    const auto found =
        std::find_if(ports_.begin(), ports_.end(), [&](const PortInfo& port) { return port.path == path; });
    if (found != ports_.end()) {
        select_port(static_cast<std::size_t>(found - ports_.begin()));
        return;
    }
    ports_.push_back({path, path});
    selected_port_ = ports_.size() - 1;
    set_status("Ready to connect");
    publish();
}

void ViewModel::set_settings(Settings settings)
{
    if (same_settings(settings_, settings)) return;
    if (connected()) {
        report_error("Disconnect before changing serial settings.");
        return;
    }
    const auto& rates = supported_baud_rates();
    if (std::find(rates.begin(), rates.end(), settings.baud_rate) == rates.end() || settings.data_bits < 5 ||
        settings.data_bits > 8 || (settings.stop_bits != 1 && settings.stop_bits != 2) ||
        (settings.parity != Parity::None && settings.parity != Parity::Odd && settings.parity != Parity::Even) ||
        (settings.flow_control != FlowControl::None && settings.flow_control != FlowControl::RtsCts &&
         settings.flow_control != FlowControl::XonXoff)) {
        report_error("Unsupported serial settings.");
        return;
    }
    settings_ = settings;
    set_status("Ready to connect");
    publish();
}

void ViewModel::toggle_connection()
{
    if (connected()) {
        disconnect();
        return;
    }
    if (selected_port_ >= ports_.size()) {
        report_error("Select an available serial port first.");
        return;
    }
    if (!model_.connect(ports_[selected_port_].path, settings_)) {
        report_error(model_.last_error());
        return;
    }
    set_status("Connected to " + model_.device());
    publish();
}

void ViewModel::disconnect()
{
    const bool was_connected = connected();
    model_.disconnect();
    const bool changed = set_status("Disconnected");
    if (was_connected || changed || model_.revision() != observed_model_revision_) publish();
}

void ViewModel::poll()
{
    const bool was_connected = connected();
    const bool was_sending   = model_.queued_bytes() != 0;
    model_.poll();
    if (model_.revision() == observed_model_revision_ && was_connected == connected()) return;
    if (!model_.last_error().empty())
        set_status(model_.last_error(), true);
    else if (was_connected && !connected())
        set_status("Disconnected");
    else if (was_sending && model_.queued_bytes() == 0)
        set_status("Data sent");
    publish();
}

void ViewModel::set_rx_hex(bool enabled)
{
    if (rx_hex_ == enabled) return;
    rx_hex_ = enabled;
    publish();
}

void ViewModel::set_tx_hex(bool enabled)
{
    if (tx_hex_ == enabled) return;
    tx_hex_ = enabled;
    publish();
}

void ViewModel::set_timestamps(bool enabled)
{
    if (timestamps_ == enabled) return;
    timestamps_ = enabled;
    publish();
}

void ViewModel::set_autoscroll(bool enabled)
{
    if (autoscroll_ == enabled) return;
    autoscroll_ = enabled;
    publish();
}

void ViewModel::set_line(Line line, bool enabled)
{
    if (!connected()) {
        report_error("Connect a serial port before changing line signals.");
        return;
    }
    if (!model_.set_line(line, enabled)) {
        report_error(model_.last_error());
        return;
    }
    const bool changed = set_status(line_name(line) + (enabled ? " enabled" : " disabled"));
    if (changed || model_.revision() != observed_model_revision_) publish();
}

void ViewModel::set_line_ending(EndOfLine ending)
{
    if (line_ending_ == ending) return;
    if (ending != EndOfLine::None && ending != EndOfLine::LF && ending != EndOfLine::CR && ending != EndOfLine::CRLF) {
        report_error("Unsupported line ending.");
        return;
    }
    line_ending_ = ending;
    publish();
}

void ViewModel::send(const std::string& text)
{
    if (!connected()) {
        report_error("Connect a serial port before sending data.");
        return;
    }
    std::vector<std::uint8_t> bytes;
    std::string validation_error;
    if (!parse_send(text, tx_hex_ ? Format::Hex : Format::Ascii, line_ending_, bytes, validation_error)) {
        report_error(validation_error);
        return;
    }
    if (bytes.empty()) {
        report_error("Enter data to send.");
        return;
    }
    if (!model_.send(bytes)) {
        report_error(model_.last_error());
        return;
    }
    set_status(model_.queued_bytes() == 0 ? "Data sent" : "Sending data...");
    publish();
}

void ViewModel::clear()
{
    model_.clear();
    if (model_.revision() != observed_model_revision_) publish();
}

std::string ViewModel::connection_summary() const
{
    const char parity   = settings_.parity == Parity::None ? 'N' : settings_.parity == Parity::Odd ? 'O' : 'E';
    std::string summary = std::to_string(settings_.baud_rate) + " baud / " + std::to_string(settings_.data_bits) +
                          parity + std::to_string(settings_.stop_bits);
    if (settings_.flow_control == FlowControl::RtsCts)
        summary += " / RTS-CTS";
    else if (settings_.flow_control == FlowControl::XonXoff)
        summary += " / XON-XOFF";
    return summary;
}

std::string ViewModel::log_text() const
{
    // A bounded suffix also protects LVGL from very long escaped ASCII records.
    constexpr std::size_t max_record_bytes = 4096;
    const std::string omitted              = "[Earlier data omitted from display]\n";
    std::vector<std::string> lines;
    std::size_t size = 0;
    bool truncated   = false;
    for (auto it = model_.records().rbegin(); it != model_.records().rend(); ++it) {
        // The Receive panel only shows bytes read from the serial device.
        // Transmitted bytes remain available through the TX counter, but are
        // intentionally omitted from this display.
        if (it->direction != Direction::Rx) continue;
        std::string line = timestamps_ ? timestamp_text(it->timestamp) : "";
        line += "RX  ";
        if (it->bytes.size() > max_record_bytes) {
            line += "... ";
            const std::vector<std::uint8_t> tail(it->bytes.end() - max_record_bytes, it->bytes.end());
            line += format_bytes(tail, rx_hex_ ? Format::Hex : Format::Ascii);
            truncated = true;
        } else {
            line += format_bytes(it->bytes, rx_hex_ ? Format::Hex : Format::Ascii);
        }
        // A received LF already separates records; do not invent a blank line.
        if (line.back() != '\n') line += '\n';
        if (size + line.size() + omitted.size() > max_log_characters) {
            truncated = true;
            break;
        }
        size += line.size();
        lines.push_back(std::move(line));
    }
    std::string result;
    result.reserve(size + (truncated ? omitted.size() : 0));
    if (truncated) result += omitted;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) result += *it;
    // The label needs no final separator, which would create an empty last row.
    // Additional payload LFs remain visible and raw records are never modified.
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

}  // namespace serial
