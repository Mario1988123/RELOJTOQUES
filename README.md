# RELOJTOQUES 🎩✨

**Sistema completo de magia con reloj inteligente ESP32-S3 y app Android**

## 📋 Descripción

RELOJTOQUES es un sistema de magia profesional que combina:

- **Reloj inteligente ESP32-S3**: Permite seleccionar una carta de forma invisible mediante toques
- **App Android**: Recibe la carta mediante beacons WiFi y la muestra discretamente  
- **Transmisión invisible**: Utiliza caracteres Unicode invisibles en el SSID WiFi

## 🎯 Funcionamiento

### En el Reloj (ESP32-S3)

1. **Pantalla principal**: Muestra la hora normalmente
2. **Activar modo secreto**: Mantener pulsada la pantalla 2 segundos
3. **Pantalla secreta**: Aparece pantalla negra con título "Notificación"
4. **Seleccionar palo**: Toca 1-4 veces (♥ ♠ ♣ ♦)
5. **Espera 2 segundos** sin tocar para confirmar
6. **Seleccionar número**: Toca 1-13 veces (A, 2-10, J, Q, K)
7. **Espera 2 segundos** para confirmar y transmitir
8. **Transmisión continua**: Emite beacons WiFi "MOE_W" hasta apagar

### En la App Android

1. Abrir app RELOJTOQUES
2. Crear/abrir una nota
3. Presionar botón de escaneo WiFi
4. La app detecta automáticamente el beacon "MOE_W"
5. Muestra la carta decodificada
6. Insertar carta en la nota

## 🔧 Compilación Firmware ESP32

```bash
# Requiere ESP-IDF v5.3+
cd esp32/
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 📱 Compilar App Android

```bash
cd android/
./gradlew assembleDebug
# APK en: app/build/outputs/apk/debug/
```

## ✨ Características Clave

- Selección invisible de carta mediante toques
- Transmisión WiFi con caracteres invisibles (U+200B, U+200C, U+200D, U+200E)
- Interfaz LVGL en reloj
- App con Jetpack Compose
- Decodificación automática en tiempo real

---

**¡Que disfrutes la magia!** ✨
