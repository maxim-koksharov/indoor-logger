// components/main/include/OLEDDriver.hpp
#pragma once

// Объявляем C-структуру из драйвера, чтобы использовать ее в классе
extern "C" {
    #include "ssd1306.h"
}

constexpr uint32_t oled_sie = 128 * 64;

class OLEDDriver {
public:
    OLEDDriver(int sda_pin, int scl_pin, uint8_t i2c_addr = 0x3C);
    
    // Инициализация I2C и дисплея
    void initialize(); 

    // Основные методы рисования
    void clear();
    void draw_circle(uint8_t x, uint8_t y, uint8_t r);
    // virtual void refresh();
    
    // Рисование текста
    // virtual void drawText(uint8_t x, uint8_t y, const char* text);

    // Рисование базовых фигур (точка)
    // virtual void drawPixel(uint8_t x, uint8_t y);

private:
    ssd1306_t dev; // Структура состояния дисплея из C-драйвера
    int _sda_pin;
    int _scl_pin;
    uint8_t _i2c_addr;
    uint8_t framebuffer[oled_sie];
};