/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.fire.aodguard;

import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.service.dreams.IDreamManager;
import android.util.Log;

public final class FireAodGuardService extends Service {
    private static final String TAG = "FireAodGuard";
    private static final String SUPPRESSION_TOKEN = "FireAodGuard";
    private static final String DREAM_SERVICE = "dreams";

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final Runnable mEvaluateRunnable = this::evaluateNow;

    private AudioManager mAudioManager;
    private IDreamManager mDreamManager;
    private PowerManager mPowerManager;
    private boolean mSuppressed;

    private final AudioManager.OnModeChangedListener mModeChangedListener = mode -> evaluate();

    private final BroadcastReceiver mStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            evaluate();
        }
    };

    public static void start(Context context) {
        context.startService(new Intent(context, FireAodGuardService.class));
    }

    @Override
    public void onCreate() {
        super.onCreate();

        mAudioManager = getSystemService(AudioManager.class);
        mDreamManager = IDreamManager.Stub.asInterface(ServiceManager.getService(DREAM_SERVICE));
        mPowerManager = getSystemService(PowerManager.class);

        mAudioManager.addOnModeChangedListener(getMainExecutor(), mModeChangedListener);

        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_POWER_CONNECTED);
        filter.addAction(Intent.ACTION_POWER_DISCONNECTED);
        registerReceiver(mStateReceiver, filter, Context.RECEIVER_NOT_EXPORTED);

        evaluate();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        evaluate();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (mAudioManager != null) {
            mAudioManager.removeOnModeChangedListener(mModeChangedListener);
        }
        unregisterReceiver(mStateReceiver);
        setAmbientDisplaySuppressed(false);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void evaluate() {
        mHandler.removeCallbacks(mEvaluateRunnable);
        mHandler.post(mEvaluateRunnable);
    }

    private void evaluateNow() {
        boolean activeCommunication = isCommunicationActive();

        if (activeCommunication) {
            setAmbientDisplaySuppressed(true);
            stopActiveDream();
        } else {
            setAmbientDisplaySuppressed(false);
        }
    }

    private boolean isCommunicationActive() {
        int mode = mAudioManager.getMode();
        return mode == AudioManager.MODE_IN_CALL || mode == AudioManager.MODE_IN_COMMUNICATION;
    }

    private void setAmbientDisplaySuppressed(boolean suppressed) {
        if (mSuppressed == suppressed || mPowerManager == null) {
            return;
        }

        try {
            mPowerManager.suppressAmbientDisplay(SUPPRESSION_TOKEN, suppressed);
            mSuppressed = suppressed;
            Log.i(TAG, "Ambient display suppression " + (suppressed ? "enabled" : "disabled"));
        } catch (RuntimeException e) {
            Log.w(TAG, "Unable to update ambient display suppression", e);
        }
    }

    private void stopActiveDream() {
        if (mDreamManager == null) {
            return;
        }

        try {
            mDreamManager.awaken();
        } catch (RemoteException | RuntimeException e) {
            Log.w(TAG, "Unable to stop active dream", e);
        }
    }
}
