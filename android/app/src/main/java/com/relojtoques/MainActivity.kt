package com.relojtoques

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat

class MainActivity : AppCompatActivity() {

    private lateinit var wifiManager: WifiManager
    private lateinit var btnScan: Button
    private lateinit var tvCard: TextView
    private lateinit var tvHistory: TextView
    private var scanning = false
    private val history = mutableListOf<String>()

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)) {
                scanSuccess()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        wifiManager = applicationContext.getSystemService(WIFI_SERVICE) as WifiManager
        btnScan = findViewById(R.id.btnScan)
        tvCard = findViewById(R.id.tvCard)
        tvHistory = findViewById(R.id.tvHistory)

        btnScan.setOnClickListener {
            if (!scanning) startScan() else stopScan()
        }

        checkPermissions()
    }

    private fun checkPermissions() {
        val perms = arrayOf(
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_WIFI_STATE,
            Manifest.permission.CHANGE_WIFI_STATE
        )

        if (perms.any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }) {
            ActivityCompat.requestPermissions(this, perms, 1)
        }
    }

    private fun startScan() {
        scanning = true
        btnScan.text = "Detener"
        registerReceiver(receiver, IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION))
        wifiManager.startScan()
        Toast.makeText(this, "Escaneando...", Toast.LENGTH_SHORT).show()
    }

    private fun stopScan() {
        scanning = false
        btnScan.text = "Iniciar"
        try { unregisterReceiver(receiver) } catch (e: Exception) {}
        Toast.makeText(this, "Detenido", Toast.LENGTH_SHORT).show()
    }

    private fun scanSuccess() {
        wifiManager.scanResults.forEach { result ->
            val ssid = result.SSID
            if (ssid.startsWith("CARD_")) {
                decodeCard(ssid)?.let { card ->
                    showCard(card)
                }
            }
        }
        if (scanning) wifiManager.startScan()
    }

    private fun decodeCard(ssid: String): Pair<Int, Int>? {
        if (ssid.length < 11) return null

        val encoded = ssid.substring(5)
        val suitChar = encoded.substring(0, 3)

        val suit = when (suitChar) {
            "\u200B" -> 1 // ♥
            "\u200C" -> 2 // ♠
            "\u200D" -> 3 // ♣
            "\u200E" -> 4 // ♦
            else -> return null
        }

        var number = 0
        for (i in 6..9) {
            number = number shl 1
            if (encoded.getOrNull(i) == '\u200C') number = number or 1
        }

        return if (number in 1..13) Pair(suit, number) else null
    }

    private fun showCard(card: Pair<Int, Int>) {
        val suits = arrayOf("", "♥", "♠", "♣", "♦")
        val numbers = arrayOf("", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K")

        val cardStr = "${numbers[card.second]} ${suits[card.first]}"
        tvCard.text = cardStr
        history.add(0, cardStr)
        if (history.size > 10) history.removeLast()
        tvHistory.text = history.joinToString("\n")

        Toast.makeText(this, "Carta: $cardStr", Toast.LENGTH_SHORT).show()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (scanning) stopScan()
    }
}
