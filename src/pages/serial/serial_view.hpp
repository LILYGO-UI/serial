#ifndef LILYGO_UI_SERIAL_VIEW_HPP
#define LILYGO_UI_SERIAL_VIEW_HPP

#include "pages/serial/serial_view_model.hpp"
#include <lvgl.h>
#include <string>
#include <vector>

namespace serial {

class View {
public:
    explicit View(ViewModel &view_model) noexcept : view_model_(view_model)
    {
    }
    ~View()
    {
        destroy();
    }
    lv_obj_t *create(lv_obj_t *parent, const char *application_name);
    void destroy() noexcept;

private:
    static void event(lv_event_t *event);
    static void changed(lv_observer_t *observer, lv_subject_t *subject);
    static void tick(lv_timer_t *timer);
    void apply_layout();
    void refresh();
    void show_keyboard(bool visible);
    void read_settings();
    void fit_port_text();

    ViewModel &view_model_;
    lv_timer_t *timer_      = nullptr;
    lv_obj_t *surface_      = nullptr;
    lv_obj_t *header_       = nullptr;
    lv_obj_t *error_banner_ = nullptr;
    lv_obj_t *error_text_   = nullptr;
    lv_obj_t *body_         = nullptr;
    lv_obj_t *sidebar_      = nullptr;
    lv_obj_t *workspace_    = nullptr;
    lv_obj_t *connection_   = nullptr;
    lv_obj_t *receiver_     = nullptr;
    lv_obj_t *composer_     = nullptr;
    lv_obj_t *badge_        = nullptr;
    lv_obj_t *badge_text_   = nullptr;
    lv_obj_t *scan_         = nullptr;
    lv_obj_t *port_         = nullptr;
    lv_obj_t *baud_         = nullptr;
    lv_obj_t *data_         = nullptr;
    lv_obj_t *parity_       = nullptr;
    lv_obj_t *stop_         = nullptr;
    lv_obj_t *flow_         = nullptr;
    lv_obj_t *connect_      = nullptr;
    lv_obj_t *connect_text_ = nullptr;
    lv_obj_t *status_       = nullptr;
    lv_obj_t *rts_          = nullptr;
    lv_obj_t *dtr_          = nullptr;
    lv_obj_t *break_        = nullptr;
    lv_obj_t *rx_ascii_     = nullptr;
    lv_obj_t *rx_hex_       = nullptr;
    lv_obj_t *tx_ascii_     = nullptr;
    lv_obj_t *tx_hex_       = nullptr;
    lv_obj_t *timestamps_   = nullptr;
    lv_obj_t *autoscroll_   = nullptr;
    lv_obj_t *clear_        = nullptr;
    lv_obj_t *log_scroll_   = nullptr;
    lv_obj_t *log_          = nullptr;
    lv_obj_t *empty_        = nullptr;
    lv_obj_t *counters_     = nullptr;
    lv_obj_t *input_        = nullptr;
    lv_obj_t *ending_       = nullptr;
    lv_obj_t *send_         = nullptr;
    lv_obj_t *keyboard_     = nullptr;
    std::vector<unsigned int> baud_rates_;
    std::string port_options_;
    std::string displayed_log_;
    bool wide_       = false;
    bool laying_out_ = false;
};

}  // namespace serial
#endif
