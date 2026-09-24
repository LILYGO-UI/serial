#include "pages/serial/serial_model.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace serial {
namespace {

struct BaudRate {
    unsigned int value;
    speed_t native;
};
const std::vector<BaudRate>& baud_rates()
{
    static const std::vector<BaudRate> values = {
        {110, B110},         {300, B300},     {600, B600},     {1200, B1200},   {2400, B2400},     {4800, B4800},
        {9600, B9600},       {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}, {230400, B230400},
#ifdef B460800
        {460800, B460800},
#endif
#ifdef B500000
        {500000, B500000},
#endif
#ifdef B576000
        {576000, B576000},
#endif
#ifdef B921600
        {921600, B921600},
#endif
#ifdef B1000000
        {1000000, B1000000},
#endif
#ifdef B1500000
        {1500000, B1500000},
#endif
#ifdef B2000000
        {2000000, B2000000},
#endif
#ifdef B3000000
        {3000000, B3000000},
#endif
    };
    return values;
}

bool ascii_space(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

int hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string system_error(const std::string& operation, int error)
{
    return operation + ": " + std::strerror(error);
}

bool port_name(const std::string& name)
{
    if (name.compare(0, 3, "cu.") == 0 && name.size() > 3) return true;
    for (const auto* prefix : {"ttyUSB", "ttyACM", "ttyAMA", "ttyS", "ttyTHS", "serial"}) {
        const std::size_t length = std::strlen(prefix);
        if (name.size() > length && name.compare(0, length, prefix) == 0 &&
            std::all_of(name.begin() + static_cast<std::ptrdiff_t>(length), name.end(),
                        [](unsigned char c) { return c >= '0' && c <= '9'; }))
            return true;
    }
    return false;
}

}  // namespace

bool is_hidden_port_path(std::string_view path)
{
    return path == "/dev/ttyS0" || path == "/dev/serial0";
}

const std::vector<unsigned int>& supported_baud_rates()
{
    static const auto result = [] {
        std::vector<unsigned int> values;
        for (const auto& baud : baud_rates()) values.push_back(baud.value);
        return values;
    }();
    return result;
}

bool parse_send(std::string_view text, Format format, EndOfLine ending, std::vector<std::uint8_t>& bytes,
                std::string& error)
{
    bytes.clear();
    error.clear();
    std::vector<std::uint8_t> parsed;
    if (format == Format::Ascii) {
        for (const unsigned char value : text) {
            if (value > 0x7f) {
                error = "ASCII accepts only characters 0x00-0x7F. Use HEX for binary data.";
                return false;
            }
            parsed.push_back(value);
            if (parsed.size() > Model::max_pending_bytes) {
                error = "A message cannot exceed 64 KiB.";
                return false;
            }
        }
    } else if (format == Format::Hex) {
        for (std::size_t i = 0; i < text.size();) {
            if (ascii_space(static_cast<unsigned char>(text[i]))) {
                ++i;
                continue;
            }
            const int high = hex_value(static_cast<unsigned char>(text[i]));
            if (high < 0 || i + 1 >= text.size() || hex_value(static_cast<unsigned char>(text[i + 1])) < 0) {
                error = "HEX requires complete byte pairs, for example 48 65 6C 6C 6F.";
                return false;
            }
            parsed.push_back(
                static_cast<std::uint8_t>((high << 4) | hex_value(static_cast<unsigned char>(text[i + 1]))));
            if (parsed.size() > Model::max_pending_bytes) {
                error = "A message cannot exceed 64 KiB.";
                return false;
            }
            i += 2;
        }
    } else {
        error = "Invalid data format.";
        return false;
    }
    switch (ending) {
        case EndOfLine::None:
            break;
        case EndOfLine::LF:
            parsed.push_back('\n');
            break;
        case EndOfLine::CR:
            parsed.push_back('\r');
            break;
        case EndOfLine::CRLF:
            parsed.push_back('\r');
            parsed.push_back('\n');
            break;
        default:
            error = "Invalid line ending.";
            return false;
    }
    if (parsed.empty()) {
        error = "Enter data to send.";
        return false;
    }
    if (parsed.size() > Model::max_pending_bytes) {
        error = "A message cannot exceed 64 KiB.";
        return false;
    }
    bytes = std::move(parsed);
    return true;
}

std::string format_bytes(const std::vector<std::uint8_t>& bytes, Format format)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * (format == Format::Hex ? 3 : 1));
    for (const auto value : bytes) {
        if (format == Format::Hex) {
            if (!text.empty()) text.push_back(' ');
            text.push_back(digits[value >> 4]);
            text.push_back(digits[value & 15]);
        } else if ((value >= 0x20 && value <= 0x7e) || value == '\n' || value == '\t') {
            text.push_back(static_cast<char>(value));
        } else if (value == '\r') {
            text += "\\r";
        } else {
            text += "\\x";
            text.push_back(digits[value >> 4]);
            text.push_back(digits[value & 15]);
        }
    }
    return text;
}

