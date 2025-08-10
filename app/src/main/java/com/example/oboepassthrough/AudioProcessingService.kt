package com.example.oboepassthrough

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

class AudioProcessingService : Service() {

    // This function is called when the service is first created.
    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    // This function is called every time the service is started.
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Create the persistent notification.
        val notification = createNotification()

        // Start the service in the foreground.
        // This is a requirement for services that access the microphone from the background.
        startForeground(NOTIFICATION_ID, notification)

        // Call your C++ function to start the audio processing.
        startPassthrough()

        // If the service is killed by the system, it will be automatically restarted.
        return START_STICKY
    }

    // This function is called when the service is destroyed.
    override fun onDestroy() {
        // Call your C++ function to stop the audio processing and release resources.
        stopPassthrough()
        super.onDestroy()
    }

    // A foreground service requires a notification channel on Android 8.0 (API 26) and higher.
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "Audio Processing Service Channel",
                NotificationManager.IMPORTANCE_DEFAULT
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(serviceChannel)
        }
    }

    // Creates the notification that will be shown to the user.
    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Hearing Support Active")
            .setContentText("Audio is being processed in the background.")
            // Replace with your actual app icon
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .build()
    }

    // We don't need to bind to this service, so we return null.
    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    // --- JNI Functions ---
    // These declarations link to the C++ functions you've already written.
    private external fun startPassthrough()
    private external fun stopPassthrough()

    companion object {
        const val CHANNEL_ID = "AudioProcessingServiceChannel"
        const val NOTIFICATION_ID = 1
    }
}
