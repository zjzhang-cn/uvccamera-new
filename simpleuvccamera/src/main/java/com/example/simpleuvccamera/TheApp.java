package com.example.simpleuvccamera;

import android.app.Application;

import com.example.simpleuvccamera.log.MyLogcatSaver;

import timber.log.Timber;

public class TheApp extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        Timber.plant(new Timber.DebugTree());
    }
}
