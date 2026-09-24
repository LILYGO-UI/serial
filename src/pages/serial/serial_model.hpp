#ifndef LILYGO_UI_SERIAL_MODEL_HPP
#define LILYGO_UI_SERIAL_MODEL_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <termios.h>
#include <vector>

namespace serial {

enum class Parity { None, Odd, Even };
enum class FlowControl { None, RtsCts, XonXoff };
enum class Format { Ascii, Hex };
enum class EndOfLine { None, LF, CR, CRLF };
enum class Line { Rts, Dtr, Break };
enum class Direction { Rx, Tx };

struct Settings {
    unsigned int baud_rate   = 115200;
    int data_bits            = 8;
    Parity parity            = Parity::None;
    int stop_bits            = 1;
    FlowControl flow_control = FlowControl::None;
};

struct PortInfo {
    std::string path;
    std::string name;
};

// These onboard CM0 aliases are intentionally hidden from the port picker.
bool is_hidden_port_path(std::string_view path);

struct Record {
    std::uint64_t id;
    Direction direction;
    std::chrono::system_clock::time_point timestamp;
    std::vector<std::uint8_t> bytes;
};

// HEX accepts byte pairs, contiguous or separated by ASCII whitespace.
// ASCII rejects bytes above 0x7F. Errors never leave a partial output.
bool parse_send(std::string_view text, Format format, EndOfLine ending, std::vector<std::uint8_t>& bytes,
                std::string& error);
std::string format_bytes(const std::vector<std::uint8_t>& bytes, Format format);
const std::vector<unsigned int>& supported_baud_rates();

// Single-threaded, nonblocking POSIX backend. No LVGL dependency.
class Model {
public:
    static constexpr std::size_t max_buffer_bytes  = 256 * 1024;
    static constexpr std::size_t max_records       = 512;
    static constexpr std::size_t max_pending_bytes = 64 * 1024;

    Model() = default;
    ~Model();
    Model(const Model&)            = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&)                 = delete;
    Model& operator=(Model&&)      = delete;

    const std::vector<PortInfo>& scan_ports();
    bool connect(const std::string& path, const Settings& settings);
    void disconnect();
    void poll();
    bool send(const std::vector<std::uint8_t>& bytes);
    bool set_line(Line line, bool enabled);
    // Clear displayed records and byte counters; queued output is retained.
    void clear();

    [[nodiscard]] bool connected() const noexcept
    {
        return fd_ >= 0;
    }
    [[nodiscard]] const std::string& device() const noexcept
    {
        return device_;
    }
    [[nodiscard]] const Settings& settings() const noexcept
    {
        return settings_;
    }
    [[nodiscard]] const std::string& last_error() const noexcept
    {
        return last_error_;
    }
    [[nodiscard]] const std::vector<PortInfo>& ports() const noexcept
    {
        return ports_;
    }
    [[nodiscard]] const std::deque<Record>& records() const noexcept
    {
        return records_;
    }
    [[nodiscard]] std::uint64_t rx_bytes() const noexcept
    {
        return rx_bytes_;
    }
    [[nodiscard]] std::uint64_t tx_bytes() const noexcept
    {
        return tx_bytes_;
    }
    [[nodiscard]] std::size_t buffered_bytes() const noexcept
    {
        return buffered_bytes_;
    }
    [[nodiscard]] std::size_t queued_bytes() const noexcept
    {
        return pending_.size() - pending_offset_;
    }
    [[nodiscard]] std::uint64_t revision() const noexcept
    {
        return revision_;
    }
    [[nodiscard]] bool line_state(Line line) const noexcept;

private:
    bool fail(const std::string& message);
    void close_port();
    void flush_pending();
    void append_record(Direction direction, const std::uint8_t* data, std::size_t size);

    int fd_ = -1;
    termios original_termios_{};
    int original_modem_bits_       = 0;
    bool have_original_modem_bits_ = false;
    bool rts_                      = false;
    bool dtr_                      = false;
    bool break_                    = false;
    std::string device_;
    Settings settings_;
    std::string last_error_;
    std::vector<PortInfo> ports_;
    std::deque<Record> records_;
    std::vector<std::uint8_t> pending_;
    std::size_t pending_offset_   = 0;
    std::size_t buffered_bytes_   = 0;
    std::uint64_t rx_bytes_       = 0;
    std::uint64_t tx_bytes_       = 0;
    std::uint64_t next_record_id_ = 1;
    std::uint64_t revision_       = 0;
};

}  // namespace serial

#endif
