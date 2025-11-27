#include "alarm_manager.h"
#include "application.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <cJSON.h>
#include <ctime>
#include <algorithm>
#include <cctype>

#define TAG "AlarmManager"

using namespace Lang;

AlarmManager::AlarmManager() {
    // Timer kiểm tra báo thức mỗi phút
    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            static_cast<AlarmManager*>(arg)->CheckAlarms();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_check_timer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 60 * 1000000)); // 60s

    // Timer lặp chuông
    esp_timer_create_args_t ring_args = {
        .callback = [](void* arg) {
            static_cast<AlarmManager*>(arg)->OnRingTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_ring_timer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&ring_args, &ring_timer_handle_));
}

AlarmManager::~AlarmManager() {
    if (timer_handle_) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);
        esp_timer_delete(ring_timer_handle_);
        ring_timer_handle_ = nullptr;
    }
}

void AlarmManager::SetOnTriggered(std::function<void(const Alarm&)> cb) {
    on_triggered_ = cb;
}

void AlarmManager::AddAlarm(int hour, int minute,
                            const std::string& ringtone,
                            bool repeat_daily) {
    if (hour   < 0) hour   = 0;
    if (hour   > 23) hour  = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    alarms_.push_back(Alarm{hour, minute, ringtone, repeat_daily});
    ESP_LOGI(TAG, "Added alarm %02d:%02d ringtone=%s repeat=%d",
             hour, minute, ringtone.c_str(), repeat_daily ? 1 : 0);
}

void AlarmManager::RemoveAll() {
    StopRinging();  // nếu đang reo thì tắt
    alarms_.clear();
    ESP_LOGI(TAG, "All alarms cleared");
}

void AlarmManager::ListAlarms(std::string& out_json) {
    cJSON* root = cJSON_CreateArray();
    for (auto& a : alarms_) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "hour", a.hour);
        cJSON_AddNumberToObject(item, "minute", a.minute);
        cJSON_AddStringToObject(item, "ringtone", a.ringtone.c_str());
        cJSON_AddBoolToObject(item, "repeat_daily", a.repeat_daily);
        cJSON_AddItemToArray(root, item);
    }
    char* s = cJSON_PrintUnformatted(root);
    out_json.assign(s ? s : "[]");
    if (s) cJSON_free(s);
    cJSON_Delete(root);
}

void AlarmManager::CheckAlarms() {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    for (auto it = alarms_.begin(); it != alarms_.end();) {
        if (ti.tm_hour == it->hour && ti.tm_min == it->minute) {
            TriggerAlarm(*it);
            if (!it->repeat_daily) {
                it = alarms_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void AlarmManager::TriggerAlarm(const Alarm& a) {
    ESP_LOGI(TAG, "Alarm triggered at %02d:%02d (ring=%s)",
             a.hour, a.minute, a.ringtone.c_str());

    // Hiển thị popup báo thức
    Application::GetInstance().Alert("Báo thức", "Đã đến giờ!", "bell", "");

    // Bắt đầu phát chuông lặp
    StartRinging(a);

    if (on_triggered_) on_triggered_(a);
}

// ====================================================================
// Cơ chế phát chuông: 10 lần, mỗi lần cách nhau 10 giây
// ====================================================================

void AlarmManager::StartRinging(const Alarm& a) {
    current_alarm_ = a;
    ring_count_ = 0;
    is_ringing_ = true;

    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);  // reset timer nếu đang chạy
        esp_timer_start_periodic(ring_timer_handle_, 10000000); // 10s
    }

    ESP_LOGI(TAG, "StartRinging: ringtone=%s", a.ringtone.c_str());
}

void AlarmManager::OnRingTimer() {
    if (!is_ringing_) return;

    if (ring_count_ >= 10) {
        ESP_LOGI(TAG, "Reached max ring count, stopping alarm");
        StopRinging();
        return;
    }

    // id chuông mà AI / người dùng chọn
    const std::string& id = current_alarm_.ringtone;

    // mặc định: GA
    const std::string_view* ogg = &Lang::Sounds::OGG_GA;

    if (id == "ga") {
        ogg = &Lang::Sounds::OGG_GA;
    } else if (id == "alarm1") {
        ogg = &Lang::Sounds::OGG_ALARM1;
    } else if (id == "iphone") {
        ogg = &Lang::Sounds::OGG_IPHONE;
    } else {
        // giá trị lạ → ép về GA
        ogg = &Lang::Sounds::OGG_GA;
    }

    // Fallback: GA lỗi → ALARM1 → lại về GA
    if (ogg->empty()) {
        ESP_LOGW(TAG, "Selected ringtone is empty, fallback to ALARM1");
        ogg = &Lang::Sounds::OGG_ALARM1;
        if (ogg->empty()) {
            ESP_LOGW(TAG, "ALARM1 is also empty, fallback back to GA");
            ogg = &Lang::Sounds::OGG_GA;
        }
    }

    ESP_LOGI(TAG, "Playing alarm sound #%d (id=%s)", ring_count_ + 1, id.c_str());
	Application::GetInstance().GetAudioService().PlaySound(*ogg);
   
    ring_count_++;
}

void AlarmManager::StopRinging() {
    if (!is_ringing_) {
        return;
    }

    is_ringing_ = false;
    ring_count_ = 0;

    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);
    }

    ESP_LOGI(TAG, "Alarm ringing stopped");
}
