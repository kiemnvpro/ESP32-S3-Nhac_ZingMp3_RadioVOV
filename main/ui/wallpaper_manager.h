// main/ui/wallpaper_manager.h
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <esp_timer.h>
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "assets.h"

class WallpaperManager {
public:
    enum class TransitionEffect {
        FadeBlack = 0
    };

    static WallpaperManager& GetInstance() {
        static WallpaperManager ins;
        return ins;
    }

    // Đăng ký danh sách ảnh (tên trong assets hoặc đường dẫn SPIFFS)
    void SetWallpapers(const std::vector<std::string>& names);

    // Đổi nền ngay lập tức theo index (0..n-1)
    bool Apply(size_t index);

    // Đổi nền kèm hiệu ứng
    bool ApplyWithEffect(size_t index, TransitionEffect fx);

    // Bật/tắt auto-rotate, interval tính bằng giây // đổi mỗi 180 giây (3 phút)
    void EnableAutoRotate(bool enable, int interval_sec = 180);

    // Gọi mỗi giây từ timer hệ thống
    void OnTick();

    // Lấy index hiện tại (để lưu/khôi phục)
    size_t current_index() const { return current_index_; }

private:
    WallpaperManager() = default;
    ~WallpaperManager() = default;

    // Thử lấy ảnh từ Assets (ưu tiên) hoặc SPIFFS
    std::shared_ptr<LvglImage> LoadImage(const std::string& name);

#ifdef HAVE_LVGL
    // Các hiệu ứng chuyển cảnh
    bool do_fade_black_then_apply(const std::shared_ptr<LvglImage>& new_img);
#endif

    std::vector<std::string> names_;
	// *** mới thêm: cache hình đã decode ***
    std::vector<std::shared_ptr<LvglImage>> images_;
    size_t current_index_ = 0;

    bool auto_rotate_ = true;
    int interval_sec_ = 180; // đổi mỗi 180 giây (3 phút)
    int elapsed_sec_ = 0;
};
