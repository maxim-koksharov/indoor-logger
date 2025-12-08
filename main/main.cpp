// main/main.cpp

#include "OLEDDriver.hpp"

// Обязательная обертка для C-заголовков FreeRTOS
extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
}

#define SCL_PIN 5
#define SDA_PIN 4
#define I2C_BUS 0

// 1. Создаем глобальный объект драйвера
// Пин D1 (GPIO5) = SCL, Пин D2 (GPIO4) = SDA (типично для NodeMCU/ESP8266)
OLEDDriver oled(5, 4); 

// 2. Создаем задачу FreeRTOS для работы с дисплеем
// (Рекомендуется выполнять длительные операции в отдельной задаче)
void oled_test_task(void *pvParameter) {
    oled.initialize();
    
    while(1) {
        oled.clear();
        
        // 1. Рисуем текст (первая строка)
        // oled.drawText(0, 0, "Hello from C++!");
        
        // 2. Рисуем вторую строку
        // oled.drawText(0, 16, "RTOS SSD1306"); 

        // 3. Рисуем простую фигуру (точка)
        // for (int i = 0; i < 128; i += 5) {
        //     oled.drawPixel(i, 32);
        // }

        // 4. Отправляем буфер на дисплей
        // oled.refresh();
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Задержка 2 секунды

        oled.draw_circle(10, 20, 5);

        // Пример прокрутки текста
        // oled.clear();
        // oled.drawText(0, 0, "Scrolling test...");
        // oled.drawText(0, 16, "Portugal! *");
        // oled.refresh();
        vTaskDelay(pdMS_TO_TICKS(200000));

        oled.draw_circle(10, 20, 10);
    }

    vTaskDelete(NULL);
}


// Точка входа в RTOS SDK
extern "C" void app_main(void) {
    // Создаем задачу FreeRTOS
    xTaskCreate(oled_test_task, "oled_test", 4096, NULL, 5, NULL);
}