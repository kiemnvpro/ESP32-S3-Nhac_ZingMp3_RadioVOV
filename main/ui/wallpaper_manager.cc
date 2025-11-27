// main/ui/wallpaper_manager.cc
#include "ui/wallpaper_manager.h"
#include <esp_log.h>
#include "assets.h"
#include "lvgl_theme.h"
#include "board.h"
#include <cstdlib>
#include <ctime>

// Khoá LVGL + FreeRTOS
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "WallpaperManager";

void WallpaperManager::SetWallpapers(const std::vector<std::string>& names) {
    names_ = names;
    if (names_.empty()) return;
	current_index_ = current_index_ % names_.size();
    Apply(current_index_);
}

std::shared_ptr<LvglImage> WallpaperManager::LoadImage(const std::string& name) {
    // 1) Assets
    void* ptr = nullptr; size_t size = 0;
    auto& assets = Assets::GetInstance();
    if (assets.partition_valid() && assets.checksum_valid() && assets.GetAssetData(name, ptr, size)) {
        auto dot = name.find_last_of('.');
        bool is_cbin = (dot != std::string::npos) && (name.substr(dot) == ".cbin");
        if (is_cbin) {
            ESP_LOGI(TAG, "Load %s from assets as CBIN (%u bytes)", name.c_str(), (unsigned)size);
            return std::make_shared<LvglCBinImage>(ptr);
        } else {
            ESP_LOGI(TAG, "Load %s from assets as RAW (%u bytes)", name.c_str(), (unsigned)size);
            return std::make_shared<LvglRawImage>(ptr, size);
        }
    }

    // 2) SPIFFS
    std::string path = "/spiffs/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGW(TAG, "Image not found: %s (assets or SPIFFS)", name.c_str());
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)heap_caps_malloc(sz, MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); ESP_LOGE(TAG, "OOM reading %s", path.c_str()); return nullptr; }
    fread(buf, 1, sz, f);
    fclose(f);

    ESP_LOGI(TAG, "Load %s from SPIFFS (%ld bytes)", path.c_str(), sz);
    return std::make_shared<LvglAllocatedImage>(buf, (size_t)sz);
}

bool WallpaperManager::Apply(size_t index) {
    if (names_.empty()) return false;
	   index %= names_.size();

    auto display = Board::GetInstance().GetDisplay();
    if (!display) return false;

    auto& tm = LvglThemeManager::GetInstance();
    auto theme = display->GetTheme();
    if (!theme) return false;

    auto img = LoadImage(names_[index]);
    if (!img) {
        ESP_LOGE(TAG, "Failed to load image: %s", names_[index].c_str());
		
	return false;
    }

    // set cho light & dark
    if (auto light = tm.GetTheme("light")) light->set_background_image(img);
    if (auto dark  = tm.GetTheme("dark"))  dark->set_background_image(img);

    display->SetTheme(theme);
    current_index_ = index;
    ESP_LOGI(TAG, "Applied wallpaper #%u: %s", (unsigned)index, names_[index].c_str());
	// Hiển thị tên hình nền lên khung chat
    char buf[64];
    snprintf(buf, sizeof(buf), "Hình nền: %s", names_[index].c_str());
    display->SetChatMessage("system", buf);
    return true;
}

bool WallpaperManager::ApplyWithEffect(size_t index, TransitionEffect fx) {
    if (names_.empty()) return false;
	index %= names_.size();

    // Map mọi thứ về FadeBlack để an toàn tuyệt đối
    (void)fx;
    fx = TransitionEffect::FadeBlack;

    auto display = Board::GetInstance().GetDisplay();
    if (!display) return false;
    auto theme = display->GetTheme();
    if (!theme) return false;

    auto new_img = LoadImage(names_[index]);
    if (!new_img) {
        ESP_LOGE(TAG, "Failed to load image: %s", names_[index].c_str());
        return false;
    }

#ifdef HAVE_LVGL
    if (!do_fade_black_then_apply(new_img)) {
        // fallback đổi thẳng
        return Apply(index);	
    }
#else
    if (!Apply(index)) return false;
#endif

    current_index_ = index;
    ESP_LOGI(TAG, "Applied wallpaper #%u with FadeBlack", (unsigned)index);
	// Hiển thị tên hình nền lên khung chat (khi dùng hiệu ứng)
    char buf[64];
    snprintf(buf, sizeof(buf), "Hình nền: %s", names_[index].c_str());
    display->SetChatMessage("system", buf);
    return true;
}

void WallpaperManager::EnableAutoRotate(bool enable, int interval_sec) {
    auto_rotate_ = enable;
    if (interval_sec > 0) interval_sec_ = interval_sec;
    elapsed_sec_ = 0;
}

void WallpaperManager::OnTick() {
    if (!auto_rotate_ || names_.size() < 2) return;
    elapsed_sec_++;
    if (elapsed_sec_ >= interval_sec_) {
        elapsed_sec_ = 0;
		images_.clear();
		images_.reserve(names_.size());
    // chỉ dùng FadeBlack cho auto-rotate
    ApplyWithEffect((current_index_ + 1) % names_.size(), TransitionEffect::FadeBlack);
	 
     
    }
}

#ifdef HAVE_LVGL
// ======= HIỆU ỨNG DUY NHẤT: Fade đen an toàn =======
static void set_theme_bg(const std::shared_ptr<LvglImage>& new_img) {
    auto display = Board::GetInstance().GetDisplay();
    auto& tm = LvglThemeManager::GetInstance();
    auto theme = display->GetTheme();
    if (auto light = tm.GetTheme("light")) light->set_background_image(new_img);
    if (auto dark  = tm.GetTheme("dark"))  dark->set_background_image(new_img);
    display->SetTheme(theme);
}

bool WallpaperManager::do_fade_black_then_apply(const std::shared_ptr<LvglImage>& new_img) {
    const uint32_t t1 = 220, t2 = 240;

    // tạo overlay đen trên screen hiện tại
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_t* overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);

    // fade to black
    lv_anim_t a1; lv_anim_init(&a1);
    lv_anim_set_var(&a1, overlay);
    lv_anim_set_values(&a1, 0, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a1, [](void* obj, int32_t v){
        lv_obj_set_style_bg_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_set_time(&a1, t1);
    lv_anim_start(&a1);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(t1 + 30));   // để flush xong

    // áp nền mới (chỉ đụng theme, không đổi screen)
    set_theme_bg(new_img);

    // fade in từ đen
    lvgl_port_lock(0);
    lv_anim_t a2; lv_anim_init(&a2);
    lv_anim_set_var(&a2, overlay);
    lv_anim_set_values(&a2, LV_OPA_COVER, 0);
    lv_anim_set_exec_cb(&a2, [](void* obj, int32_t v){
        lv_obj_set_style_bg_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_set_time(&a2, t2);
    lv_anim_start(&a2);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(t2 + 40));

    lvgl_port_lock(0);
    lv_obj_del(overlay);
    lvgl_port_unlock();
    return true;
}


#endif // HAVE_LVGL
