package com.example.oboepassthrough

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {

    external fun startPassthrough()
    external fun stopPassthrough()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        }

        findViewById<Button>(R.id.startButton).setOnClickListener {
            startPassthrough()
        }

        findViewById<Button>(R.id.stopButton).setOnClickListener {
            stopPassthrough()
        }
    }

    companion object {
        init {
            System.loadLibrary("native-lib")
        }
    }
}