Model::~Model()
{
    close_port();
}

bool Model::fail(const std::string& message)
{
    last_error_ = message;
    ++revision_;
    return false;
}

const std::vector<PortInfo>& Model::scan_ports()
{
    std::vector<PortInfo> found;
    std::error_code error;
    const auto add_directory = [&](const std::filesystem::path& path, bool filter) {
        std::error_code scan_error;
        std::filesystem::directory_iterator entry(path, scan_error), end;
        if (scan_error) {
            if (path == "/dev") error = scan_error;
            return;
        }
        while (entry != end) {
            const auto name = entry->path().filename().string();
            std::error_code type_error;
            if ((!filter || (port_name(name) && !is_hidden_port_path(entry->path().string()))) &&
                std::filesystem::is_character_file(entry->path(), type_error)) {
                found.push_back({entry->path().string(), name});
            }
            entry.increment(scan_error);
            if (scan_error) {
                if (path == "/dev") error = scan_error;
                break;
            }
        }
    };
    add_directory("/dev", true);
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    ports_      = std::move(found);
    last_error_ = error ? "Cannot scan serial ports: " + error.message() : "";
    ++revision_;
    return ports_;
}

bool Model::connect(const std::string& path, const Settings& settings)
{
    if (connected()) return fail("Disconnect the current port before connecting another port.");
    if (path.empty()) return fail("Select a serial port first.");
    const auto baud = std::find_if(baud_rates().begin(), baud_rates().end(),
                                   [&](const auto& candidate) { return candidate.value == settings.baud_rate; });
    if (baud == baud_rates().end()) return fail("Unsupported baud rate.");
    if (settings.data_bits < 5 || settings.data_bits > 8) return fail("Data bits must be 5, 6, 7, or 8.");
    if (settings.stop_bits != 1 && settings.stop_bits != 2) return fail("Stop bits must be 1 or 2.");
    if (settings.parity != Parity::None && settings.parity != Parity::Odd && settings.parity != Parity::Even)
        return fail("Invalid parity setting.");
    if (settings.flow_control != FlowControl::None && settings.flow_control != FlowControl::RtsCts &&
        settings.flow_control != FlowControl::XonXoff)
        return fail("Invalid flow control setting.");
#ifndef CRTSCTS
    if (settings.flow_control == FlowControl::RtsCts)
        return fail("Hardware flow control is not supported on this platform.");
#endif
    const int descriptor = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) return fail(system_error("Cannot open " + path, errno));
    termios previous{};
    if (::tcgetattr(descriptor, &previous) < 0) {
        const auto message = system_error("Cannot read serial settings", errno);
        ::close(descriptor);
        return fail(message);
    }
    int previous_modem_bits             = 0;
    const bool have_previous_modem_bits = (::ioctl(descriptor, TIOCMGET, &previous_modem_bits) == 0);
    termios config                      = previous;
    ::cfmakeraw(&config);
    config.c_iflag &= ~(IGNPAR | INPCK);
    config.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CMSPAR
    config.c_cflag &= ~CMSPAR;
#endif
    config.c_cflag |= CLOCAL | CREAD;
    switch (settings.data_bits) {
        case 5:
            config.c_cflag |= CS5;
            break;
        case 6:
            config.c_cflag |= CS6;
            break;
        case 7:
            config.c_cflag |= CS7;
            break;
        case 8:
            config.c_cflag |= CS8;
            break;
    }
    if (settings.parity != Parity::None) {
        config.c_cflag |= PARENB;
        config.c_iflag |= INPCK;
        if (settings.parity == Parity::Odd) config.c_cflag |= PARODD;
    }
    if (settings.stop_bits == 2) config.c_cflag |= CSTOPB;
