package com.mixtape.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.IBinder;
import android.os.PowerManager;

/**
 * Музыка играет, пока экран выключен или приложение свёрнуто: Android не трогает процесс,
 * у которого есть такой сервис с уведомлением. В шторке — название и кнопки «назад / пауза / вперёд».
 */
public class PlaybackService extends Service {
    static final String ACTION_UPDATE = "com.mixtape.app.UPDATE";
    static final String ACTION_MEDIA = "com.mixtape.app.MEDIA";
    private static final String CHANNEL = "playback";
    private static final int NOTE_ID = 1;
    private PowerManager.WakeLock lock;

    @Override
    public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationChannel ch = new NotificationChannel(CHANNEL, "Воспроизведение", NotificationManager.IMPORTANCE_LOW);
        ch.setShowBadge(false);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);
        lock = ((PowerManager) getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Mixtape:play");
        lock.setReferenceCounted(false);
    }

    @Override
    public int onStartCommand(Intent i, int flags, int startId) {
        if (i != null && ACTION_MEDIA.equals(i.getAction())) {
            try { MainActivity.nativeOnMedia(i.getIntExtra("a", 0)); } catch (Throwable ignored) { }
            return START_NOT_STICKY;
        }
        boolean playing = i != null && i.getBooleanExtra("playing", false);
        String title = i != null ? i.getStringExtra("title") : "";
        String artist = i != null ? i.getStringExtra("artist") : "";
        Notification n = build(playing, title == null ? "" : title, artist == null ? "" : artist);
        if (playing) {
            startForeground(NOTE_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            lock.acquire(6 * 60 * 60 * 1000L);
        } else {
            // пауза: уведомление остаётся (можно продолжить из шторки), но процесс больше не держим
            stopForeground(STOP_FOREGROUND_DETACH);
            getSystemService(NotificationManager.class).notify(NOTE_ID, n);
            if (lock.isHeld()) lock.release();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        if (lock != null && lock.isHeld()) lock.release();
        super.onDestroy();
    }

    private PendingIntent media(int action) {
        Intent i = new Intent(this, PlaybackService.class).setAction(ACTION_MEDIA).putExtra("a", action);
        return PendingIntent.getService(this, 10 + action, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private Notification build(boolean playing, String title, String artist) {
        Intent open = new Intent(this, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Icon icPrev = Icon.createWithResource(this, android.R.drawable.ic_media_previous);
        Icon icPlay = Icon.createWithResource(this, playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play);
        Icon icNext = Icon.createWithResource(this, android.R.drawable.ic_media_next);
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle(title.isEmpty() ? "Mixtape" : title)
                .setContentText(artist)
                .setContentIntent(pi)
                .setOngoing(playing)
                .setOnlyAlertOnce(true)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .addAction(new Notification.Action.Builder(icPrev, "Назад", media(2)).build())
                .addAction(new Notification.Action.Builder(icPlay, playing ? "Пауза" : "Играть", media(0)).build())
                .addAction(new Notification.Action.Builder(icNext, "Вперёд", media(1)).build())
                .setStyle(new Notification.MediaStyle().setShowActionsInCompactView(0, 1, 2))
                .build();
    }
}
