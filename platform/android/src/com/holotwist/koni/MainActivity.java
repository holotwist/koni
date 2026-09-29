package com.holotwist.koni;

import android.app.NativeActivity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.IBinder;
import android.os.PowerManager;
import android.provider.MediaStore;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import java.io.File;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

import android.content.pm.ServiceInfo;

public class MainActivity extends NativeActivity {

    static {
        System.loadLibrary("koni-sparkles");
    }

    public static native void setStorageBaseNative(String dataPath, String cachePath);
    public static native void addMusicDirNative(String dirPath);
    public static native void addTrackNative(String path, String title, String artist, String album, int durationSec, long mtime);
    public static native void libraryReloadNative();
    public static native void triggerRescanNative();
    public static native boolean isNativeReady();
    public static native void triggerBackNative();
    public static native void saveStateNative();

    private PowerManager.WakeLock wakeLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();

        String dataPath = getFilesDir().getAbsolutePath();
        String cachePath = getCacheDir().getAbsolutePath();
        setStorageBaseNative(dataPath, cachePath);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "koni:audiowakelock");
            wakeLock.acquire(10 * 60 * 1000L);
        }

        startPlaybackService();
        requestAudioPermissions();

        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                new android.window.OnBackInvokedCallback() {
                    @Override
                    public void onBackInvoked() {
                        triggerBackNative();
                    }
                }
            );
        }
    }

    @Override
    public void onBackPressed() {
        triggerBackNative();
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
            triggerBackNative();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    protected void onPause() {
        saveStateNative();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (hasAudioPermission()) {
            scanAndRegisterMusicDirs();
        }
    }

    @Override
    protected void onDestroy() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        stopPlaybackService();
        super.onDestroy();
    }

    private void startPlaybackService() {
        try {
            Intent intent = new Intent(this, PlaybackService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
            } else {
                startService(intent);
            }
        } catch (Exception ignored) {}
    }

    private void stopPlaybackService() {
        try {
            stopService(new Intent(this, PlaybackService.class));
        } catch (Exception ignored) {}
    }

    private void hideSystemBars() {
        Window window = getWindow();
        if (window == null) return;
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        View decorView = window.getDecorView();
        if (decorView != null) {
            decorView.post(new Runnable() {
                @Override
                public void run() {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        Window w = getWindow();
                        if (w != null) {
                            w.setDecorFitsSystemWindows(false);
                            WindowInsetsController c = w.getInsetsController();
                            if (c != null) {
                                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                            }
                        }
                    } else {
                        decorView.setSystemUiVisibility(
                            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        );
                    }
                }
            });
        }
    }

    private boolean hasAudioPermission() {
        if (Build.VERSION.SDK_INT >= 33) {
            return checkSelfPermission("android.permission.READ_MEDIA_AUDIO") == PackageManager.PERMISSION_GRANTED;
        } else if (Build.VERSION.SDK_INT >= 23) {
            return checkSelfPermission("android.permission.READ_EXTERNAL_STORAGE") == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    private void requestAudioPermissions() {
        List<String> perms = new ArrayList<>();
        if (!hasAudioPermission()) {
            if (Build.VERSION.SDK_INT >= 33) {
                perms.add("android.permission.READ_MEDIA_AUDIO");
            } else if (Build.VERSION.SDK_INT >= 23) {
                perms.add("android.permission.READ_EXTERNAL_STORAGE");
            }
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED) {
            perms.add("android.permission.POST_NOTIFICATIONS");
        }
        if (!perms.isEmpty()) {
            requestPermissions(perms.toArray(new String[0]), 101);
        } else {
            scanAndRegisterMusicDirs();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (grantResults != null && grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            scanAndRegisterMusicDirs();
        }
    }

    public void scanAndRegisterMusicDirs() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    int attempts = 0;
                    while (!isNativeReady() && attempts++ < 50) {
                        Thread.sleep(100);
                    }

                    HashSet<String> dirs = new HashSet<>();

                    File extMusic = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC);
                    if (extMusic != null && extMusic.exists()) dirs.add(extMusic.getAbsolutePath());
                    else dirs.add("/storage/emulated/0/Music");

                    File extDown = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                    if (extDown != null && extDown.exists()) dirs.add(extDown.getAbsolutePath());
                    else dirs.add("/storage/emulated/0/Download");

                    for (String d : dirs) {
                        addMusicDirNative(d);
                    }

                    Uri uri = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
                    String[] projection = {
                        MediaStore.Audio.Media.DATA,
                        MediaStore.Audio.Media.TITLE,
                        MediaStore.Audio.Media.ARTIST,
                        MediaStore.Audio.Media.ALBUM,
                        MediaStore.Audio.Media.DURATION,
                        MediaStore.Audio.Media.DATE_MODIFIED
                    };
                    String selection = MediaStore.Audio.Media.IS_MUSIC + " != 0";
                    Cursor cursor = getContentResolver().query(uri, projection, selection, null, null);
                    if (cursor != null) {
                        int dataCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DATA);
                        int titleCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE);
                        int artistCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST);
                        int albumCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM);
                        int durCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION);
                        int mtimeCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_MODIFIED);

                        while (cursor.moveToNext()) {
                            String filePath = cursor.getString(dataCol);
                            if (filePath != null && !filePath.startsWith("/storage/emulated/0/Android/")) {
                                String title = cursor.getString(titleCol);
                                String artist = cursor.getString(artistCol);
                                String album = cursor.getString(albumCol);
                                int durationSec = cursor.getInt(durCol) / 1000;
                                long mtime = cursor.getLong(mtimeCol);

                                addTrackNative(filePath, title, artist, album, durationSec, mtime);
                            }
                        }
                        cursor.close();
                    }

                    libraryReloadNative();
                    triggerRescanNative();
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }).start();
    }

    public static class PlaybackService extends Service {
        private static final String CHANNEL_ID = "koni_playback";

        @Override
        public void onCreate() {
            super.onCreate();
            try {
                createNotificationChannel();
                Notification notif = buildNotification();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    startForeground(1, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
                } else {
                    startForeground(1, notif);
                }
            } catch (Exception ignored) {}
        }

        @Override
        public int onStartCommand(Intent intent, int flags, int startId) {
            return START_STICKY;
        }

        @Override
        public IBinder onBind(Intent intent) {
            return null;
        }

        private void createNotificationChannel() {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Koni Audio Playback", NotificationManager.IMPORTANCE_LOW);
                channel.setDescription("Background playback service");
                NotificationManager nm = getSystemService(NotificationManager.class);
                if (nm != null) nm.createNotificationChannel(channel);
            }
        }

        private Notification buildNotification() {
            Intent nIntent = new Intent(this, MainActivity.class);
            PendingIntent pIntent = PendingIntent.getActivity(
                this, 0, nIntent,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);

            Notification.Builder b = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

            return b.setContentTitle("Koni")
                .setContentText("Playing audio")
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentIntent(pIntent)
                .setOngoing(true)
                .build();
        }
    }
}