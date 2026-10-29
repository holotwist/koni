package com.holotwist.koni;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
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

public class MainActivity extends android.app.NativeActivity {

    static {
        System.loadLibrary("koni-sparkles");
    }

    public static native void setStorageBaseNative(String dataPath, String cachePath);
    public static native void addMusicDirNative(String dirPath);
    public static native void beginTransactionNative();
    public static native void commitTransactionNative();
    public static native void addTrackNative(String path, String title, String artist, String album, int durationSec, long mtime);
    public static native void libraryReloadNative();
    public static native void triggerRescanNative();
    public static native boolean isNativeReady();
    public static native void triggerBackNative();
    public static native void saveStateNative();
    public static native void sendMediaCommandNative(int cmd, long arg);

    private PowerManager.WakeLock wakeLock;
    private static boolean sScannedOnce = false;
    private static MainActivity sInstance = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
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
        if (hasAudioPermission() && !sScannedOnce) {
            scanAndRegisterMusicDirs();
        }
    }

    @Override
    protected void onDestroy() {
        saveStateNative();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        stopPlaybackService();
        sInstance = null;
        super.onDestroy();
        // Terminate process
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    public static void sendMediaCommand(int cmd, long arg) {
        sendMediaCommandNative(cmd, arg);
    }

    // Called from C
    public static void updateMediaStateFromNative(int state, String title, String artist, String album, long posMs, long durMs, String artPath) {
        if (PlaybackService.sInstance != null) {
            PlaybackService.sInstance.updateState(state, title, artist, album, posMs, durMs, artPath);
        }
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
        sScannedOnce = true;
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

                        // Batch inserts within an explicit transaction
                        beginTransactionNative();
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
                        commitTransactionNative();
                        cursor.close();
                    }

                    libraryReloadNative();
                    triggerRescanNative();
                } catch (Exception e) {
                    commitTransactionNative();
                    e.printStackTrace();
                }
            }
        }).start();
    }

    public static class PlaybackService extends Service {
        public static PlaybackService sInstance = null;
        private static final String CHANNEL_ID = "koni_playback";
        private static final int NOTIF_ID = 1;

        private MediaSession mSession;
        private NotificationManager mNotifyMgr;
        private AudioManager mAudioMgr;
        private AudioFocusRequest mFocusRequest;

        private String mTitle = "Koni";
        private String mArtist = "";
        private String mAlbum = "";
        private String mArtPath = "";
        private Bitmap mArtBitmap = null;
        private long mPosMs = 0;
        private long mDurMs = 0;
        private int mState = 0;

        @Override
        public void onCreate() {
            super.onCreate();
            sInstance = this;
            mNotifyMgr = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            mAudioMgr = (AudioManager) getSystemService(Context.AUDIO_SERVICE);

            createNotificationChannel();
            initMediaSession();
            requestAudioFocus();

            try {
                Notification notif = buildNotification();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    startForeground(NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
                } else {
                    startForeground(NOTIF_ID, notif);
                }
            } catch (Exception e) {
                stopSelf();
            }
        }

        @Override
        public int onStartCommand(Intent intent, int flags, int startId) {
            if (intent != null && intent.getAction() != null) {
                String act = intent.getAction();
                if ("com.holotwist.koni.PREV".equals(act)) {
                    MainActivity.sendMediaCommand(3, 0);
                } else if ("com.holotwist.koni.PLAY_PAUSE".equals(act)) {
                    MainActivity.sendMediaCommand(1, 0);
                } else if ("com.holotwist.koni.NEXT".equals(act)) {
                    MainActivity.sendMediaCommand(2, 0);
                }
            }
            return START_NOT_STICKY;
        }

        @Override
        public void onTaskRemoved(Intent rootIntent) {
            // Terminate process
            if (mSession != null) {
                mSession.setActive(false);
                mSession.release();
            }
            abandonAudioFocus();
            stopForeground(true);
            stopSelf();
            android.os.Process.killProcess(android.os.Process.myPid());
            super.onTaskRemoved(rootIntent);
        }

        @Override
        public void onDestroy() {
            if (mSession != null) {
                mSession.setActive(false);
                mSession.release();
            }
            abandonAudioFocus();
            if (mArtBitmap != null) {
                mArtBitmap.recycle();
                mArtBitmap = null;
            }
            sInstance = null;
            super.onDestroy();
        }

        @Override
        public IBinder onBind(Intent intent) {
            return null;
        }

        public void updateState(int state, String title, String artist, String album, long posMs, long durMs, String artPath) {
            mState = state;
            mTitle = (title != null && !title.isEmpty()) ? title : "Koni";
            mArtist = (artist != null) ? artist : "";
            mAlbum = (album != null) ? album : "";
            mPosMs = posMs;
            mDurMs = durMs;

            if (artPath != null && !artPath.equals(mArtPath)) {
                mArtPath = artPath;
                if (mArtBitmap != null) {
                    mArtBitmap.recycle();
                    mArtBitmap = null;
                }
                if (!artPath.isEmpty()) {
                    File f = new File(artPath);
                    if (f.exists()) {
                        try {
                            mArtBitmap = BitmapFactory.decodeFile(artPath);
                        } catch (Exception ignored) {}
                    }
                }
            }

            syncMediaSession();

            try {
                Notification notif = buildNotification();
                mNotifyMgr.notify(NOTIF_ID, notif);
            } catch (Exception ignored) {}
        }

        private void initMediaSession() {
            mSession = new MediaSession(this, "KoniMediaSession");
            mSession.setCallback(new MediaSession.Callback() {
                @Override
                public void onPlay() {
                    MainActivity.sendMediaCommand(5, 0);
                }

                @Override
                public void onPause() {
                    MainActivity.sendMediaCommand(6, 0);
                }

                @Override
                public void onSkipToNext() {
                    MainActivity.sendMediaCommand(2, 0);
                }

                @Override
                public void onSkipToPrevious() {
                    MainActivity.sendMediaCommand(3, 0);
                }

                @Override
                public void onSeekTo(long pos) {
                    MainActivity.sendMediaCommand(4, pos);
                }

                @Override
                public void onStop() {
                    MainActivity.sendMediaCommand(6, 0);
                }
            });
            mSession.setActive(true);
        }

        private void syncMediaSession() {
            if (mSession == null) return;

            long actions = PlaybackState.ACTION_PLAY
                         | PlaybackState.ACTION_PAUSE
                         | PlaybackState.ACTION_PLAY_PAUSE
                         | PlaybackState.ACTION_SKIP_TO_NEXT
                         | PlaybackState.ACTION_SKIP_TO_PREVIOUS
                         | PlaybackState.ACTION_SEEK_TO
                         | PlaybackState.ACTION_STOP;

            int pbState = PlaybackState.STATE_NONE;
            if (mState == 1) pbState = PlaybackState.STATE_PLAYING;
            else if (mState == 2) pbState = PlaybackState.STATE_PAUSED;
            else pbState = PlaybackState.STATE_STOPPED;

            PlaybackState.Builder psb = new PlaybackState.Builder()
                .setActions(actions)
                .setState(pbState, mPosMs, mState == 1 ? 1.0f : 0.0f);
            mSession.setPlaybackState(psb.build());

            MediaMetadata.Builder mb = new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, mTitle)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, mArtist)
                .putString(MediaMetadata.METADATA_KEY_ALBUM, mAlbum)
                .putLong(MediaMetadata.METADATA_KEY_DURATION, mDurMs);

            if (mArtBitmap != null && !mArtBitmap.isRecycled()) {
                mb.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, mArtBitmap);
            }
            mSession.setMetadata(mb.build());
        }

        private void createNotificationChannel() {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Koni Audio Playback", NotificationManager.IMPORTANCE_LOW);
                channel.setDescription("Audio playback and media controls");
                if (mNotifyMgr != null) mNotifyMgr.createNotificationChannel(channel);
            }
        }

        private Notification buildNotification() {
            boolean isPlaying = (mState == 1);
            int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
            }

            Intent nIntent = new Intent(this, MainActivity.class);
            nIntent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            PendingIntent pIntent = PendingIntent.getActivity(this, 0, nIntent, pendingFlags);

            Intent prevIntent = new Intent(this, PlaybackService.class).setAction("com.holotwist.koni.PREV");
            PendingIntent pPrev = PendingIntent.getService(this, 101, prevIntent, pendingFlags);

            Intent ppIntent = new Intent(this, PlaybackService.class).setAction("com.holotwist.koni.PLAY_PAUSE");
            PendingIntent pPlayPause = PendingIntent.getService(this, 102, ppIntent, pendingFlags);

            Intent nextIntent = new Intent(this, PlaybackService.class).setAction("com.holotwist.koni.NEXT");
            PendingIntent pNext = PendingIntent.getService(this, 103, nextIntent, pendingFlags);

            Notification.Action prevAction = new Notification.Action.Builder(
                android.R.drawable.ic_media_previous, "Previous", pPrev).build();

            Notification.Action ppAction = new Notification.Action.Builder(
                isPlaying ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play,
                isPlaying ? "Pause" : "Play", pPlayPause).build();

            Notification.Action nextAction = new Notification.Action.Builder(
                android.R.drawable.ic_media_next, "Next", pNext).build();

            Notification.Builder b = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

            b.setContentTitle(mTitle)
             .setContentText(mArtist)
             .setSubText(mAlbum)
             .setSmallIcon(android.R.drawable.ic_media_play)
             .setContentIntent(pIntent)
             .setVisibility(Notification.VISIBILITY_PUBLIC)
             .setOngoing(isPlaying)
             .addAction(prevAction)
             .addAction(ppAction)
             .addAction(nextAction);

            if (mArtBitmap != null && !mArtBitmap.isRecycled()) {
                b.setLargeIcon(mArtBitmap);
            }

            Notification.MediaStyle mediaStyle = new Notification.MediaStyle()
                .setMediaSession(mSession.getSessionToken())
                .setShowActionsInCompactView(0, 1, 2);

            b.setStyle(mediaStyle);
            return b.build();
        }

        private void requestAudioFocus() {
            if (mAudioMgr == null) return;
            AudioManager.OnAudioFocusChangeListener listener = new AudioManager.OnAudioFocusChangeListener() {
                @Override
                public void onAudioFocusChange(int focusChange) {
                    if (focusChange == AudioManager.AUDIOFOCUS_LOSS ||
                        focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                        MainActivity.sendMediaCommand(6, 0); // Pause
                    }
                }
            };

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                mFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build())
                    .setOnAudioFocusChangeListener(listener)
                    .build();
                mAudioMgr.requestAudioFocus(mFocusRequest);
            } else {
                mAudioMgr.requestAudioFocus(listener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
            }
        }

        private void abandonAudioFocus() {
            if (mAudioMgr == null) return;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && mFocusRequest != null) {
                mAudioMgr.abandonAudioFocusRequest(mFocusRequest);
            }
        }
    }
}