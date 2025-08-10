package com.example.oboepassthrough

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {

    private val permissionRequestCode = 101

    // REMOVED: Do not declare or call external JNI functions from the Activity.
    // external fun startPassthrough()
    // external fun stopPassthrough()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        findViewById<Button>(R.id.startButton).setOnClickListener {
            // Check for permissions before starting the service.
            if (arePermissionsGranted()) {
                startAudioService()
            } else {
                requestPermissions()
            }
        }

        findViewById<Button>(R.id.stopButton).setOnClickListener {
            stopAudioService()
        }
    }

    // This function sends a command to start the service.
    private fun startAudioService() {
        val serviceIntent = Intent(this, AudioProcessingService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)
    }

    // This function sends a command to stop the service.
    private fun stopAudioService() {
        val serviceIntent = Intent(this, AudioProcessingService::class.java)
        stopService(serviceIntent)
    }

    // --- Permission Handling Logic ---

    private fun arePermissionsGranted(): Boolean {
        val audioPermission = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED

        val notificationPermission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(
                this, Manifest.permission.POST_NOTIFICATIONS
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
        return audioPermission && notificationPermission
    }

    private fun requestPermissions() {
        val permissionsToRequest = mutableListOf<String>()
        permissionsToRequest.add(Manifest.permission.RECORD_AUDIO)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissionsToRequest.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        ActivityCompat.requestPermissions(
            this,
            permissionsToRequest.toTypedArray(),
            permissionRequestCode
        )
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == permissionRequestCode) {
            if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                startAudioService()
            }
        }
    }

    companion object {
        init {
            System.loadLibrary("native-lib")
        }
    }
}