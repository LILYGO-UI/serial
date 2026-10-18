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
    for (auto* item = object; item; item = lv_obj_get_parent(item)) assert(!lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
}

class Pointer {
public:
    explicit Pointer(lv_display_t* display) : display_(display), indev_(lv_indev_create())
    {
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev_, display);
        lv_indev_set_mode(indev_, LV_INDEV_MODE_EVENT);
        lv_indev_set_user_data(indev_, this);
        lv_indev_set_read_cb(indev_, [](lv_indev_t* indev, lv_indev_data_t* data) {
            auto& self  = *static_cast<Pointer*>(lv_indev_get_user_data(indev));
            data->point = self.point_;
            data->state = self.pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        });
    }
    ~Pointer()
    {
        lv_indev_delete(indev_);
    }
    void move(lv_point_t point, bool pressed)
    {
        point_   = point;
        pressed_ = pressed;
        lv_indev_read(indev_);
        frame(display_);
    }
    void click(lv_obj_t* object)
    {
        const auto point = center(object);
        move(point, true);
        move(point, false);
    }
    static lv_point_t center(lv_obj_t* object)
    {
        lv_obj_update_layout(object);
        lv_area_t area{};
        lv_obj_get_coords(object, &area);
        return {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    }

private:
    lv_display_t* display_;
    lv_indev_t* indev_;
    lv_point_t point_{};
    bool pressed_ = false;
};

void select(lv_obj_t* object, std::uint32_t value)
{
    assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
    for (auto* item = object; item; item = lv_obj_get_parent(item)) assert(!lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN));
    lv_dropdown_set_selected(object, value);
    lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
}

void toggle(lv_obj_t* object, bool value)
{
    assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
    for (auto* item = object; item; item = lv_obj_get_parent(item)) assert(!lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN));
    if (value)
        lv_obj_add_state(object, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
}

void visible(lv_obj_t* object, const char* name)
{
    lv_obj_update_layout(object);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    assert(lv_obj_get_width(object) > 0 && lv_obj_get_height(object) >= 30);
    assert(!lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN));
    for (auto* parent = lv_obj_get_parent(object); parent; parent = lv_obj_get_parent(parent)) {
        assert(!lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN));
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

