package com.elitemagic.notes.watch

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.wifi.ScanResult
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Gestor de beacons WiFi del reloj RELOJTOQUES
 * Reemplaza VoiceRecognitionManager escaneando beacons WiFi "MOE_W"
 */
class WatchBeaconManager(private val context: Context) {

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val _recognizedCard = MutableStateFlow<String?>(null)
    val recognizedCard: StateFlow<String?> = _recognizedCard.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    private val _scanStartTime = MutableStateFlow<Long?>(null)
    val scanStartTime: StateFlow<Long?> = _scanStartTime.asStateFlow()

    private val wifiManager: WifiManager =
        context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

    private val wifiScanReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val success = intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)
            if (success) {
                scanSuccess()
            } else {
                scanFailure()
            }
        }
    }

    private fun scanSuccess() {
        val results: List<ScanResult> = wifiManager.scanResults
        Log.d(TAG, "Scan success! Found ${results.size} networks")

        // Buscar beacon "MOE_W"
        val moeBeacon = results.find { it.SSID.startsWith("MOE_W") }

        if (moeBeacon != null) {
            Log.d(TAG, "Found MOE_W beacon: ${moeBeacon.SSID}")
            val card = decodeCardFromSSID(moeBeacon.SSID)
            if (card != null) {
                _recognizedCard.value = card
                _isScanning.value = false
                Log.d(TAG, "Decoded card: $card")
            }
        } else {
            Log.d(TAG, "MOE_W beacon not found, continuing scan...")
            // Continuar escaneando
            if (_isScanning.value) {
                wifiManager.startScan()
            }
        }
    }

    private fun scanFailure() {
        Log.e(TAG, "Scan failed")
        // Reintentar
        if (_isScanning.value) {
            wifiManager.startScan()
        }
    }

    /**
     * Decodifica la carta desde el SSID con caracteres invisibles
     * Formato: "MOE_W" + caracteres invisibles codificando palo y número
     */
    private fun decodeCardFromSSID(ssid: String): String? {
        if (!ssid.startsWith("MOE_W")) return null

        try {
            // Extraer la parte invisible después de "MOE_W"
            val invisiblePart = ssid.substring(5)

            // Los primeros 3 caracteres invisibles codifican el palo
            // U+200B x3 = Corazones (♥)
            // U+200C x3 = Picas (♠)
            // U+200D x3 = Tréboles (♣)
            // U+200E x3 = Diamantes (♦)

            val suit = when {
                invisiblePart.startsWith("\u200B\u200B\u200B") -> "♥"
                invisiblePart.startsWith("\u200C\u200C\u200C") -> "♠"
                invisiblePart.startsWith("\u200D\u200D\u200D") -> "♣"
                invisiblePart.startsWith("\u200E\u200E\u200E") -> "♦"
                else -> return null
            }

            // Los siguientes 4 caracteres codifican el número en binario
            // U+200B = 0, U+200C = 1
            val numberPart = invisiblePart.substring(3, minOf(7, invisiblePart.length))
            var number = 0
            for (char in numberPart) {
                number = number shl 1
                if (char == '\u200C') number = number or 1
            }

            // Convertir número a nombre de carta
            val cardName = when (number) {
                1 -> "AS"
                in 2..10 -> number.toString()
                11 -> "J"
                12 -> "Q"
                13 -> "K"
                else -> return null
            }

            return "$cardName de $suit"
        } catch (e: Exception) {
            Log.e(TAG, "Error decoding SSID: ${e.message}")
            return null
        }
    }

    fun startScanning() {
        Log.d(TAG, "Starting WiFi scan for MOE_W beacon")
        _scanStartTime.value = System.currentTimeMillis()
        _error.value = null
        _recognizedCard.value = null
        _isScanning.value = true

        // Registrar receiver
        val filter = IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(wifiScanReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(wifiScanReceiver, filter)
        }

        // Iniciar scan
        val success = wifiManager.startScan()
        if (!success) {
            _error.value = "No se pudo iniciar el escaneo WiFi"
            _isScanning.value = false
        }
    }

    fun stopScanning() {
        Log.d(TAG, "Stopping scan")
        _isScanning.value = false
        try {
            context.unregisterReceiver(wifiScanReceiver)
        } catch (e: IllegalArgumentException) {
            // Receiver not registered, ignore
        }
    }

    fun destroy() {
        stopScanning()
    }

    fun clearRecognizedCard() {
        _recognizedCard.value = null
    }

    companion object {
        private const val TAG = "WatchBeaconManager"
    }
}