#ifdef CRTSCTS
    config.c_cflag &= ~CRTSCTS;
    if (settings.flow_control == FlowControl::RtsCts) config.c_cflag |= CRTSCTS;
#endif
    config.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (settings.flow_control == FlowControl::XonXoff) {
        config.c_iflag |= IXON | IXOFF;
        config.c_cc[VSTART] = 0x11;
        config.c_cc[VSTOP]  = 0x13;
    }
    config.c_cc[VMIN]  = 0;
    config.c_cc[VTIME] = 0;
    if (::cfsetispeed(&config, baud->native) < 0 || ::cfsetospeed(&config, baud->native) < 0 ||
        ::tcsetattr(descriptor, TCSANOW, &config) < 0) {
        const auto message = system_error("Cannot apply serial settings", errno);
        ::tcsetattr(descriptor, TCSANOW, &previous);
        if (have_previous_modem_bits) ::ioctl(descriptor, TIOCMSET, &previous_modem_bits);
        ::close(descriptor);
        return fail(message);
    }
    termios applied{};
    tcflag_t setting_mask = CSIZE | PARENB | PARODD | CSTOPB;
#ifdef CRTSCTS
    setting_mask |= CRTSCTS;
#endif
    if (::tcgetattr(descriptor, &applied) < 0 || (applied.c_cflag & setting_mask) != (config.c_cflag & setting_mask) ||
        ::cfgetispeed(&applied) != baud->native || ::cfgetospeed(&applied) != baud->native) {
        ::tcsetattr(descriptor, TCSANOW, &previous);
        if (have_previous_modem_bits) ::ioctl(descriptor, TIOCMSET, &previous_modem_bits);
        ::close(descriptor);
        return fail("The serial driver could not apply the requested settings.");
    }
    fd_                       = descriptor;
    original_termios_         = previous;
    original_modem_bits_      = previous_modem_bits;
    have_original_modem_bits_ = have_previous_modem_bits;
    // Start every connection with software-controlled modem lines released.
    // Drivers without modem-control support simply reject this ioctl; those
    // devices are still usable and report an unavailable line state below.
    int lines_to_clear = TIOCM_RTS | TIOCM_DTR;
    ::ioctl(fd_, TIOCMBIC, &lines_to_clear);
    int current_modem_bits             = 0;
    const bool have_current_modem_bits = (::ioctl(fd_, TIOCMGET, &current_modem_bits) == 0);
    rts_                               = have_current_modem_bits && (current_modem_bits & TIOCM_RTS);
    dtr_                               = have_current_modem_bits && (current_modem_bits & TIOCM_DTR);
    break_                             = false;
    settings_                          = settings;
    device_                            = path;
    last_error_.clear();
    ++revision_;
    return true;
}

void Model::close_port()
{
    if (fd_ >= 0) {
#ifdef TIOCCBRK
        if (break_) ::ioctl(fd_, TIOCCBRK);
#endif
        ::tcsetattr(fd_, TCSANOW, &original_termios_);
        if (have_original_modem_bits_) ::ioctl(fd_, TIOCMSET, &original_modem_bits_);
        ::close(fd_);
        fd_ = -1;
    }
    pending_.clear();
    pending_offset_           = 0;
    have_original_modem_bits_ = false;
    rts_ = dtr_ = break_ = false;
}

void Model::disconnect()
{
    close_port();
    last_error_.clear();
    ++revision_;
}

void Model::append_record(Direction direction, const std::uint8_t* data, std::size_t size)
{
    if (size == 0) return;
    if (direction == Direction::Rx)
        rx_bytes_ += size;
    else
        tx_bytes_ += size;
    records_.push_back(
        {next_record_id_++, direction, std::chrono::system_clock::now(), std::vector<std::uint8_t>(data, data + size)});
    buffered_bytes_ += size;
    while (records_.size() > max_records || buffered_bytes_ > max_buffer_bytes) {
        buffered_bytes_ -= records_.front().bytes.size();
        records_.pop_front();
    }
    ++revision_;
}

void Model::flush_pending()
{
    while (connected() && queued_bytes() > 0) {
        const auto count = ::write(fd_, pending_.data() + pending_offset_, queued_bytes());
        if (count > 0) {
            append_record(Direction::Tx, pending_.data() + pending_offset_, static_cast<std::size_t>(count));
            pending_offset_ += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            const auto message = system_error("Serial write failed; port disconnected", errno);
            close_port();
            fail(message);
            return;
        }
    }
    if (pending_offset_ == pending_.size()) {
        pending_.clear();
        pending_offset_ = 0;
    }
}

