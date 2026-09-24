#include "pages/serial/serial_view_model.hpp"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>

namespace {

class Pseudoterminal {
public:
    Pseudoterminal()
    {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        assert(master_ >= 0);
        assert(::grantpt(master_) == 0);
        assert(::unlockpt(master_) == 0);
        const char* name = ::ptsname(master_);
        assert(name != nullptr);
        path_ = name;
    }

    ~Pseudoterminal()
    {
        close();
    }
    Pseudoterminal(const Pseudoterminal&)            = delete;
    Pseudoterminal& operator=(const Pseudoterminal&) = delete;

    const std::string& path() const
    {
        return path_;
    }

    void write(const std::string& bytes, serial::ViewModel& vm)
    {
        std::size_t offset = 0;
        int retries        = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(master_, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                assert(++retries < 1000);
                vm.poll();
                ::usleep(1000);
                continue;
            }
            assert(count > 0);
            offset += static_cast<std::size_t>(count);
        }
    }

    std::string read(std::size_t expected)
    {
        std::string result;
        while (result.size() < expected) {
            pollfd descriptor{master_, POLLIN, 0};
            assert(::poll(&descriptor, 1, 2000) > 0);
            char buffer[4096];
            const auto count = ::read(master_, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            assert(count > 0);
            result.append(buffer, static_cast<std::size_t>(count));
        }
        return result;
    }

    void close()
    {
        if (master_ >= 0) ::close(master_);
        master_ = -1;
    }

private:
    int master_ = -1;
    std::string path_;
};

void received(serial::ViewModel& view_model, std::uint64_t expected)
{
    for (int attempt = 0; attempt < 1000 && view_model.rx_bytes() < expected; ++attempt) {
        view_model.poll();
        if (view_model.rx_bytes() < expected) ::usleep(1000);
    }
    assert(view_model.rx_bytes() == expected);
}

void test_preferences_and_subject()
{
    serial::ViewModel view_model;
    assert(!view_model.connected());
    assert(!view_model.rx_hex());
    assert(!view_model.tx_hex());
    assert(view_model.timestamps());
    assert(view_model.autoscroll());
    assert(view_model.line_ending() == serial::EndOfLine::None);
    assert(view_model.settings().baud_rate == 115200);
    assert(view_model.settings().data_bits == 8);
    assert(view_model.settings().stop_bits == 1);
    assert(view_model.settings().parity == serial::Parity::None);
    assert(!view_model.hardware_flow());
    assert(view_model.log_text().empty());
    assert(view_model.rx_bytes() == 0 && view_model.tx_bytes() == 0);
    assert(view_model.connection_summary() == "115200 baud / 8N1");

    int notifications = 0;
    auto* observer    = lv_subject_add_observer(
        view_model.revision_subject(),
        [](lv_observer_t* observer, lv_subject_t*) { ++*static_cast<int*>(lv_observer_get_user_data(observer)); },
        &notifications);
    assert(observer != nullptr);
    const auto initial_notifications = notifications;
    const auto initial_revision      = lv_subject_get_int(view_model.revision_subject());
    view_model.poll();
    view_model.poll();
    view_model.set_rx_hex(false);
    view_model.set_tx_hex(false);
    assert(notifications == initial_notifications);
    assert(lv_subject_get_int(view_model.revision_subject()) == initial_revision);

    view_model.set_rx_hex(true);
    assert(view_model.rx_hex());
    assert(notifications == initial_notifications + 1);
    view_model.set_rx_hex(true);
    assert(notifications == initial_notifications + 1);
    view_model.set_tx_hex(true);
    view_model.set_timestamps(false);
    view_model.set_autoscroll(false);
    view_model.set_line_ending(serial::EndOfLine::CRLF);
    assert(view_model.tx_hex() && !view_model.timestamps() && !view_model.autoscroll());
    assert(view_model.line_ending() == serial::EndOfLine::CRLF);

    auto settings         = view_model.settings();
    settings.baud_rate    = 9600;
    settings.data_bits    = 7;
    settings.parity       = serial::Parity::Even;
    settings.stop_bits    = 2;
    settings.flow_control = serial::FlowControl::RtsCts;
    view_model.set_settings(settings);
    assert(view_model.settings().baud_rate == 9600);
    assert(view_model.hardware_flow());
    assert(view_model.connection_summary() == "9600 baud / 7E2 / RTS-CTS");
    settings.data_bits = 9;
    view_model.set_settings(settings);
    assert(view_model.error());
    assert(view_model.settings().data_bits == 7);
    view_model.set_line(serial::Line::Rts, true);
    assert(view_model.error() && !view_model.line(serial::Line::Rts));
    view_model.send("hello");
    assert(view_model.error() && view_model.tx_bytes() == 0);
    lv_observer_remove(observer);
}

void test_connection_and_data()
{
    Pseudoterminal terminal;
    serial::ViewModel view_model;
    view_model.select_path(terminal.path());
    assert(view_model.ports()[view_model.selected_port()].path == terminal.path());
    view_model.toggle_connection();
    assert(view_model.connected());
    assert(!view_model.error());

    const auto idle_revision = lv_subject_get_int(view_model.revision_subject());
    view_model.poll();
    assert(lv_subject_get_int(view_model.revision_subject()) == idle_revision);
    auto other_settings      = view_model.settings();
    other_settings.baud_rate = 9600;
    view_model.set_settings(other_settings);
    assert(view_model.error());
    assert(view_model.settings().baud_rate == 115200);
    view_model.select_path("/dev/unavailable-serial-test");
    assert(view_model.ports()[view_model.selected_port()].path == terminal.path());
    view_model.scan_ports();
    assert(view_model.ports()[view_model.selected_port()].path == terminal.path());

    view_model.set_line_ending(serial::EndOfLine::CRLF);
    view_model.send("PING");
    assert(!view_model.error());
    assert(terminal.read(6) == "PING\r\n");
    assert(view_model.tx_bytes() == 6);
    assert(view_model.log_text().empty());
    view_model.set_timestamps(false);
    assert(view_model.log_text().empty());
    view_model.set_timestamps(true);
    assert(view_model.log_text().empty());

    view_model.set_line_ending(serial::EndOfLine::None);
    view_model.set_tx_hex(true);
    view_model.send("00 41 fF");
    assert(!view_model.error());
    assert(terminal.read(3) == std::string("\x00\x41\xff", 3));
    assert(view_model.tx_bytes() == 9);
    view_model.send("A");
    assert(view_model.error() && view_model.tx_bytes() == 9);
    view_model.send("GG");
    assert(view_model.error() && view_model.tx_bytes() == 9);
    view_model.set_tx_hex(false);
    view_model.send("\xc3\xa9");
    assert(view_model.error() && view_model.tx_bytes() == 9);

    terminal.write(std::string("A\x00\xff\r\n", 5), view_model);
    received(view_model, 5);
    const auto ascii_log = view_model.log_text();
    assert(ascii_log.find("RX  A\\x00\\xFF\\r") != std::string::npos);
    view_model.set_rx_hex(true);
    assert(view_model.log_text().find("RX  41 00 FF 0D 0A") != std::string::npos);
    view_model.set_rx_hex(false);
    assert(view_model.log_text() == ascii_log);
    view_model.clear();
    assert(view_model.log_text().empty());
    assert(view_model.rx_bytes() == 0 && view_model.tx_bytes() == 0);

    // Device newlines already separate records. Preserve deliberate blank
    // lines, but omit the one final line separator used only for display.
    view_model.set_timestamps(false);
    terminal.write("first\n\n", view_model);
    received(view_model, 7);
    assert(view_model.log_text() == "RX  first\n");
    terminal.write("second\n", view_model);
    received(view_model, 14);
    assert(view_model.log_text() == "RX  first\n\nRX  second");
    view_model.set_rx_hex(true);
    assert(view_model.log_text().find("66 69 72 73 74 0A 0A") != std::string::npos);
    view_model.set_rx_hex(false);
    view_model.clear();

    // Binary-heavy traffic expands to four characters per byte in ASCII mode.
    // The display must remain bounded while counters retain the true byte total.
    const std::string chunk(1024, '\0');
    for (int i = 0; i < 48; ++i) {
        terminal.write(chunk, view_model);
        received(view_model, static_cast<std::uint64_t>(i + 1) * chunk.size());
    }
    assert(view_model.log_text().size() <= serial::ViewModel::max_log_characters);
    assert(view_model.log_text().find("Earlier data omitted") != std::string::npos);
    view_model.set_rx_hex(true);
    assert(view_model.log_text().size() <= serial::ViewModel::max_log_characters);

    // PTYs commonly reject modem controls. A failed ioctl must leave the
    // switch at its effective prior state instead of pretending it succeeded.
    const bool previous_rts = view_model.line(serial::Line::Rts);
    view_model.set_line(serial::Line::Rts, !previous_rts);
    if (view_model.error())
        assert(view_model.line(serial::Line::Rts) == previous_rts);
    else
        assert(view_model.line(serial::Line::Rts) == !previous_rts);

    terminal.close();
    for (int attempt = 0; attempt < 1000 && view_model.connected(); ++attempt) {
        view_model.poll();
        if (view_model.connected()) ::usleep(1000);
    }
    assert(!view_model.connected());
    assert(view_model.error());
    assert(!view_model.line(serial::Line::Rts));
    view_model.disconnect();
    assert(!view_model.connected() && !view_model.error());
}

void test_failed_connection()
{
    serial::ViewModel view_model;
    view_model.select_path("/dev/nonexistent-lilygo-serial-test");
    view_model.toggle_connection();
    assert(!view_model.connected());
    assert(view_model.error());
    assert(view_model.status().find("Cannot open") != std::string::npos);
    assert(view_model.log_text().empty());
}

}  // namespace

int main()
{
    lv_init();
    test_preferences_and_subject();
    test_connection_and_data();
    test_failed_connection();
    lv_deinit();
    return 0;
}
