# 🃏 Reloj Toques

Sistema para transmitir cartas de poker mediante toques en un reloj ESP32-S3. Las cartas se envían por WiFi usando beacons y se visualizan en una app Android.

## 🎯 Cómo Funciona

1. **Dar toques en el reloj** para seleccionar palo (1-4 toques)
2. **Esperar 2-3 segundos**
3. **Dar toques** para número (1-13 toques)
4. La carta se transmite automáticamente por WiFi beacon
5. La app Android la detecta y muestra

## 🃏 Codificación de Cartas

### Palos (1-4 toques)
- 1 toque = ♥ Corazones
- 2 toques = ♠ Picas
- 3 toques = ♣ Tréboles
- 4 toques = ♦ Diamantes

### Números (1-13 toques)
- 1 = As
- 2-10 = 2-10
- 11 = J (Jota)
- 12 = Q (Reina)
- 13 = K (Rey)

## 📁 Estructura

```
RELOJTOQUES/
├── esp32/              # Firmware Arduino para ESP32-S3
├── android/            # App Android (Kotlin)
└── .github/workflows/  # CI/CD para compilar APK
```

## 🚀 Instalación Rápida

### ESP32
1. Abrir `esp32/reloj_toques.ino` en Arduino IDE
2. Seleccionar board ESP32S3
3. Upload

### Android
El APK se compila automáticamente en GitHub Actions.
Descárgalo de: https://github.com/Mario1988123/RELOJTOQUES/actions

## 📱 Uso

1. Instalar APK en Android
2. Abrir app y dar permisos
3. Presionar "Iniciar Escaneo"
4. En ESP32: dar toques para enviar carta
5. Ver carta en app

## 🛠️ Hardware

- ESP32-S3
- Acelerómetro QMI8658 (I2C: SDA=GPIO18, SCL=GPIO8)

## 📄 Licencia

Código abierto - Uso educativo
