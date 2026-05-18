#include "weather_icons.h"

#include "weather_icon_images.h"

static const lv_img_dsc_t *pick_day_night(const lv_img_dsc_t *day, const lv_img_dsc_t *night, bool is_day)
{
    return is_day ? day : night;
}

const lv_img_dsc_t *weather_code_to_image_ex(int code, bool is_day)
{
    switch (code) {
    case 0:
        return pick_day_night(&weather_img_clear_day, &weather_img_clear_night, is_day);
    case 1:
    case 2:
        return pick_day_night(&weather_img_partly_cloudy_day, &weather_img_partly_cloudy_night, is_day);
    case 3:
        return pick_day_night(&weather_img_cloudy_day, &weather_img_cloudy_night, is_day);
    case 45:
    case 48:
        return pick_day_night(&weather_img_fog_day, &weather_img_fog_night, is_day);
    case 51:
    case 53:
    case 55:
        /* Drizzle — pack has no drizzle asset; rain icon + WMO label distinguishes */
        return pick_day_night(&weather_img_rain_day, &weather_img_rain_night, is_day);
    case 56:
    case 57:
        return pick_day_night(&weather_img_sleet_day, &weather_img_sleet_night, is_day);
    case 61:
    case 63:
    case 65:
        return pick_day_night(&weather_img_rain_day, &weather_img_rain_night, is_day);
    case 66:
    case 67:
        return pick_day_night(&weather_img_sleet_day, &weather_img_sleet_night, is_day);
    case 71:
    case 73:
    case 75:
    case 77:
        return pick_day_night(&weather_img_snow_day, &weather_img_snow_night, is_day);
    case 80:
    case 81:
    case 82:
        /* Rain showers */
        return pick_day_night(&weather_img_rain_day, &weather_img_rain_night, is_day);
    case 85:
    case 86:
        return pick_day_night(&weather_img_snow_day, &weather_img_snow_night, is_day);
    case 95:
        return pick_day_night(&weather_img_thunderstorm_day, &weather_img_thunderstorm_night, is_day);
    case 96:
    case 99:
        return pick_day_night(&weather_img_hail_day, &weather_img_hail_night, is_day);
    default:
        return &weather_img_unknown;
    }
}

const lv_img_dsc_t *weather_code_to_image(int code)
{
    return weather_code_to_image_ex(code, true);
}

bool weather_code_uses_rain_background(int code)
{
    switch (code) {
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
    case 95:
    case 96:
    case 99:
        return true;
    default:
        return false;
    }
}

/* WMO Weather interpretation codes (WW) — Open-Meteo / WMO 4677 */
const char *weather_wind_level_label(float wind_kmh)
{
    if (wind_kmh < 1.0f) {
        return "Calm";
    }
    if (wind_kmh < 6.0f) {
        return "Light air";
    }
    if (wind_kmh < 12.0f) {
        return "Light breeze";
    }
    if (wind_kmh < 20.0f) {
        return "Gentle breeze";
    }
    if (wind_kmh < 29.0f) {
        return "Moderate breeze";
    }
    if (wind_kmh < 39.0f) {
        return "Fresh breeze";
    }
    if (wind_kmh < 50.0f) {
        return "Windy";
    }
    if (wind_kmh < 62.0f) {
        return "Strong wind";
    }
    if (wind_kmh < 75.0f) {
        return "Near gale";
    }
    if (wind_kmh < 88.0f) {
        return "Gale";
    }
    if (wind_kmh < 103.0f) {
        return "Strong gale";
    }
    if (wind_kmh < 118.0f) {
        return "Storm";
    }
    return "Violent storm";
}

const char *weather_code_to_label(int code)
{
    switch (code) {
    case 0:
        return "Clear sky";
    case 1:
        return "Mainly clear";
    case 2:
        return "Partly cloudy";
    case 3:
        return "Overcast";
    case 45:
        return "Fog";
    case 48:
        return "Depositing rime fog";
    case 51:
        return "Light drizzle";
    case 53:
        return "Moderate drizzle";
    case 55:
        return "Dense drizzle";
    case 56:
        return "Light freezing drizzle";
    case 57:
        return "Dense freezing drizzle";
    case 61:
        return "Slight rain";
    case 63:
        return "Moderate rain";
    case 65:
        return "Heavy rain";
    case 66:
        return "Light freezing rain";
    case 67:
        return "Heavy freezing rain";
    case 71:
        return "Slight snow";
    case 73:
        return "Moderate snow";
    case 75:
        return "Heavy snow";
    case 77:
        return "Snow grains";
    case 80:
        return "Slight rain showers";
    case 81:
        return "Moderate rain showers";
    case 82:
        return "Violent rain showers";
    case 85:
        return "Slight snow showers";
    case 86:
        return "Heavy snow showers";
    case 95:
        return "Thunderstorm";
    case 96:
        return "Thunderstorm with slight hail";
    case 99:
        return "Thunderstorm with heavy hail";
    default:
        return "Unknown";
    }
}

void weather_icon_set_src(lv_obj_t *img_obj, int weather_code)
{
    weather_icon_set_src_ex(img_obj, weather_code, true);
}

void weather_icon_set_src_ex(lv_obj_t *img_obj, int weather_code, bool is_day)
{
    if (!img_obj) {
        return;
    }
    lv_img_set_src(img_obj, weather_code_to_image_ex(weather_code, is_day));
}