bool Model::send(const std::vector<std::uint8_t>& bytes)
{
    if (!connected()) return fail("Connect a serial port before sending data.");
    if (bytes.empty()) return fail("Enter data to send.");
    if (bytes.size() > max_pending_bytes - queued_bytes())
        return fail("The 64 KiB output queue is full. Wait for pending data to send.");
    if (pending_offset_ > 0) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(pending_offset_));
        pending_offset_ = 0;
    }
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    last_error_.clear();
    ++revision_;
    flush_pending();
    return connected();
}

void Model::poll()
{
    if (!connected()) return;
    int modem_bits = 0;
    if (::ioctl(fd_, TIOCMGET, &modem_bits) == 0) {
        const bool rts = (modem_bits & TIOCM_RTS) != 0;
        const bool dtr = (modem_bits & TIOCM_DTR) != 0;
        if (rts != rts_ || dtr != dtr_) {
            rts_ = rts;
            dtr_ = dtr;
            ++revision_;
        }
    }
    pollfd descriptor{fd_, static_cast<short>(POLLIN | (queued_bytes() ? POLLOUT : 0)), 0};
    const int result = ::poll(&descriptor, 1, 0);
    if (result < 0) {
        if (errno == EINTR) return;
        const auto message = system_error("Serial poll failed; port disconnected", errno);
        close_port();
        fail(message);
        return;
    }
    if (result == 0) return;
    if (descriptor.revents & POLLIN) {
        std::uint8_t buffer[4096];
        std::size_t received = 0;
        while (received < max_pending_bytes) {
            const auto count = ::read(fd_, buffer, sizeof(buffer));
            if (count > 0) {
                append_record(Direction::Rx, buffer, static_cast<std::size_t>(count));
                received += static_cast<std::size_t>(count);
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                const auto message = system_error("Serial read failed; port disconnected", errno);
                close_port();
                fail(message);
                return;
            }
        }
    }
    if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        close_port();
        fail("Serial port disconnected. Check the cable and reconnect.");
        return;
    }
    if (descriptor.revents & POLLOUT) flush_pending();
}

bool Model::set_line(Line line, bool enabled)
{
    if (!connected()) return fail("Connect a serial port before changing line state.");
    const char* name = line == Line::Rts ? "RTS" : line == Line::Dtr ? "DTR" : "BREAK";
    if (line == Line::Break) {
#if defined(TIOCSBRK) && defined(TIOCCBRK)
        if (::ioctl(fd_, enabled ? TIOCSBRK : TIOCCBRK) < 0)
            return fail(system_error("Cannot set BREAK (driver may not support it)", errno));
        break_ = enabled;
#else
        return fail("BREAK is not supported on this platform.");
#endif
    } else if (line == Line::Rts || line == Line::Dtr) {
        if (line == Line::Rts && settings_.flow_control == FlowControl::RtsCts)
            return fail("RTS is controlled by hardware flow control. Disable RTS/CTS to set it manually.");
        int bits = line == Line::Rts ? TIOCM_RTS : TIOCM_DTR;
        if (::ioctl(fd_, enabled ? TIOCMBIS : TIOCMBIC, &bits) < 0)
            return fail(system_error(std::string("Cannot set ") + name + " (driver may not support it)", errno));
        if (line == Line::Rts)
            rts_ = enabled;
        else
            dtr_ = enabled;
        int current_bits = 0;
        if (::ioctl(fd_, TIOCMGET, &current_bits) == 0) {
            rts_ = (current_bits & TIOCM_RTS) != 0;
            dtr_ = (current_bits & TIOCM_DTR) != 0;
        }
    } else {
        return fail("Invalid serial line.");
    }
    last_error_.clear();
    ++revision_;
    return true;
}

bool Model::line_state(Line line) const noexcept
{
    switch (line) {
        case Line::Rts:
            return rts_;
        case Line::Dtr:
            return dtr_;
        case Line::Break:
            return break_;
    }
    return false;
}

void Model::clear()
{
    records_.clear();
    buffered_bytes_ = 0;
    rx_bytes_ = tx_bytes_ = 0;
    ++revision_;
}

}  // namespace serial
