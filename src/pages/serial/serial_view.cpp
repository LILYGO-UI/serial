#include "pages/serial/serial_view.hpp"

#include "components/components.hpp"
#include <cm0/typography.h>
#include <algorithm>
#include <cstdio>

namespace serial {
namespace {
using namespace components;

void font(lv_obj_t *object, int size, lv_style_selector_t selector = 0)
{
    if (const auto *value = lilygo_ui_font_get(size)) lv_obj_set_style_text_font(object, value, selector);
}

lv_obj_t *box(lv_obj_t *parent, lv_flex_flow_t flow, int gap = 12)
{
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(object, flow);
    lv_obj_set_style_pad_gap(object, gap, 0);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    font(object, 22);
    lv_obj_set_style_text_color(object, lv_color_hex(color_text), 0);
    return object;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int size = 22, std::uint32_t color = color_text)
{
    auto *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    font(object, size);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

lv_obj_t *card(lv_obj_t *parent, const char *name)
{
    auto *object = box(parent, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_set_name_static(object, name);
    lv_obj_set_style_bg_color(object, lv_color_hex(color_container_background), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 20, 0);
    lv_obj_set_style_pad_all(object, 18, 0);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return object;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, const char *name, lv_event_cb_t callback, void *data,
                 bool primary = false)
{
    auto *object = lv_button_create(parent);
    lv_obj_set_name_static(object, name);
    lv_obj_set_size(object, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_radius(object, 12, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_hor(object, 16, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(primary ? color_action : color_accent_background), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
    auto *text_object = label(object, text, 22, primary ? color_container_background : color_accent);
    lv_obj_center(text_object);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, data);
    return object;
}

lv_obj_t *dropdown(lv_obj_t *parent, const char *options, const char *name, lv_event_cb_t callback, void *data)
{
    auto *object = lv_dropdown_create(parent);
    lv_obj_set_name_static(object, name);
    lv_obj_set_size(object, LV_PCT(100), 44);
    lv_dropdown_set_options(object, options);
    font(object, 22);
    lv_obj_set_style_radius(object, 10, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_hor(object, 12, 0);
    lv_obj_set_style_pad_ver(object, 7, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color_control), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color_text), 0);
    lv_obj_set_style_opa(object, LV_OPA_50, LV_STATE_DISABLED);
    auto *list = lv_dropdown_get_list(object);
    font(list, 22);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_text_color(list, lv_color_hex(color_text), 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(color_container_background), 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(color_accent), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_hex(color_container_background), LV_PART_SELECTED);
    lv_obj_add_event_cb(object, callback, LV_EVENT_VALUE_CHANGED, data);
    return object;
}

lv_obj_t *field(lv_obj_t *parent, const char *title, const char *options, const char *name, lv_event_cb_t callback,
                void *data)
{
    auto *container = box(parent, LV_FLEX_FLOW_COLUMN, 5);
    lv_obj_set_width(container, 0);
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    label(container, title, 14, color_secondary);
    return dropdown(container, options, name, callback, data);
}

lv_obj_t *toggle(lv_obj_t *parent, const char *name, lv_event_cb_t callback, void *data)
{
    auto *object = lv_switch_create(parent);
    lv_obj_set_name_static(object, name);
    lv_obj_set_size(object, 50, 30);
    lv_obj_set_style_bg_color(object, lv_color_hex(color_border), LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, lv_color_hex(color_success), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_pad_all(object, -3, LV_PART_KNOB);
    lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
    // Extend the touch target without making the visual switch oversized.
    lv_obj_set_ext_click_area(object, 7);
    lv_obj_add_event_cb(object, callback, LV_EVENT_VALUE_CHANGED, data);
    return object;
}

void checked(lv_obj_t *object, bool value)
{
    if (value)
        lv_obj_add_state(object, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(object, LV_STATE_CHECKED);
}

void disabled(lv_obj_t *object, bool value)
{
    if (value)
        lv_obj_add_state(object, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(object, LV_STATE_DISABLED);
}

void segment_style(lv_obj_t *object, bool selected)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(selected ? color_container_background : color_control), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(object, 0), lv_color_hex(selected ? color_accent : color_secondary),
                                0);
}

void segments(lv_obj_t *parent, lv_obj_t *&ascii, lv_obj_t *&hex, const char *ascii_name, const char *hex_name,
              lv_event_cb_t callback, void *data)
{
    auto *group = box(parent, LV_FLEX_FLOW_ROW, 2);
    lv_obj_set_size(group, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_bg_color(group, lv_color_hex(color_control), 0);
    lv_obj_set_style_bg_opa(group, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(group, 12, 0);
    lv_obj_set_style_pad_all(group, 3, 0);
    ascii = button(group, "ASCII", ascii_name, callback, data);
    hex   = button(group, "HEX", hex_name, callback, data);
    for (auto *item : {ascii, hex}) {
        lv_obj_set_height(item, 38);
        lv_obj_set_style_pad_hor(item, 12, 0);
        lv_obj_set_style_radius(item, 9, 0);
        font(lv_obj_get_child(item, 0), 14);
    }
}
}  // namespace

lv_obj_t *View::create(lv_obj_t *parent, const char *application_name)
{
    if (surface_) return surface_;
    surface_ = components::create_page(parent);
    font(surface_, 22);
    lv_obj_set_style_pad_gap(surface_, 0, 0);
    lv_obj_add_event_cb(surface_, event, LV_EVENT_SIZE_CHANGED, this);

    header_ = box(surface_, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_name_static(header_, "app_header");
    lv_obj_set_height(header_, 82);
    lv_obj_set_style_pad_hor(header_, 24, 0);
    auto *titles = box(header_, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_width(titles, 0);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_flex_align(titles, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto *title = label(titles, application_name, 36, color_container_title);
    lv_obj_set_name_static(title, "app_title");
    label(titles, "SERIAL CONSOLE", 14, color_secondary);
    badge_ = box(header_, LV_FLEX_FLOW_ROW, 6);
    lv_obj_set_size(badge_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(badge_, 12, 0);
    lv_obj_set_style_pad_ver(badge_, 7, 0);
    lv_obj_set_style_radius(badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge_, lv_color_hex(color_container_background), 0);
    lv_obj_set_style_bg_opa(badge_, LV_OPA_COVER, 0);
    badge_text_ = label(badge_, "Offline", 14, color_secondary);
    lv_obj_set_name_static(badge_text_, "connection_badge");

    error_banner_ = box(surface_, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_name_static(error_banner_, "error_banner");
    lv_obj_set_style_pad_hor(error_banner_, 20, 0);
    lv_obj_set_style_pad_bottom(error_banner_, 10, 0);
    error_text_ = label(error_banner_, "", 14, color_error);
    lv_obj_set_width(error_text_, LV_PCT(100));
    lv_obj_add_flag(error_banner_, LV_OBJ_FLAG_HIDDEN);

    body_ = box(surface_, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_set_height(body_, 0);
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_set_style_pad_hor(body_, 20, 0);
    lv_obj_set_style_pad_bottom(body_, 20, 0);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    sidebar_ = box(body_, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_name_static(sidebar_, "settings_panel");
    connection_      = card(sidebar_, "connection_card");
    auto *port_row   = box(connection_, LV_FLEX_FLOW_ROW, 8);
    auto *port_label = label(port_row, "Connection", 22, color_container_title);
    lv_obj_set_flex_grow(port_label, 1);
    scan_ = button(port_row, LV_SYMBOL_REFRESH " Scan", "scan_ports", event, this);
    lv_obj_set_height(scan_, 34);
    font(lv_obj_get_child(scan_, 0), 14);
    port_ = dropdown(connection_, "No ports found", "port_select", event, this);

    baud_rates_ = supported_baud_rates();
    std::string baud_options;
    for (auto value : baud_rates_) {
        if (!baud_options.empty()) baud_options += '\n';
        baud_options += std::to_string(value);
    }
    auto *row = box(connection_, LV_FLEX_FLOW_ROW, 12);
    baud_     = field(row, "Baud rate", baud_options.c_str(), "baud_rate", event, this);
    data_     = field(row, "Data bits", "5\n6\n7\n8", "data_bits", event, this);
    row       = box(connection_, LV_FLEX_FLOW_ROW, 12);
    parity_   = field(row, "Parity", "None\nOdd\nEven", "parity", event, this);
    stop_     = field(row, "Stop bits", "1\n2", "stop_bits", event, this);
    row       = box(connection_, LV_FLEX_FLOW_ROW, 12);
    flow_     = field(row, "Flow control", "None\nRTS/CTS\nXON/XOFF", "flow_control", event, this);
    connect_  = button(connection_, "Connect", "connect_button", event, this, true);
    lv_obj_set_width(connect_, LV_PCT(100));
    connect_text_ = lv_obj_get_child(connect_, 0);
    status_       = label(connection_, "Select a port to begin.", 14, color_secondary);
    lv_obj_set_width(status_, LV_PCT(100));
    lv_obj_set_name_static(status_, "connection_status");

    auto *lines      = card(sidebar_, "line_card");
    auto *line_title = box(lines, LV_FLEX_FLOW_ROW, 8);
    auto *lt         = label(line_title, "Line control", 22, color_container_title);
    lv_obj_set_flex_grow(lt, 1);
    label(line_title, "OUTPUT", 14, color_secondary);
    row            = box(lines, LV_FLEX_FLOW_ROW, 8);
    auto make_line = [&](const char *text, const char *name) {
        auto *cell = box(row, LV_FLEX_FLOW_COLUMN, 8);
        lv_obj_set_width(cell, 0);
        lv_obj_set_flex_grow(cell, 1);
        label(cell, text, 14, color_secondary);
        return toggle(cell, name, event, this);
    };
    rts_   = make_line("RTS", "line_rts");
    dtr_   = make_line("DTR", "line_dtr");
    break_ = make_line("BREAK", "line_break");

    workspace_ = box(body_, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_name_static(workspace_, "workspace");
    receiver_      = card(workspace_, "receive_card");
    row            = box(receiver_, LV_FLEX_FLOW_ROW, 8);
    auto *rx_title = label(row, "Receive", 22, color_container_title);
    lv_obj_set_flex_grow(rx_title, 1);
    segments(row, rx_ascii_, rx_hex_, "rx_ascii", "rx_hex", event, this);
    auto *options    = box(receiver_, LV_FLEX_FLOW_ROW_WRAP, 12);
    auto make_option = [&](const char *text, const char *name) {
        auto *pair = box(options, LV_FLEX_FLOW_ROW, 8);
        lv_obj_set_width(pair, LV_SIZE_CONTENT);
        label(pair, text, 14, color_secondary);
        return toggle(pair, name, event, this);
    };
    timestamps_ = make_option("Time", "show_time");
    autoscroll_ = make_option("Auto-scroll", "auto_scroll");
    log_scroll_ = box(receiver_, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_name_static(log_scroll_, "receive_scroll");
    lv_obj_set_height(log_scroll_, 0);
    lv_obj_set_flex_grow(log_scroll_, 1);
    lv_obj_set_style_bg_color(log_scroll_, lv_color_hex(color_page_background), 0);
    lv_obj_set_style_bg_opa(log_scroll_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(log_scroll_, 12, 0);
    lv_obj_set_style_pad_all(log_scroll_, 14, 0);
    lv_obj_add_flag(log_scroll_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(log_scroll_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(log_scroll_, LV_SCROLLBAR_MODE_AUTO);
    log_ = label(log_scroll_, "", 14, color_text);
    lv_obj_set_name_static(log_, "receive_log");
    lv_obj_set_width(log_, LV_PCT(100));
    empty_ = box(log_scroll_, LV_FLEX_FLOW_COLUMN, 8);
    lv_obj_set_style_pad_ver(empty_, 4, 0);
    auto *hint = label(empty_, "Connect a port to see incoming data.", 14, color_secondary);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    row       = box(receiver_, LV_FLEX_FLOW_ROW, 8);
    counters_ = label(row, "RX 0 B    TX 0 B", 14, color_secondary);
    lv_obj_set_name_static(counters_, "byte_counters");
    lv_obj_set_width(counters_, 0);
    lv_obj_set_flex_grow(counters_, 1);
    clear_ = button(row, "Clear", "clear_log", event, this);
    font(lv_obj_get_child(clear_, 0), 14);
    lv_obj_set_height(clear_, 34);

    composer_      = card(workspace_, "send_card");
    row            = box(composer_, LV_FLEX_FLOW_ROW, 8);
    auto *tx_title = label(row, "Send", 22, color_container_title);
    lv_obj_set_flex_grow(tx_title, 1);
    segments(row, tx_ascii_, tx_hex_, "tx_ascii", "tx_hex", event, this);
    input_ = lv_textarea_create(composer_);
    lv_obj_set_name_static(input_, "send_input");
    lv_obj_set_size(input_, LV_PCT(100), 76);
    lv_textarea_set_max_length(input_, 16384);
    lv_textarea_set_placeholder_text(input_, "Type an ASCII message...");
    font(input_, 22);
    lv_obj_set_style_radius(input_, 12, 0);
    lv_obj_set_style_border_width(input_, 0, 0);
    lv_obj_set_style_bg_color(input_, lv_color_hex(color_control), 0);
    lv_obj_set_style_pad_all(input_, 10, 0);
    lv_obj_add_event_cb(input_, event, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(input_, event, LV_EVENT_CLICKED, this);
    row     = box(composer_, LV_FLEX_FLOW_ROW, 10);
    ending_ = dropdown(row, "No ending\nLF (\\n)\nCR (\\r)\nCRLF", "line_ending", event, this);
    lv_obj_set_width(ending_, 0);
    lv_obj_set_flex_grow(ending_, 1);
    send_ = button(row, "Send " LV_SYMBOL_UP, "send_button", event, this, true);

    keyboard_ = lv_keyboard_create(surface_);
    lv_obj_set_name_static(keyboard_, "send_keyboard");
    font(keyboard_, 22);
    font(keyboard_, 22, LV_PART_ITEMS);
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard_, event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, event, LV_EVENT_CANCEL, this);
    lv_subject_add_observer_obj(view_model_.revision_subject(), changed, surface_, this);
    timer_ = lv_timer_create(tick, 20, this);
    apply_layout();
    refresh();
    return surface_;
}

void View::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    if (surface_) lv_obj_delete(surface_);
    surface_ = header_ = body_ = sidebar_ = workspace_ = connection_ = receiver_ = composer_ = nullptr;
    error_banner_ = error_text_ = nullptr;
    badge_ = badge_text_ = scan_ = port_ = baud_ = data_ = parity_ = stop_ = flow_ = nullptr;
    connect_ = connect_text_ = status_ = rts_ = dtr_ = break_ = nullptr;
    rx_ascii_ = rx_hex_ = tx_ascii_ = tx_hex_ = timestamps_ = autoscroll_ = clear_ = nullptr;
    log_scroll_ = log_ = empty_ = counters_ = input_ = ending_ = send_ = keyboard_ = nullptr;
    port_options_.clear();
    displayed_log_.clear();
}

void View::changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<View *>(lv_observer_get_user_data(observer))->refresh();
}

void View::tick(lv_timer_t *timer)
{
    static_cast<View *>(lv_timer_get_user_data(timer))->view_model_.poll();
}

void View::event(lv_event_t *e)
{
    auto &self      = *static_cast<View *>(lv_event_get_user_data(e));
    auto *target    = lv_event_get_target_obj(e);
    const auto code = lv_event_get_code(e);
    auto &vm        = self.view_model_;
    if (target == self.surface_ && code == LV_EVENT_SIZE_CHANGED) {
        self.apply_layout();
        return;
    }
    if (target == self.keyboard_) {
        self.show_keyboard(false);
        return;
    }
    if (target == self.input_) {
        self.show_keyboard(true);
        return;
    }
    if (code == LV_EVENT_CLICKED) {
        if (target == self.scan_)
            vm.scan_ports();
        else if (target == self.connect_)
            vm.toggle_connection();
        else if (target == self.clear_)
            vm.clear();
        else if (target == self.send_)
            vm.send(lv_textarea_get_text(self.input_));
        else if (target == self.rx_ascii_)
            vm.set_rx_hex(false);
        else if (target == self.rx_hex_)
            vm.set_rx_hex(true);
        else if (target == self.tx_ascii_)
            vm.set_tx_hex(false);
        else if (target == self.tx_hex_)
            vm.set_tx_hex(true);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        const bool value = lv_obj_has_state(target, LV_STATE_CHECKED);
        if (target == self.port_)
            vm.select_port(lv_dropdown_get_selected(target));
        else if (target == self.timestamps_)
            vm.set_timestamps(value);
        else if (target == self.autoscroll_)
            vm.set_autoscroll(value);
        else if (target == self.rts_)
            vm.set_line(Line::Rts, value);
        else if (target == self.dtr_)
            vm.set_line(Line::Dtr, value);
        else if (target == self.break_)
            vm.set_line(Line::Break, value);
        else if (target == self.ending_)
            vm.set_line_ending(static_cast<EndOfLine>(lv_dropdown_get_selected(target)));
        else
            self.read_settings();
    }
}

void View::read_settings()
{
    Settings settings;
    settings.baud_rate    = baud_rates_.at(lv_dropdown_get_selected(baud_));
    settings.data_bits    = 5 + lv_dropdown_get_selected(data_);
    settings.stop_bits    = 1 + lv_dropdown_get_selected(stop_);
    settings.parity       = static_cast<Parity>(lv_dropdown_get_selected(parity_));
    settings.flow_control = static_cast<FlowControl>(lv_dropdown_get_selected(flow_));
    view_model_.set_settings(settings);
}

void View::refresh()
{
    if (!surface_ || !send_) return;
    const bool connected = view_model_.connected();
    std::string options;
    for (const auto &port : view_model_.ports()) {
        if (!options.empty()) options += '\n';
        options += port.path;
    }
    if (options.empty()) options = "No ports found";
    if (options != port_options_) {
        port_options_ = options;
        lv_dropdown_set_options(port_, options.c_str());
    }
    if (!view_model_.ports().empty()) lv_dropdown_set_selected(port_, view_model_.selected_port());
    fit_port_text();
    const auto &settings = view_model_.settings();
    const auto baud      = std::find(baud_rates_.begin(), baud_rates_.end(), settings.baud_rate);
    if (baud != baud_rates_.end()) lv_dropdown_set_selected(baud_, baud - baud_rates_.begin());
    lv_dropdown_set_selected(data_, settings.data_bits - 5);
    lv_dropdown_set_selected(stop_, settings.stop_bits - 1);
    lv_dropdown_set_selected(parity_, static_cast<unsigned>(settings.parity));
    lv_dropdown_set_selected(flow_, static_cast<unsigned>(settings.flow_control));
    lv_dropdown_set_selected(ending_, static_cast<unsigned>(view_model_.line_ending()));
    for (auto *object : {port_, baud_, data_, stop_, parity_, flow_}) disabled(object, connected);
    disabled(port_, connected || view_model_.ports().empty());
    disabled(connect_, !connected && view_model_.ports().empty());
    disabled(send_, !connected);
    disabled(rts_, !connected || view_model_.hardware_flow());
    disabled(dtr_, !connected);
    disabled(break_, !connected);
    checked(rts_, view_model_.line(Line::Rts));
    checked(dtr_, view_model_.line(Line::Dtr));
    checked(break_, view_model_.line(Line::Break));
    checked(timestamps_, view_model_.timestamps());
    checked(autoscroll_, view_model_.autoscroll());
    segment_style(rx_ascii_, !view_model_.rx_hex());
    segment_style(rx_hex_, view_model_.rx_hex());
    segment_style(tx_ascii_, !view_model_.tx_hex());
    segment_style(tx_hex_, view_model_.tx_hex());
    lv_label_set_text(connect_text_, connected ? "Disconnect" : "Connect");
    lv_label_set_text(badge_text_, connected ? "Connected" : "Offline");
    lv_obj_set_style_text_color(badge_text_, lv_color_hex(connected ? color_success : color_secondary), 0);
    lv_label_set_text(status_, view_model_.status().c_str());
    lv_obj_set_style_text_color(status_, lv_color_hex(view_model_.error() ? color_error : color_secondary), 0);
    if (view_model_.error()) {
        lv_label_set_text(error_text_, view_model_.status().c_str());
        lv_obj_remove_flag(error_banner_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(error_banner_, LV_OBJ_FLAG_HIDDEN);
    }
    char counters[96];
    std::snprintf(counters, sizeof(counters), "RX %llu B    TX %llu B",
                  static_cast<unsigned long long>(view_model_.rx_bytes()),
                  static_cast<unsigned long long>(view_model_.tx_bytes()));
    lv_label_set_text(counters_, counters);
    lv_textarea_set_placeholder_text(input_, view_model_.tx_hex() ? "48 65 6C 6C 6F" : "Type an ASCII message...");
    const auto scroll = lv_obj_get_scroll_y(log_scroll_);
    auto text = view_model_.log_text();
    if (text != displayed_log_) {
        displayed_log_    = std::move(text);
        lv_label_set_text(log_, displayed_log_.c_str());
    }
    if (displayed_log_.empty()) {
        lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(log_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(log_, LV_OBJ_FLAG_HIDDEN);
    }
    // Measure only the visible content before bounding the scroll destination.
    lv_obj_update_layout(log_scroll_);
    if (displayed_log_.empty()) {
        lv_obj_scroll_to_y(log_scroll_, 0, LV_ANIM_OFF);
    } else if (view_model_.autoscroll()) {
        lv_obj_scroll_to_y(log_scroll_, LV_COORD_MAX, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(log_scroll_, scroll, LV_ANIM_OFF);
    }
}

void View::fit_port_text()
{
    std::string text = "No ports found";
    if (view_model_.selected_port() < view_model_.ports().size()) {
        text = view_model_.ports()[view_model_.selected_port()].path;
    }
    const auto *text_font = lilygo_ui_font_get(22);
    const auto available  = lv_obj_get_content_width(port_) - 32;
    const auto text_width = [text_font](const std::string &value) {
        lv_point_t size{};
        lv_text_get_size(&size, value.c_str(), text_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        return size.x;
    };
    if (text_font && available > 40 && text_width(text) > available) {
        do {
            auto last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
            text.resize(last);
        } while (!text.empty() && text_width(text + "...") > available);
        text += "...";
    }
    lv_dropdown_set_text(port_, text.c_str());
}

void View::show_keyboard(bool visible)
{
    if (visible) {
        lv_keyboard_set_textarea(keyboard_, input_);
        lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(surface_);
    if (visible) lv_obj_scroll_to_view_recursive(input_, LV_ANIM_OFF);
}

void View::apply_layout()
{
    if (!body_ || !keyboard_ || laying_out_) return;
    laying_out_       = true;
    const auto width  = lv_obj_get_content_width(surface_);
    const auto height = lv_obj_get_content_height(surface_);
    wide_             = width >= 900 || (width >= 700 && height < 600);
    lv_obj_set_height(header_, height < 600 ? 70 : 82);
    lv_obj_set_style_pad_hor(header_, width < 450 ? 16 : 24, 0);
    lv_obj_set_style_pad_hor(body_, width < 450 ? 12 : 20, 0);
    lv_obj_set_style_pad_gap(body_, width < 450 ? 12 : 16, 0);
    lv_obj_set_flex_flow(body_, wide_ ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(sidebar_, wide_ ? (width < 1000 ? 300 : 334) : LV_PCT(100));
    lv_obj_set_height(sidebar_, wide_ ? LV_PCT(100) : LV_SIZE_CONTENT);
    if (wide_)
        lv_obj_add_flag(sidebar_, LV_OBJ_FLAG_SCROLLABLE);
    else
        lv_obj_remove_flag(sidebar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sidebar_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sidebar_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_width(workspace_, wide_ ? 0 : LV_PCT(100));
    lv_obj_set_height(workspace_, wide_ ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(workspace_, wide_ ? 1 : 0);
    if (wide_)
        lv_obj_add_flag(workspace_, LV_OBJ_FLAG_SCROLLABLE);
    else
        lv_obj_remove_flag(workspace_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(workspace_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(workspace_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_height(receiver_, wide_ ? 0 : 300);
    lv_obj_set_style_min_height(receiver_, wide_ ? 240 : 260, 0);
    lv_obj_set_flex_grow(receiver_, wide_ ? 1 : 0);
    lv_obj_set_style_pad_all(composer_, wide_ ? 14 : 18, 0);
    lv_obj_set_style_pad_gap(composer_, wide_ ? 8 : 12, 0);
    lv_obj_set_height(input_, wide_ ? 60 : 76);
    lv_obj_set_height(keyboard_, height < 600 ? 180 : 240);
    lv_obj_update_layout(surface_);
    fit_port_text();
    laying_out_ = false;
}

}  // namespace serial
