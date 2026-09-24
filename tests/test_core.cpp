#include "pages/serial/serial_model.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

class Pty {
public:
    Pty()
    {
        master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        assert(master >= 0);
        assert(::grantpt(master) == 0);
        assert(::unlockpt(master) == 0);
        const auto* name = ::ptsname(master);
        assert(name != nullptr);
        path = name;
    }
    ~Pty()
    {
        if (master >= 0) ::close(master);
    }
    void hangup()
    {
        assert(::close(master) == 0);
        master = -1;
    }
    int master = -1;
    std::string path;
};

template <typename Predicate, typename Step>
void until(Predicate done, Step step)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        assert(std::chrono::steady_clock::now() < deadline);
        step();
        ::usleep(1000);
    }
}

std::vector<std::uint8_t> read_available(int descriptor)
{
    std::vector<std::uint8_t> result;
    std::uint8_t buffer[8192];
    while (true) {
        const auto count = ::read(descriptor, buffer, sizeof(buffer));
        if (count > 0)
            result.insert(result.end(), buffer, buffer + count);
        else {
            assert(count == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
            if (count < 0 && errno == EINTR) continue;
            return result;
        }
    }
}

void receive(serial::Model& model, Pty& pty, const std::vector<std::uint8_t>& bytes)
{
    const auto target = model.rx_bytes() + bytes.size();
    std::size_t sent  = 0;
    until([&] { return model.rx_bytes() == target; },
          [&] {
              if (sent < bytes.size()) {
                  const auto count = ::write(pty.master, bytes.data() + sent, bytes.size() - sent);
                  if (count > 0)
                      sent += static_cast<std::size_t>(count);
                  else
                      assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
              }
              model.poll();
              assert(model.connected());
          });
}

void test_conversions()
{
    using serial::EndOfLine;
    using serial::Format;
    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(serial::parse_send("Hello", Format::Ascii, EndOfLine::None, bytes, error));
    assert(std::string(bytes.begin(), bytes.end()) == "Hello");
    assert(error.empty());
    assert(serial::parse_send("00ff 41\t0a\n7F", Format::Hex, EndOfLine::CRLF, bytes, error));
    assert((bytes == std::vector<std::uint8_t>{0, 255, 65, 10, 127, 13, 10}));
    assert(serial::format_bytes(bytes, Format::Hex) == "00 FF 41 0A 7F 0D 0A");
    assert(serial::format_bytes(bytes, Format::Ascii) == "\\x00\\xFFA\n\\x7F\\r\n");
    for (const auto* invalid : {"A", "A B", "0x12", "GG", "12,34", "123", "12 3Z"}) {
        assert(!serial::parse_send(invalid, Format::Hex, EndOfLine::None, bytes, error));
        assert(bytes.empty());
        assert(!error.empty());
    }
    assert(!serial::parse_send("你好", Format::Ascii, EndOfLine::None, bytes, error));
    assert(bytes.empty());
    assert(!serial::parse_send("", Format::Ascii, EndOfLine::None, bytes, error));
    assert(!serial::parse_send(" \t", Format::Hex, EndOfLine::None, bytes, error));
    assert(serial::parse_send("", Format::Ascii, EndOfLine::LF, bytes, error));
    assert((bytes == std::vector<std::uint8_t>{10}));
    assert(serial::parse_send(std::string_view("\0\r\n", 3), Format::Ascii, EndOfLine::CR, bytes, error));
    assert((bytes == std::vector<std::uint8_t>{0, 13, 10, 13}));
    assert(!serial::parse_send(std::string(serial::Model::max_pending_bytes + 1, 'a'), Format::Ascii, EndOfLine::None,
                               bytes, error));
    assert(bytes.empty());
}

void test_validation_and_scan()
{
    assert(serial::is_hidden_port_path("/dev/ttyS0"));
    assert(serial::is_hidden_port_path("/dev/serial0"));
    assert(!serial::is_hidden_port_path("/dev/ttyS1"));
    assert(!serial::is_hidden_port_path("/dev/serial1"));

    serial::Model model;
    assert(!model.connected());
    assert(model.settings().baud_rate == 115200);
    assert(model.settings().data_bits == 8);
    assert(model.settings().stop_bits == 1);
    assert(model.settings().parity == serial::Parity::None);
    assert(model.settings().flow_control == serial::FlowControl::None);
    assert(!model.send({65}));
    assert(!model.set_line(serial::Line::Dtr, true));
    assert(!model.connect("", {}));
    assert(!model.connect("/dev/serial-debugger-nonexistent", {}));
    auto invalid      = serial::Settings{};
    invalid.data_bits = 9;
    assert(!model.connect("/dev/null", invalid));
    assert(model.last_error().find("Data bits") != std::string::npos);
    invalid           = {};
    invalid.baud_rate = 12345;
    assert(!model.connect("/dev/null", invalid));
    invalid           = {};
    invalid.stop_bits = 3;
    assert(!model.connect("/dev/null", invalid));
    assert(!model.connect("/dev/null", {}));
    const auto& ports = model.scan_ports();
    assert(std::is_sorted(ports.begin(), ports.end(), [](const auto& a, const auto& b) { return a.path < b.path; }));
    for (const auto& port : ports) {
        assert(!port.name.empty());
        assert(port.path.compare(0, 5, "/dev/") == 0);
    }
    const auto revision = model.revision();
    model.poll();
    assert(model.revision() == revision);
}

void test_roundtrip_and_restoration()
{
    Pty pty;
    const int observer = ::open(pty.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(observer >= 0);
    termios original{};
    assert(::tcgetattr(observer, &original) == 0);
    original.c_lflag |= ECHO | ICANON;
    assert(::tcsetattr(observer, TCSANOW, &original) == 0);
    serial::Model model;

    // When the driver exposes modem controls, seed both lines high so the
    // connection path must explicitly release them instead of inheriting the
    // state left by the previous owner.
    int initial_modem_bits        = 0;
    const bool have_modem_bits    = ::ioctl(observer, TIOCMGET, &initial_modem_bits) == 0;
    const int seeded_modem_bits   = initial_modem_bits | TIOCM_RTS | TIOCM_DTR;
    const bool seeded_modem_lines = have_modem_bits && ::ioctl(observer, TIOCMSET, &seeded_modem_bits) == 0;
    assert(model.connect(pty.path, {}));
    assert(model.device() == pty.path);
    assert(!model.connect(pty.path, {}));
    if (seeded_modem_lines) {
        int connected_modem_bits = 0;
        assert(::ioctl(observer, TIOCMGET, &connected_modem_bits) == 0);
        assert((connected_modem_bits & (TIOCM_RTS | TIOCM_DTR)) == 0);
        assert(!model.line_state(serial::Line::Rts));
        assert(!model.line_state(serial::Line::Dtr));
    }
    termios connected{};
    assert(::tcgetattr(observer, &connected) == 0);
    assert(!(connected.c_lflag & (ECHO | ICANON)));
    assert(::cfgetospeed(&connected) == B115200);
    const auto revision = model.revision();
    model.poll();
    assert(model.revision() == revision);
    std::vector<std::uint8_t> binary;
    for (int i = 0; i < 256; ++i) binary.push_back(static_cast<std::uint8_t>(i));
    const auto before = std::chrono::system_clock::now();
    receive(model, pty, binary);
    assert(model.rx_bytes() == binary.size());
    std::vector<std::uint8_t> recorded;
    for (const auto& record : model.records()) {
        assert(record.direction == serial::Direction::Rx);
        assert(record.timestamp >= before);
        recorded.insert(recorded.end(), record.bytes.begin(), record.bytes.end());
    }
    assert(recorded == binary);
    assert(model.send(binary));
    std::vector<std::uint8_t> transmitted;
    until([&] { return transmitted.size() == binary.size(); },
          [&] {
              model.poll();
              const auto part = read_available(pty.master);
              transmitted.insert(transmitted.end(), part.begin(), part.end());
          });
    assert(transmitted == binary);
    assert(model.tx_bytes() == binary.size());
    assert(model.queued_bytes() == 0);
    for (const auto line : {serial::Line::Rts, serial::Line::Dtr, serial::Line::Break}) {
        const bool previous = model.line_state(line);
        if (model.set_line(line, !previous)) {
            assert(model.line_state(line) == !previous);
            assert(model.set_line(line, previous));
        } else {
            assert(model.line_state(line) == previous);
            assert(!model.last_error().empty());
        }
    }
    model.disconnect();
    assert(!model.connected());
    assert(model.last_error().empty());
    termios restored{};
    assert(::tcgetattr(observer, &restored) == 0);
    assert(original.c_iflag == restored.c_iflag);
    assert(original.c_oflag == restored.c_oflag);
    assert(original.c_cflag == restored.c_cflag);
    // BSD may set PENDIN when returning to canonical input; it is kernel state.
#ifdef PENDIN
    assert((original.c_lflag & ~PENDIN) == (restored.c_lflag & ~PENDIN));
#else
    assert(original.c_lflag == restored.c_lflag);
#endif
    assert(std::memcmp(original.c_cc, restored.c_cc, sizeof(original.c_cc)) == 0);
    assert(::cfgetispeed(&original) == ::cfgetispeed(&restored));
    assert(::cfgetospeed(&original) == ::cfgetospeed(&restored));
    ::close(observer);
}

void test_partial_writes_and_limits()
{
    Pty pty;
    serial::Model model;
    assert(model.connect(pty.path, {}));
    std::vector<std::uint8_t> payload(16 * 1024);
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i);
    std::vector<std::uint8_t> expected;
    bool full = false;
    for (int i = 0; i < 64; ++i) {
        if (model.send(payload))
            expected.insert(expected.end(), payload.begin(), payload.end());
        else {
            full = true;
            break;
        }
    }
    assert(full);
    assert(model.connected());
    assert(model.queued_bytes() > 0);
    assert(model.queued_bytes() <= serial::Model::max_pending_bytes);
    const auto queued = model.queued_bytes();
    assert(!model.send(std::vector<std::uint8_t>(serial::Model::max_pending_bytes + 1)));
    assert(model.queued_bytes() == queued);
    std::vector<std::uint8_t> transmitted;
    until([&] { return transmitted.size() == expected.size() && model.queued_bytes() == 0; },
          [&] {
              const auto part = read_available(pty.master);
              transmitted.insert(transmitted.end(), part.begin(), part.end());
              model.poll();
          });
    assert(transmitted == expected);
    assert(model.tx_bytes() == expected.size());
    model.clear();
    assert(model.rx_bytes() == 0 && model.tx_bytes() == 0 && model.records().empty());
    std::vector<std::uint8_t> inbound(4096, 0x55);
    for (int i = 0; i < 70; ++i) receive(model, pty, inbound);
    assert(model.rx_bytes() == 70 * inbound.size());
    assert(model.buffered_bytes() <= serial::Model::max_buffer_bytes);
    assert(model.buffered_bytes() < model.rx_bytes());
    model.clear();
    for (std::size_t i = 0; i < serial::Model::max_records + 10; ++i) receive(model, pty, {42});
    assert(model.records().size() == serial::Model::max_records);
    assert(model.rx_bytes() == serial::Model::max_records + 10);
    pty.hangup();
    until([&] { return !model.connected(); }, [&] { model.poll(); });
    assert(!model.last_error().empty());
    assert(model.queued_bytes() == 0);
    assert(!model.send({1}));
}

void test_serial_settings()
{
    Pty pty;
    serial::Model model;
    serial::Settings settings;
    settings.baud_rate    = 9600;
    settings.stop_bits    = 2;
    settings.flow_control = serial::FlowControl::XonXoff;
    assert(model.connect(pty.path, settings));
    const int observer = ::open(pty.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(observer >= 0);
    termios applied{};
    assert(::tcgetattr(observer, &applied) == 0);
    assert(::cfgetispeed(&applied) == B9600);
    assert(::cfgetospeed(&applied) == B9600);
    assert(applied.c_cflag & CSTOPB);
    assert(applied.c_iflag & IXON);
    assert(applied.c_iflag & IXOFF);
    assert(!(applied.c_iflag & IXANY));
    assert(applied.c_cc[VSTART] == 0x11 && applied.c_cc[VSTOP] == 0x13);
    model.disconnect();
    ::close(observer);
#ifdef CRTSCTS
    settings.flow_control = serial::FlowControl::RtsCts;
    // Some PTY drivers reject hardware flow control; real drivers vary too.
    if (model.connect(pty.path, settings)) {
        assert(!model.set_line(serial::Line::Rts, true));
        assert(model.last_error().find("hardware flow control") != std::string::npos);
    } else {
        assert(!model.last_error().empty());
    }
#endif
}

}  // namespace

int main()
{
    test_conversions();
    test_validation_and_scan();
    test_roundtrip_and_restoration();
    test_partial_writes_and_limits();
    test_serial_settings();
    return 0;
}