void reachable(lv_obj_t* object, const char* name)
{
    lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
    visible(object, name);
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

void settings(lv_display_t* display, lv_obj_t* surface, bool open)
{
    auto* button = named(surface, "settings_button");
    visible(button, "settings_button");
    assert(lv_obj_has_flag(named(surface, "settings_panel"), LV_OBJ_FLAG_HIDDEN) == open);
    click(button);
    frame(display);
    assert(lv_obj_has_flag(named(surface, "settings_panel"), LV_OBJ_FLAG_HIDDEN) == !open);
    assert(lv_obj_has_flag(named(surface, "workspace"), LV_OBJ_FLAG_HIDDEN) == open);
    assert(std::string(lv_label_get_text(lv_obj_get_child(button, 0))).find(open ? "Back" : "Settings") !=
           std::string::npos);
    visible(button, "settings_button after navigation");
}

void check_settings_controls(lv_obj_t* surface)
{
    assert(!lv_obj_has_flag(named(surface, "settings_panel"), LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(named(surface, "workspace"), LV_OBJ_FLAG_HIDDEN));
    for (const char* name : {"port_select", "baud_rate", "data_bits", "parity", "stop_bits", "flow_control",
                             "scan_ports", "connect_button", "line_rts", "line_dtr", "line_break"}) {
        reachable(named(surface, name), name);
    }
}

void check_workspace(lv_obj_t* surface)
{
    assert(lv_obj_has_flag(named(surface, "settings_panel"), LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(named(surface, "workspace"), LV_OBJ_FLAG_HIDDEN));
    // These assertions deliberately do not scroll: both working areas and their
    // controls must fit on the main screen at the same time.
    for (const char* name :
         {"settings_button", "receive_card", "send_card", "receive_scroll", "send_input", "rx_ascii", "rx_hex",
          "show_time", "auto_scroll", "clear_log", "tx_ascii", "tx_hex", "line_ending", "send_button"}) {
        visible(named(surface, name), name);
    }
    assert(lv_obj_get_scroll_y(named(surface, "workspace")) == 0);
    lv_area_t receiver{}, composer{};
    lv_obj_get_coords(named(surface, "receive_card"), &receiver);
    lv_obj_get_coords(named(surface, "send_card"), &composer);
    assert(receiver.x2 < composer.x1 || receiver.y2 < composer.y1);
    assert(lv_obj_get_content_height(named(surface, "receive_scroll")) >= 60);
    assert(lv_obj_get_content_height(named(surface, "send_input")) >= 40);
}

void resize(lv_display_t* display, int width, int height)
{
    display_width  = width;
    display_height = height;
    framebuffer.assign(static_cast<std::size_t>(display_width) * display_height, {});
    lv_display_set_resolution(display, display_width, display_height);
    frame(display);
}

void check_keyboard(lv_obj_t* surface)
{
    for (const char* name :
         {"receive_card", "receive_scroll", "send_card", "send_input", "send_button", "hide_keyboard", "send_keyboard"})
        visible(named(surface, name), name);
    assert(lv_obj_get_scroll_y(named(surface, "workspace")) == 0);
}

void test_splitter(lv_display_t* display, lv_obj_t* surface, const std::string& prefix)
{
    Pointer pointer(display);
    auto* workspace   = named(surface, "workspace");
    auto* receiver    = named(surface, "receive_card");
    auto* composer    = named(surface, "send_card");
    auto* splitter    = named(surface, "pane_splitter");
    auto* input       = named(surface, "send_input");
    auto* keyboard    = named(surface, "send_keyboard");
    const auto row    = [&] { return lv_obj_get_style_flex_flow(workspace, LV_PART_MAIN) == LV_FLEX_FLOW_ROW; };
    const auto extent = [&](lv_obj_t* object) { return row() ? lv_obj_get_width(object) : lv_obj_get_height(object); };
    const auto drag   = [&](int delta) {
        const auto start = Pointer::center(splitter);
        auto end         = start;
        if (row())
            end.x = std::clamp(end.x + delta, 0, display_width - 1);
        else
            end.y = std::clamp(end.y + delta, 0, display_height - 1);
        pointer.move(start, true);
        pointer.move({(start.x + end.x) / 2, (start.y + end.y) / 2}, true);
        pointer.move(end, true);
        pointer.move(end, false);
    };
    assert(std::abs(extent(receiver) - extent(composer)) <= 1);
    const auto initial_size = extent(receiver);
    drag(60);
    assert(extent(receiver) > initial_size + 30);
    check_workspace(surface);
    save(display, prefix + "-split.ppm");
    const auto receive_size = extent(receiver);
    const auto send_size    = extent(composer);
    const std::string draft = lv_textarea_get_text(input);
    for (int attempt = 0; attempt < 2; ++attempt) {
        pointer.click(input);
        assert(lv_keyboard_get_textarea(keyboard) == input);
        check_keyboard(surface);
        pointer.click(named(surface, "hide_keyboard"));
        assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
        assert(lv_keyboard_get_textarea(keyboard) == nullptr);
        assert(extent(receiver) == receive_size && extent(composer) == send_size);
        assert(std::string(lv_textarea_get_text(input)) == draft);
        check_workspace(surface);
    }
    settings(display, surface, true);
    settings(display, surface, false);
    assert(extent(receiver) == receive_size && extent(composer) == send_size);
    const auto original_width  = display_width;
    const auto original_height = display_height;
    resize(display, original_height, original_width);
    check_workspace(surface);
    resize(display, original_width, original_height);
    assert(extent(receiver) == receive_size && extent(composer) == send_size);
    // Drag past both limits: neither pane or its controls may disappear.
    drag(10000);
    check_workspace(surface);
    drag(-10000);
    check_workspace(surface);
    drag((extent(receiver) + extent(composer)) / 2 - extent(receiver));
    assert(std::abs(extent(receiver) - extent(composer)) <= 1);
    check_workspace(surface);
}

void check_log_bottom(lv_obj_t* scroll, lv_obj_t* log)
{
    lv_obj_update_layout(scroll);
    lv_area_t content{}, label{};
    lv_obj_get_content_coords(scroll, &content);
    lv_obj_get_coords(log, &label);
    const auto visible_height  = content.y2 - content.y1 + 1;
    const auto expected_scroll = std::max(0, lv_obj_get_height(log) - visible_height);
    assert(lv_obj_get_scroll_y(scroll) == expected_scroll);
    if (expected_scroll > 0)
        assert(label.y2 == content.y2);
    else
        assert(label.y1 == content.y1);
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
    assert(std::string(lv_label_get_text(lv_obj_get_child(named(surface, "settings_button"), 0))).find("Settings") !=
           std::string::npos);
    check_workspace(surface);
    const auto* workspace = named(surface, "workspace");
    const bool horizontal = lv_obj_get_style_flex_flow(workspace, LV_PART_MAIN) == LV_FLEX_FLOW_ROW;
    assert(std::abs(horizontal ? lv_obj_get_width(named(surface, "receive_card")) -
                                     lv_obj_get_width(named(surface, "send_card"))
                               : lv_obj_get_height(named(surface, "receive_card")) -
                                     lv_obj_get_height(named(surface, "send_card"))) <= 1);
    save(display, prefix + "-empty.ppm");

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
    settings(display, surface, true);
    check_settings_controls(surface);
    select(named(surface, "baud_rate"), baud_index(9600));
    select(named(surface, "data_bits"), 2);
    select(named(surface, "parity"), 2);
    select(named(surface, "stop_bits"), 1);
    select(named(surface, "flow_control"), 1);
    assert(vm.settings().baud_rate == 9600 && vm.settings().data_bits == 7);
    assert(vm.settings().parity == serial::Parity::Even && vm.settings().stop_bits == 2 && vm.hardware_flow());
    auto* baud = named(surface, "baud_rate");
    lv_dropdown_open(baud);
    assert(lv_dropdown_is_open(baud));
    settings(display, surface, false);
    assert(!lv_dropdown_is_open(baud));
    check_workspace(surface);
    settings(display, surface, true);
    assert(named(surface, "baud_rate") == baud);
    assert(lv_dropdown_get_selected(baud) == baud_index(9600));
    assert(vm.settings().baud_rate == 9600 && vm.settings().data_bits == 7 && vm.hardware_flow());
    select(named(surface, "baud_rate"), baud_index(115200));
    select(named(surface, "data_bits"), 3);
    select(named(surface, "parity"), 0);
    select(named(surface, "stop_bits"), 0);
    select(named(surface, "flow_control"), 0);
    click(named(surface, "scan_ports"));

    // A long unavailable path must leave the settings usable and show a
    // readable error, including after returning to the working areas.
    vm.select_path("/dev/serial/by-id/usb-LILYGO_Unavailable_Render_Test_Device_0123456789-if00-port0");
    reachable(named(surface, "connect_button"), "connect_button before error");
    click(named(surface, "connect_button"));
    assert(!vm.connected() && vm.error());
    assert(!lv_obj_has_flag(named(surface, "error_banner"), LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_label_get_text(named(surface, "connection_status"))) == vm.status());
    check_settings_controls(surface);
    reset_scroll(surface);
    save(display, prefix + "-settings-error.ppm");
    settings(display, surface, false);
    assert(!lv_obj_has_flag(named(surface, "error_banner"), LV_OBJ_FLAG_HIDDEN));
    visible(named(surface, "settings_button"), "settings_button with error");
    click(named(surface, "send_input"));
    frame(display);
    check_keyboard(surface);
    save(display, prefix + "-keyboard-error.ppm");
    click(named(surface, "hide_keyboard"));
    frame(display);
    check_workspace(surface);
    settings(display, surface, true);

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
    reset_scroll(surface);
    save(display, prefix + "-settings.ppm");
    settings(display, surface, false);
    check_workspace(surface);

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

    std::string many_lines;
    for (int i = 0; i < 35; ++i) many_lines += "Sensor sample " + std::to_string(i) + ": OK\n";
    receive(display, vm, terminal, many_lines);
    auto* scroll = named(surface, "receive_scroll");
    assert(lv_obj_get_scroll_y(scroll) > 0);
    check_log_bottom(scroll, log);
    assert(std::string(lv_label_get_text(log)).back() != '\n');
    // Keyboard layout changes must retain the latest received line even when
    // no new serial data arrives to trigger a ViewModel update.
    click(input);
    frame(display);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    if (!lv_obj_has_flag(named(surface, "receive_card"), LV_OBJ_FLAG_HIDDEN)) check_log_bottom(scroll, log);
    lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
    frame(display);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);
    check_log_bottom(scroll, log);
    check_workspace(surface);
    toggle(named(surface, "auto_scroll"), false);
    lv_obj_scroll_to_y(scroll, 0, LV_ANIM_OFF);
    receive(display, vm, terminal, "This line must not move your scroll position.\n");
    assert(lv_obj_get_scroll_y(scroll) == 0);
    lv_obj_scroll_to_y(scroll, 60, LV_ANIM_OFF);
    const auto saved_scroll = lv_obj_get_scroll_y(scroll);
    assert(saved_scroll > 0);
    click(input);
    frame(display);
    if (!lv_obj_has_flag(named(surface, "receive_card"), LV_OBJ_FLAG_HIDDEN))
        assert(lv_obj_get_scroll_y(scroll) == saved_scroll);
    lv_obj_send_event(keyboard, LV_EVENT_CANCEL, nullptr);
    frame(display);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);
    assert(lv_obj_get_scroll_y(scroll) == saved_scroll);
    assert(!vm.autoscroll());
    check_workspace(surface);
    const auto saved_rx     = vm.rx_bytes();
    const std::string draft = "A draft survives settings navigation\nand background serial traffic.";
    lv_textarea_set_text(input, draft.c_str());
    settings(display, surface, true);
    receive(display, vm, terminal, "Received while settings are open.\n");
    assert(vm.connected() && vm.rx_bytes() > saved_rx && vm.tx_bytes() == 17);
    settings(display, surface, false);
    assert(named(surface, "send_input") == input && named(surface, "receive_scroll") == scroll);
    assert(std::string(lv_textarea_get_text(input)) == draft);
    assert(std::string(lv_label_get_text(log)).find("Received while settings are open.") != std::string::npos);
    assert(lv_obj_get_scroll_y(scroll) == saved_scroll);
    assert(!vm.autoscroll() && !vm.rx_hex() && vm.tx_hex());
    check_workspace(surface);
    toggle(named(surface, "auto_scroll"), true);
    assert(lv_obj_get_scroll_y(scroll) > 0);
    check_log_bottom(scroll, log);
    settings(display, surface, true);
    receive(display, vm, terminal, "Autoscroll also catches up after settings.\n");
    settings(display, surface, false);
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
    check_workspace(surface);
    save(display, output);
    visible(named(surface, "send_button"), "send_button");
    save(display, prefix + "-send.ppm");
    click(input);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    visible(input, "send_input with keyboard");
    visible(named(surface, "send_button"), "send_button with keyboard");
    check_keyboard(surface);
    save(display, prefix + "-keyboard.ppm");
    const auto keyboard_width  = display_width;
    const auto keyboard_height = display_height;
    resize(display, keyboard_height, keyboard_width);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(std::string(lv_textarea_get_text(input)) == "AT+VERSION?");
    visible(input, "send_input with keyboard after rotation");
    visible(named(surface, "send_button"), "send_button with keyboard after rotation");
    check_keyboard(surface);
    resize(display, keyboard_width, keyboard_height);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(std::string(lv_textarea_get_text(input)) == "AT+VERSION?");
    visible(input, "send_input with keyboard after restoring size");
    visible(named(surface, "send_button"), "send_button with keyboard after restoring size");
    check_keyboard(surface);
    lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
    frame(display);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);
    check_workspace(surface);
    click(input);
    lv_obj_send_event(keyboard, LV_EVENT_CANCEL, nullptr);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);
    click(input);
    settings(display, surface, true);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == nullptr);
    settings(display, surface, false);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_textarea_get_text(input)) == "AT+VERSION?");
    auto* ending = named(surface, "line_ending");
    lv_dropdown_open(ending);
    assert(lv_dropdown_is_open(ending));
    settings(display, surface, true);
    assert(!lv_dropdown_is_open(ending));
    settings(display, surface, false);
    check_workspace(surface);

    // Resize the existing page rather than reconstructing its widgets.
    const auto original_width          = display_width;
    const auto original_height         = display_height;
    const auto original_receive_width  = lv_obj_get_width(scroll);
    const auto original_receive_height = lv_obj_get_height(scroll);
    const auto original_input_width    = lv_obj_get_width(input);
    const auto original_input_height   = lv_obj_get_height(input);
    resize(display, original_width + 160, original_height + 160);
    check_workspace(surface);
    assert(lv_obj_get_width(scroll) >= original_receive_width);
    assert(lv_obj_get_height(scroll) >= original_receive_height);
    assert(lv_obj_get_width(scroll) > original_receive_width || lv_obj_get_height(scroll) > original_receive_height);
    assert(lv_obj_get_width(input) >= original_input_width);
    assert(lv_obj_get_height(input) >= original_input_height);
    assert(lv_obj_get_width(input) > original_input_width || lv_obj_get_height(input) > original_input_height);
    resize(display, original_width, original_height);
    click(named(surface, "rx_hex"));
    click(named(surface, "tx_hex"));
    lv_textarea_set_text(input, "41 42 43");
    toggle(named(surface, "auto_scroll"), false);
    resize(display, original_height, original_width);
    assert(named(surface, "send_input") == input);
    assert(std::string(lv_textarea_get_text(input)) == "41 42 43");
    assert(vm.rx_hex() && vm.tx_hex() && !vm.autoscroll());
    assert(vm.connected() && vm.tx_bytes() == 13);
    check_workspace(surface);
    reset_scroll(surface);
    save(display, prefix + "-resized.ppm");
    settings(display, surface, true);
    check_settings_controls(surface);
    resize(display, original_width, original_height);
    check_settings_controls(surface);
    assert(vm.connected() && vm.rx_hex() && vm.tx_hex() && !vm.autoscroll());
    assert(std::string(lv_textarea_get_text(input)) == "41 42 43");
    resize(display, original_height, original_width);
    check_settings_controls(surface);
    reset_scroll(surface);
    save(display, prefix + "-settings-resized.ppm");
    settings(display, surface, false);
    check_workspace(surface);
    resize(display, original_width, original_height);
    assert(std::string(lv_textarea_get_text(input)) == "41 42 43");
    check_workspace(surface);
    settings(display, surface, true);
    const bool rts_before = vm.line(serial::Line::Rts);
    reachable(named(surface, "line_rts"), "line_rts");
    toggle(named(surface, "line_rts"), !rts_before);
    assert(lv_obj_has_state(named(surface, "line_rts"), LV_STATE_CHECKED) == vm.line(serial::Line::Rts));
    reachable(named(surface, "connect_button"), "disconnect in settings");
    click(named(surface, "connect_button"));
    assert(!vm.connected());
    assert(lv_obj_has_state(named(surface, "send_button"), LV_STATE_DISABLED));
    settings(display, surface, false);
    check_workspace(surface);
    test_splitter(display, surface, prefix);
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
