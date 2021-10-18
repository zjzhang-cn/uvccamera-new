package com.example.simpleuvccamera;

import android.hardware.usb.UsbDevice;
import android.util.Log;

import com.serenegiant.usb.USBMonitor;
import com.serenegiant.usb.UVCCamera;

import java.util.List;

public class SimpleUSBMonitorListener implements USBMonitor.OnDeviceConnectListener {

    @Override
    public void onAttach(UsbDevice device) {

    }

    @Override
    public void onDettach(UsbDevice device) {

    }

    @Override
    public void onConnect(UsbDevice device, USBMonitor.UsbControlBlock ctrlBlock, boolean createNew) {

    }

    @Override
    public void onDisconnect(UsbDevice device, USBMonitor.UsbControlBlock ctrlBlock) {

    }

    @Override
    public void onCancel(UsbDevice device) {

    }
}
