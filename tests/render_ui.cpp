#include "app.hpp"
#include "pages/serial/serial_view.hpp"

#include <cm0/typography.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kPartialRows = 80;
std::vector<lv_color32_t> framebuffer;
int display_width  = 0;
int display_height = 0;

void flush_display(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto* source = reinterpret_cast<const lv_color32_t*>(pixels);
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < display_width && area->y2 < display_height);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset      = static_cast<std::size_t>(y - area->y1) * width;
        const auto destination_offset = static_cast<std::size_t>(y) * display_width + area->x1;
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

void frame(lv_display_t* display)
{
    lv_tick_inc(25);
    lv_timer_handler();
    lv_obj_update_layout(lv_display_get_screen_active(display));
    lv_refr_now(display);
}

void save(lv_display_t* display, const std::string& path)
{
    frame(display);
    std::ofstream file(path, std::ios::binary);
    assert(file);
    file << "P6\n" << display_width << ' ' << display_height << "\n255\n";
    for (const auto& pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    assert(file.good());
    const auto& first = framebuffer.front();
    assert(std::any_of(framebuffer.begin() + 1, framebuffer.end(), [&](const auto& pixel) {
        return pixel.red != first.red || pixel.green != first.green || pixel.blue != first.blue;
    }));
}

lv_obj_t* named(lv_obj_t* root, const char* name)
{
    auto* object = lv_obj_find_by_name(root, name);
    if (!object) std::cerr << "Missing object: " << name << '\n';
    assert(object);
    return object;
}

void click(lv_obj_t* object)
{
    assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
}

void select(lv_obj_t* object, std::uint32_t value)
{
    assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
    lv_dropdown_set_selected(object, value);
    lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
}

void toggle(lv_obj_t* object, bool value)
{
    assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
    if (value)
        lv_obj_add_state(object, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
}

void reachable(lv_obj_t* object, const char* name)
{
    lv_obj_update_layout(object);
    lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
    lv_obj_update_layout(object);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    assert(lv_obj_get_width(object) > 0 && lv_obj_get_height(object) >= 30);
    for (auto* parent = lv_obj_get_parent(object); parent; parent = lv_obj_get_parent(parent)) {
        lv_area_t bounds{};
        lv_obj_get_coords(parent, &bounds);
        const bool contained = area.x1 >= bounds.x1 - 1 && area.x2 <= bounds.x2 + 1 && area.y1 >= bounds.y1 - 1 &&
                               area.y2 <= bounds.y2 + 1;
        if (!contained) {
            std::cerr << "Clipped " << name << " at " << display_width << 'x' << display_height << ": [" << area.x1
                      << ',' << area.y1 << ',' << area.x2 << ',' << area.y2 << "] parent [" << bounds.x1 << ','
                      << bounds.y1 << ',' << bounds.x2 << ',' << bounds.y2 << "]\n";
        }
        assert(contained);
    }
}

class Pseudoterminal {
public:
    Pseudoterminal()
    {
        descriptor_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        assert(descriptor_ >= 0);
        assert(::grantpt(descriptor_) == 0);
        assert(::unlockpt(descriptor_) == 0);
        const char* path = ::ptsname(descriptor_);
        assert(path);
        path_ = path;
    }
    ~Pseudoterminal()
    {
        ::close(descriptor_);
    }
    const std::string& path() const
    {
        return path_;
    }
    void write(const std::string& bytes, serial::ViewModel& vm)
    {
        std::size_t offset = 0;
        int retries        = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(descriptor_, bytes.data() + offset, bytes.size() - offset);
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
            pollfd event{descriptor_, POLLIN, 0};
            assert(::poll(&event, 1, 2000) > 0);
            char buffer[4096];
            const auto count = ::read(descriptor_, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            assert(count > 0);
            result.append(buffer, static_cast<std::size_t>(count));
        }
        return result;
    }

private:
    int descriptor_ = -1;
    std::string path_;
};

void receive(lv_display_t* display, serial::ViewModel& vm, Pseudoterminal& terminal, const std::string& data)
{
    const auto expected = vm.rx_bytes() + data.size();
    terminal.write(data, vm);
    for (int attempt = 0; attempt < 1000 && vm.rx_bytes() < expected; ++attempt) {
        frame(display);  // Exercise the View's polling timer.
        if (vm.rx_bytes() < expected) ::usleep(1000);
    }
    assert(vm.rx_bytes() == expected);
}

void reset_scroll(lv_obj_t* surface)
{
    auto* sidebar   = named(surface, "settings_panel");
    auto* workspace = named(surface, "workspace");
    lv_obj_scroll_to_y(sidebar, 0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(workspace, 0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(lv_obj_get_parent(sidebar), 0, LV_ANIM_OFF);
}

void check_controls(lv_obj_t* surface)
{
    for (const char* name :
         {"port_select", "baud_rate", "data_bits",  "parity",     "stop_bits",   "flow_control", "connect_button",
          "line_rts",    "line_dtr",  "line_break", "rx_ascii",   "rx_hex",      "show_time",    "auto_scroll",
          "clear_log",   "tx_ascii",  "tx_hex",     "send_input", "line_ending", "send_button"}) {
        reachable(named(surface, name), name);
    }
}

void check_log_bottom(lv_obj_t* scroll, lv_obj_t* log)
{
    lv_obj_update_layout(scroll);
    lv_area_t content{}, label{};
    lv_obj_get_content_coords(scroll, &content);
    lv_obj_get_coords(log, &label);
    const auto visible_height = content.y2 - content.y1 + 1;
    const auto expected_scroll = std::max(0, lv_obj_get_height(log) - visible_height);
    assert(lv_obj_get_scroll_y(scroll) == expected_scroll);
    if (expected_scroll > 0) assert(label.y2 == content.y2);
    else assert(label.y1 == content.y1);
}

void test_view(lv_display_t* display, const std::string& output)
{
    const std::string prefix = output.substr(0, output.rfind('.'));
    Pseudoterminal terminal;
    serial::ViewModel vm;
    serial::View view(vm);
    auto* root    = lv_display_get_screen_active(display);
    auto* surface = view.create(root, "Serial");
    assert(view.create(root, "Serial") == surface);
    assert(lv_obj_get_parent(surface) == root);
    frame(display);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(0xf2f2f7)));
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(lv_color_eq(lv_obj_get_style_bg_color(named(surface, "connection_card"), LV_PART_MAIN),
                       lv_color_hex(0xffffff)));
    assert(!vm.rx_hex() && !vm.tx_hex() && vm.timestamps() && vm.autoscroll());
    assert(lv_obj_has_state(named(surface, "send_button"), LV_STATE_DISABLED));
    assert(lv_obj_has_state(named(surface, "line_rts"), LV_STATE_DISABLED));
    assert(std::string(lv_label_get_text(named(surface, "receive_log"))).empty());
    save(display, prefix + "-empty.ppm");
    check_controls(surface);

    click(named(surface, "rx_hex"));
    click(named(surface, "tx_hex"));
    assert(vm.rx_hex() && vm.tx_hex());
    click(named(surface, "rx_ascii"));
    click(named(surface, "tx_ascii"));
    assert(!vm.rx_hex() && !vm.tx_hex());
    toggle(named(surface, "show_time"), false);
    toggle(named(surface, "auto_scroll"), false);
    assert(!vm.timestamps() && !vm.autoscroll());
    toggle(named(surface, "show_time"), true);
    toggle(named(surface, "auto_scroll"), true);

    const auto& rates = serial::supported_baud_rates();
    auto baud_index   = [&](unsigned int value) {
        return static_cast<std::uint32_t>(std::find(rates.begin(), rates.end(), value) - rates.begin());
    };
    select(named(surface, "baud_rate"), baud_index(9600));
    select(named(surface, "data_bits"), 2);
    select(named(surface, "parity"), 2);
    select(named(surface, "stop_bits"), 1);
    select(named(surface, "flow_control"), 1);
    assert(vm.settings().baud_rate == 9600 && vm.settings().data_bits == 7);
    assert(vm.settings().parity == serial::Parity::Even && vm.settings().stop_bits == 2 && vm.hardware_flow());
    select(named(surface, "baud_rate"), baud_index(115200));
    select(named(surface, "data_bits"), 3);
    select(named(surface, "parity"), 0);
    select(named(surface, "stop_bits"), 0);
    select(named(surface, "flow_control"), 0);
    click(named(surface, "scan_ports"));
    vm.select_path(terminal.path());
    const auto terminal_index = vm.selected_port();
    vm.select_path("/dev/nonexistent-render-port");
    select(named(surface, "port_select"), static_cast<std::uint32_t>(terminal_index));
    assert(vm.ports()[vm.selected_port()].path == terminal.path());
    reachable(named(surface, "connect_button"), "connect_button");
    click(named(surface, "connect_button"));
    assert(vm.connected());
    assert(std::string(lv_label_get_text(named(surface, "connection_badge"))) == "Connected");
    assert(lv_obj_has_state(named(surface, "baud_rate"), LV_STATE_DISABLED));
    assert(!lv_obj_has_state(named(surface, "send_button"), LV_STATE_DISABLED));

    auto* input    = named(surface, "send_input");
    auto* keyboard = named(surface, "send_keyboard");
    lv_textarea_set_text(input, "Hello, board!");
    select(named(surface, "line_ending"), 1);
    reachable(named(surface, "send_button"), "send_button");
    click(named(surface, "send_button"));
    assert(terminal.read(14) == "Hello, board!\n");
    assert(vm.tx_bytes() == 14);
    receive(display, vm, terminal, "LILYGO ready\r\nVoltage: 3.30 V\r\n");
    auto* log = named(surface, "receive_log");
    assert(std::string(lv_label_get_text(log)).find("LILYGO ready") != std::string::npos);
    click(named(surface, "rx_hex"));
    assert(std::string(lv_label_get_text(log)).find("4C 49 4C 59 47 4F") != std::string::npos);
    click(named(surface, "rx_ascii"));
    click(named(surface, "tx_hex"));
    select(named(surface, "line_ending"), 0);
    lv_textarea_set_text(input, "00 41 FF");
    click(named(surface, "send_button"));
    assert(terminal.read(3) == std::string("\x00\x41\xff", 3));
    assert(vm.tx_bytes() == 17);
    const bool rts_before = vm.line(serial::Line::Rts);
    toggle(named(surface, "line_rts"), !rts_before);
    assert(lv_obj_has_state(named(surface, "line_rts"), LV_STATE_CHECKED) == vm.line(serial::Line::Rts));

    std::string many_lines;
    for (int i = 0; i < 35; ++i) many_lines += "Sensor sample " + std::to_string(i) + ": OK\n";
    receive(display, vm, terminal, many_lines);
    auto* scroll = named(surface, "receive_scroll");
    assert(lv_obj_get_scroll_y(scroll) > 0);
    check_log_bottom(scroll, log);
    assert(std::string(lv_label_get_text(log)).back() != '\n');
    toggle(named(surface, "auto_scroll"), false);
    lv_obj_scroll_to_y(scroll, 0, LV_ANIM_OFF);
    receive(display, vm, terminal, "This line must not move your scroll position.\n");
    assert(lv_obj_get_scroll_y(scroll) == 0);
    toggle(named(surface, "auto_scroll"), true);
    assert(lv_obj_get_scroll_y(scroll) > 0);
    check_log_bottom(scroll, log);
    click(named(surface, "clear_log"));
    assert(vm.rx_bytes() == 0 && vm.tx_bytes() == 0);
    assert(std::string(lv_label_get_text(log)).empty());
    assert(lv_obj_get_scroll_y(scroll) == 0);
    receive(display, vm, terminal, "OK\n");
    assert(lv_obj_get_height(log) <= lv_obj_get_content_height(scroll));
    assert(lv_obj_get_scroll_y(scroll) == 0);
    check_log_bottom(scroll, log);
    click(named(surface, "clear_log"));
    assert(lv_obj_get_scroll_y(scroll) == 0);

    // Produce a readable real-I/O preview, retaining a draft in the composer.
    click(named(surface, "tx_ascii"));
    lv_textarea_set_text(input, "AT+VERSION?");
    select(named(surface, "line_ending"), 3);
    click(named(surface, "send_button"));
    assert(terminal.read(13) == "AT+VERSION?\r\n");
    receive(display, vm, terminal, "LILYGO Serial Studio\r\nFirmware: v1.0.0\r\nStatus: ready\r\n");
    check_log_bottom(scroll, log);
    assert(std::string(lv_label_get_text(log)).back() != '\n');
    reset_scroll(surface);
    if (display_width < 700) lv_obj_scroll_to_view_recursive(named(surface, "receive_card"), LV_ANIM_OFF);
    save(display, output);
    reachable(named(surface, "send_button"), "send_button");
    save(display, prefix + "-send.ppm");
    click(input);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    reachable(input, "send_input with keyboard");
    save(display, prefix + "-keyboard.ppm");
    lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);

    // Resize the existing page rather than reconstructing its widgets.
    const auto original_width  = display_width;
    const auto original_height = display_height;
    click(named(surface, "rx_hex"));
    click(named(surface, "tx_hex"));
    lv_textarea_set_text(input, "41 42 43");
    toggle(named(surface, "auto_scroll"), false);
    display_width  = original_height;
    display_height = original_width;
    framebuffer.assign(static_cast<std::size_t>(display_width) * display_height, {});
    lv_display_set_resolution(display, display_width, display_height);
    frame(display);
    assert(named(surface, "send_input") == input);
    assert(std::string(lv_textarea_get_text(input)) == "41 42 43");
    assert(vm.rx_hex() && vm.tx_hex() && !vm.autoscroll());
    assert(vm.connected() && vm.tx_bytes() == 13);
    check_controls(surface);
    reset_scroll(surface);
    if (display_width < 700) lv_obj_scroll_to_view_recursive(named(surface, "receive_card"), LV_ANIM_OFF);
    save(display, prefix + "-resized.ppm");
    display_width  = original_width;
    display_height = original_height;
    framebuffer.assign(static_cast<std::size_t>(display_width) * display_height, {});
    lv_display_set_resolution(display, display_width, display_height);
    frame(display);
    assert(std::string(lv_textarea_get_text(input)) == "41 42 43");
    click(named(surface, "connect_button"));
    assert(!vm.connected());
    assert(lv_obj_has_state(named(surface, "send_button"), LV_STATE_DISABLED));
    view.destroy();
    view.destroy();
    assert(lv_obj_find_by_name(root, "app_surface") == nullptr);
    vm.set_rx_hex(false);  // Object-bound observer was removed by destruction.
    frame(display);        // Polling timer was also removed.
}

}  // namespace

int main(int argc, char** argv)
{
    assert(argc == 4);
    display_width  = std::atoi(argv[1]);
    display_height = std::atoi(argv[2]);
    assert(display_width >= 320 && display_height >= 240);
    framebuffer.resize(static_cast<std::size_t>(display_width) * display_height);
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(std::max(display_width, display_height)) *
                                          kPartialRows);
    lv_init();
    auto* display = lv_display_create(display_width, display_height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    cm0_app_context_t context{};
    context.root           = lv_display_get_screen_active(display);
    const auto* descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    assert(named(context.root, "app_surface"));
    descriptor->close();
    descriptor->close();
    assert(lv_obj_find_by_name(context.root, "app_surface") == nullptr);

    test_view(display, argv[3]);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
