cmake_minimum_required(VERSION 3.16.0)

# Sadece kendi bileşenimizi (src) ve onun gerçekten ihtiyaç duyduğu ESP-IDF
# bileşenlerini derle. Varsayılanda ESP-IDF tüm bileşenleri (Bluetooth, GDB stub,
# HTTP sunucusu...) derler; PlatformIO 7.1.3 + ESP-IDF 6.1'de esp_gdbstub
# "unknown opcode or format name 'rsr.CONFIGID0'" hatası veriyor. Bonus: derleme hızlanır.
# __pio_env: PlatformIO'nun kendi eklediği boş bileşen, listede olmak zorunda.
set(COMPONENTS src __pio_env)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(katamaran)
