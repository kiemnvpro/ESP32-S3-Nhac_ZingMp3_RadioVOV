#pragma once

#include <string>
#include <vector>
#include <functional>
#include <esp_timer.h>

struct Alarm {
    int hour;
    int minute;
    std::string ringtone;   // tên chuông (ví dụ: "/spiffs/iphone.ogg" hay "activation")
    bool repeat_daily;
};

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    void SetOnTriggered(std::function<void(const Alarm&)> cb);

    void AddAlarm(int hour, int minute,
                  const std::string& ringtone,
                  bool repeat_daily);

    void RemoveAll();

    void ListAlarms(std::string& out_json);

    // Tool "self.alarm.stop" sẽ gọi hàm này
    void StopRinging();

private:
    AlarmManager();
    ~AlarmManager();
    AlarmManager(const AlarmManager&) = delete;
    AlarmManager& operator=(const AlarmManager&) = delete;

    void CheckAlarms();
    void TriggerAlarm(const Alarm& a);

    // Nội bộ phát chuông lặp
    void StartRinging(const Alarm& a);
    void OnRingTimer();

    std::vector<Alarm> alarms_;
    esp_timer_handle_t timer_handle_ = nullptr;       // timer kiểm tra báo thức mỗi phút
    esp_timer_handle_t ring_timer_handle_ = nullptr;  // timer lặp chuông

    std::function<void(const Alarm&)> on_triggered_;

    // Trạng thái chuông hiện tại
    bool is_ringing_ = false;
    Alarm current_alarm_{};
    int ring_count_ = 0;
};
