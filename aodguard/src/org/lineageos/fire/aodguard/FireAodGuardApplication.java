/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.fire.aodguard;

import android.app.Application;

public final class FireAodGuardApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        FireAodGuardService.start(this);
    }
}
