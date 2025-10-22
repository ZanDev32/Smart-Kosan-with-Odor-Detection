
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/Picopixel.h>

#ifndef SSH1106_H
#define SSH1106_H
#define I2C_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Optional overrides for ESP32/ESP32-C3 custom I2C pins and clock
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN -1 // use core default if -1
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN -1 // use core default if -1
#endif
#ifndef OLED_CLOCK_HZ
#define OLED_CLOCK_HZ 400000
#endif

class SH1106Display {
private:
  Adafruit_SH1106G display;

public:
  SH1106Display() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

  bool begin() {
    // Initialize I2C; on ESP32-family allow specifying pins
#if defined(ESP32)
    if (OLED_SDA_PIN >= 0 && OLED_SCL_PIN >= 0) {
      Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, OLED_CLOCK_HZ);
    } else {
      Wire.begin();
      Wire.setClock(OLED_CLOCK_HZ);
    }
#else
    Wire.begin();
    Wire.setClock(OLED_CLOCK_HZ);
#endif
    return display.begin(I2C_ADDR, true);
  }

  void clear() {
    display.clearDisplay();
  }

  void show() {
    display.display();
  }

  void displayDHT22(float temperature, float humidity, float heatIndex) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    // Display title
    display.setCursor(18, 0);
    display.setFont(&Picopixel);
    display.println("ROOM AIR QUALITY STATUS");
    display.setFont(NULL);
    
    // Draw box
    display.drawRect(0, 0, 128, 64, SH110X_WHITE);
    display.drawLine(0, 8, 128, 8, SH110X_WHITE);
    
    // Display temperature
    display.setCursor(2, 20);
    display.print(F("Temp : ")); 
    display.print(temperature, 1); 
    display.println(F(" C"));
    
    // Display humidity
    display.setCursor(2, 32);
    display.print(F("Humidity : ")); 
    display.print(humidity, 1); 
    display.println(F(" %"));
    
    // Display heat index
    display.setCursor(2, 44);
    display.print(F("Feels like: ")); 
    display.print(heatIndex, 1); 
    display.println(F(" C"));
  }

  // Append a one-line gas reading at the bottom of the framed area
  void displayMQ135(float gasPpm) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(2, 56);
    display.print(F("CO2: "));
    display.print(gasPpm, 0);
    display.print(F("ppm "));
  }
};

#endif
