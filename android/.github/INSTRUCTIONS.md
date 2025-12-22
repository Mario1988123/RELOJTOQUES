# 📦 Cómo Descargar la App desde GitHub Actions

## Pasos para Obtener el APK

1. **Ve a la pestaña Actions**
   - En el repositorio de GitHub, haz click en la pestaña **"Actions"** (arriba)

2. **Busca la última compilación exitosa**
   - Verás una lista de workflows ejecutados
   - Busca uno con el nombre **"Android CI"** que tenga un check verde (✅)
   - Haz click en él

3. **Descarga el APK**
   - Baja hasta la sección **"Artifacts"**
   - Verás **"app-debug"** listado
   - Haz click para descargar el ZIP
   - Descomprime el ZIP para obtener **app-debug.apk**

4. **Instala en tu dispositivo**
   - Transfiere el APK a tu dispositivo Android
   - Habilita "Instalar desde fuentes desconocidas" en Configuración
   - Toca el APK para instalarlo

## 🔄 Compilación Automática

Cada vez que haces **push** a las ramas `main`, `master` o cualquier rama que empiece con `claude/`, GitHub Actions:

- ✅ Compila el proyecto automáticamente
- ✅ Ejecuta los tests
- ✅ Genera el APK debug
- ✅ Lo sube como artifact

## 🚨 Si la Compilación Falla

1. Ve a la pestaña **Actions**
2. Haz click en la ejecución fallida (❌ rojo)
3. Revisa los logs para ver el error
4. Los errores más comunes:
   - Falta de dependencias (se resuelven automáticamente)
   - Errores de sintaxis en el código
   - Problemas con el SDK de Android (GitHub Actions lo instala automáticamente)

## 💡 Tip

Puedes también ejecutar el workflow manualmente:
1. Ve a Actions → Android CI
2. Haz click en "Run workflow"
3. Selecciona la rama
4. Haz click en "Run workflow"

Esto es útil si quieres compilar sin hacer ningún cambio en el código.
