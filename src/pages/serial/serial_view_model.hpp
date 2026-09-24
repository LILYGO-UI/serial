#ifndef LILYGO_UI_SERIAL_VIEW_MODEL_HPP
#define LILYGO_UI_SERIAL_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/serial/serial_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace serial {

// Owns presentation preferences and translates backend changes into one Subject.
// Its View must be destroyed first; the View owns polling and all LVGL objects.
class ViewModel {
public:
    static constexpr std::size_t max_log_characters = 32 * 1024;

    ViewModel();

    [[nodiscard]] lv_subject_t* revision_subject() noexcept
    {
        return revision_.get();
    }
    [[nodiscard]] const std::vector<PortInfo>& ports() const noexcept
    {
        return ports_;
    }
    [[nodiscard]] std::size_t selected_port() const noexcept
    {
        return selected_port_;
    }
    [[nodiscard]] const Settings& settings() const noexcept
    {
        return settings_;
    }
    [[nodiscard]] bool connected() const noexcept
    {
        return model_.connected();
    }
    [[nodiscard]] const std::string& status() const noexcept
    {
        return status_;
    }
    [[nodiscard]] bool error() const noexcept
    {
        return error_;
    }
    [[nodiscard]] std::uint64_t rx_bytes() const noexcept
    {
        return model_.rx_bytes();
    }
    [[nodiscard]] std::uint64_t tx_bytes() const noexcept
    {
        return model_.tx_bytes();
    }
    [[nodiscard]] bool rx_hex() const noexcept
    {
        return rx_hex_;
    }
    [[nodiscard]] bool tx_hex() const noexcept
    {
        return tx_hex_;
    }
    [[nodiscard]] bool timestamps() const noexcept
    {
        return timestamps_;
    }
    [[nodiscard]] bool autoscroll() const noexcept
    {
        return autoscroll_;
    }
    [[nodiscard]] bool line(Line line) const noexcept
    {
        return model_.line_state(line);
    }
    [[nodiscard]] bool hardware_flow() const noexcept
    {
        return settings_.flow_control == FlowControl::RtsCts;
    }
    [[nodiscard]] EndOfLine line_ending() const noexcept
    {
        return line_ending_;
    }
    [[nodiscard]] std::string log_text() const;
    [[nodiscard]] std::string connection_summary() const;

    void scan_ports();
    void select_port(std::size_t index);
    // Also useful for explicitly addressed devices and pseudoterminal tests.
    void select_path(const std::string& path);
    void set_settings(Settings settings);
    void toggle_connection();
    void disconnect();
    void poll();
    void set_rx_hex(bool enabled);
    void set_tx_hex(bool enabled);
    void set_timestamps(bool enabled);
    void set_autoscroll(bool enabled);
    void set_line(Line line, bool enabled);
    void set_line_ending(EndOfLine ending);
    void send(const std::string& text);
    void clear();

private:
    void publish();
    bool set_status(std::string status, bool error = false);
    void report_error(const std::string& message);

    Model model_;
    std::vector<PortInfo> ports_;
    std::size_t selected_port_ = 0;
    Settings settings_;
    std::string status_;
    bool error_                            = false;
    bool rx_hex_                           = false;
    bool tx_hex_                           = false;
    bool timestamps_                       = true;
    bool autoscroll_                       = true;
    EndOfLine line_ending_                 = EndOfLine::None;
    std::uint64_t observed_model_revision_ = 0;
    IntSubject revision_{0};
};

}  // namespace serial

#endif
