# Kaynak dosyalar glob yerine açıkça listelenir:
#  - proje yolunda [ ] gibi karakterler varsa glob hiçbir dosya bulamaz, PlatformIO
#    "Couldn't find the main target of the project!" hatası verir;
#  - glob CMake önbelleğinde kalır, yeni eklenen .cpp derlenmez ("undefined reference").
# YENİ BİR .cpp DOSYASI EKLERSEN BU LİSTEYE DE EKLE.
#
# REQUIRES: PlatformIO bu klasörü "main" değil ekstra bileşen olarak ekler; ESP-IDF
# sürücüleri yalnızca "main"e otomatik bağlar. Yeni bir sürücü (I2C, SPI...) kullanırsan
# bileşenini buraya ekle (örn. esp_driver_i2c).

idf_component_register(
    SRCS
        "main.cpp"
        "control.cpp"
        "ibus.cpp"
        "motor.cpp"
        "power_monitor.cpp"
        "telemetry.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES
        esp_driver_gpio
        esp_driver_uart
        esp_driver_mcpwm
        esp_adc
        esp_timer
)
