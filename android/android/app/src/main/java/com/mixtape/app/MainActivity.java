package com.mixtape.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.View;
import android.view.Window;
import android.widget.EditText;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

/**
 * Окно приложения. Весь интерфейс рисует C++ (Dear ImGui), здесь — только то, что умеет лишь Android:
 * ввод текста системной клавиатурой, выбор и сохранение файлов, буфер обмена, цвет полос, фоновая игра.
 * C++ вызывает статические методы через JNI; ответы возвращаются в native-методы ниже.
 */
public class MainActivity extends NativeActivity {
    static MainActivity inst;
    private static final int REQ_PICK = 4100;
    private int pendingPickId = 0;
    private String pendingPickKind = "";

    // ответы в C++ (android_main.cpp)
    static native void nativeOnText(int id, String text);           // null — отмена
    static native void nativeOnPicked(int id, String[] paths);      // пустой массив — отмена
    static native void nativeOnMedia(int action);                   // 0 пауза/пуск, 1 следующая, 2 предыдущая

    @Override
    protected void onCreate(Bundle b) {
        inst = this;
        super.onCreate(b);
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"}, 1);
    }

    @Override
    protected void onDestroy() {
        if (inst == this) inst = null;
        super.onDestroy();
    }

    // «Назад» обрабатывает C++ (закрывает окна приложения); сюда доходит, только если закрывать нечего:
    // сворачиваемся, а не закрываемся — музыка играет дальше
    @Override
    public void onBackPressed() {
        moveTaskToBack(true);
    }

    static void goBackground() {
        inst.runOnUiThread(() -> inst.moveTaskToBack(true));
    }

    // ---------- вызывается из C++ ----------

    static String filesDir() { return inst.getFilesDir().getAbsolutePath(); }
    static String cacheDir() { return inst.getCacheDir().getAbsolutePath(); }

    static boolean isOnline() {
        try {
            ConnectivityManager cm = (ConnectivityManager) inst.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkCapabilities c = cm.getNetworkCapabilities(cm.getActiveNetwork());
            return c != null && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
        } catch (Throwable t) {
            return true;
        }
    }

    static void copyText(String s) {
        inst.runOnUiThread(() -> {
            ClipboardManager cm = (ClipboardManager) inst.getSystemService(Context.CLIPBOARD_SERVICE);
            cm.setPrimaryClip(ClipData.newPlainText("Mixtape", s));
        });
    }

    // цвет строки состояния и панели навигации — в цвет фона темы
    static void setBarsColor(int argb) {
        inst.runOnUiThread(() -> {
            Window w = inst.getWindow();
            w.setStatusBarColor(argb);
            w.setNavigationBarColor(argb);
            View d = w.getDecorView();
            d.setSystemUiVisibility(d.getSystemUiVisibility() & ~View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR & ~View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        });
    }

    // ввод текста: обычное окно Android с полем, клавиатура появляется сама
    static void promptText(int id, String title, String initial, boolean multiline) {
        inst.runOnUiThread(() -> {
            EditText e = new EditText(inst);
            e.setText(initial);
            e.setSelection(e.getText().length());
            if (multiline) {
                e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
                e.setMinLines(6);
            } else {
                e.setSingleLine(true);
                e.setInputType(InputType.TYPE_CLASS_TEXT);
            }
            FrameLayout box = new FrameLayout(inst);
            int pad = (int) (20 * inst.getResources().getDisplayMetrics().density);
            box.setPadding(pad, pad / 2, pad, 0);
            box.addView(e);
            AlertDialog d = new AlertDialog.Builder(inst, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(title)
                    .setView(box)
                    .setPositiveButton("ОК", (dlg, w) -> nativeOnText(id, e.getText().toString()))
                    .setNegativeButton("Отмена", (dlg, w) -> nativeOnText(id, null))
                    .setOnCancelListener(dlg -> nativeOnText(id, null))
                    .create();
            if (!multiline)
                e.setOnEditorActionListener((v, a, ev) -> { nativeOnText(id, e.getText().toString()); d.dismiss(); return true; });
            d.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            d.show();
            e.requestFocus();
        });
    }

    // пароль: символы скрыты, без подсказок клавиатуры
    static void promptPassword(int id, String title) {
        inst.runOnUiThread(() -> {
            EditText e = new EditText(inst);
            e.setSingleLine(true);
            e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            FrameLayout box = new FrameLayout(inst);
            int pad = (int) (20 * inst.getResources().getDisplayMetrics().density);
            box.setPadding(pad, pad / 2, pad, 0);
            box.addView(e);
            AlertDialog d = new AlertDialog.Builder(inst, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(title)
                    .setView(box)
                    .setPositiveButton("ОК", (dlg, w) -> nativeOnText(id, e.getText().toString()))
                    .setNegativeButton("Отмена", (dlg, w) -> nativeOnText(id, null))
                    .setOnCancelListener(dlg -> nativeOnText(id, null))
                    .create();
            e.setOnEditorActionListener((v, a, ev) -> { nativeOnText(id, e.getText().toString()); d.dismiss(); return true; });
            d.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            d.show();
            e.requestFocus();
        });
    }

    // выбор файлов: kind = audio | image | backup; файлы копируются во временную папку приложения
    static void pick(int id, String kind) {
        inst.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            if (kind.equals("audio")) { i.setType("audio/*"); i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true); }
            else if (kind.equals("image")) i.setType("image/*");
            else i.setType("*/*");
            inst.pendingPickId = id;
            inst.pendingPickKind = kind;
            try {
                inst.startActivityForResult(i, REQ_PICK);
            } catch (Throwable t) {
                nativeOnPicked(id, new String[0]);
            }
        });
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK) return;
        final int id = pendingPickId;
        final ArrayList<Uri> uris = new ArrayList<>();
        if (res == Activity.RESULT_OK && data != null) {
            if (data.getClipData() != null)
                for (int k = 0; k < data.getClipData().getItemCount(); ++k) uris.add(data.getClipData().getItemAt(k).getUri());
            else if (data.getData() != null) uris.add(data.getData());
        }
        new Thread(() -> {
            ArrayList<String> paths = new ArrayList<>();
            File dir = new File(getCacheDir(), "picked");
            dir.mkdirs();
            for (Uri u : uris) {
                try {
                    String name = displayName(u);
                    File out = new File(dir, System.nanoTime() + "_" + name.replaceAll("[\\\\/:*?\"<>|]", "_"));
                    try (InputStream in = getContentResolver().openInputStream(u); OutputStream os = new FileOutputStream(out)) {
                        copy(in, os);
                    }
                    // имя без служебного префикса: по нему C++ подписывает песню
                    File nice = new File(new File(dir, "n" + System.nanoTime()), name);
                    nice.getParentFile().mkdirs();
                    paths.add(out.renameTo(nice) ? nice.getAbsolutePath() : out.getAbsolutePath());
                } catch (Throwable ignored) { }
            }
            nativeOnPicked(id, paths.toArray(new String[0]));
        }).start();
    }

    private String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Throwable ignored) { }
        return "file";
    }

    private static void copy(InputStream in, OutputStream out) throws java.io.IOException {
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    // готовый файл — в общую папку: песни в «Музыка/Mixtape», резервные копии в «Загрузки/Mixtape»
    static String publish(String path, String kind) {
        File f = new File(path);
        boolean audio = kind.equals("audio");
        ContentResolver cr = inst.getContentResolver();
        ContentValues v = new ContentValues();
        v.put(MediaStore.MediaColumns.DISPLAY_NAME, f.getName());
        v.put(MediaStore.MediaColumns.MIME_TYPE, audio ? mimeFor(f.getName()) : "application/json");
        v.put(MediaStore.MediaColumns.RELATIVE_PATH, (audio ? Environment.DIRECTORY_MUSIC : Environment.DIRECTORY_DOWNLOADS) + "/Mixtape");
        v.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri coll = audio ? MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                         : MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
        Uri item = cr.insert(coll, v);
        if (item == null) return "";
        try (InputStream in = new FileInputStream(f); OutputStream out = cr.openOutputStream(item)) {
            copy(in, out);
        } catch (Throwable t) {
            cr.delete(item, null, null);
            return "";
        }
        v.clear();
        v.put(MediaStore.MediaColumns.IS_PENDING, 0);
        cr.update(item, v, null, null);
        f.delete();
        return (audio ? "Музыка/Mixtape/" : "Загрузки/Mixtape/") + f.getName();
    }

    private static String mimeFor(String name) {
        String n = name.toLowerCase();
        if (n.endsWith(".m4a") || n.endsWith(".aac")) return "audio/mp4";
        if (n.endsWith(".flac")) return "audio/flac";
        if (n.endsWith(".ogg")) return "audio/ogg";
        if (n.endsWith(".wav")) return "audio/wav";
        return "audio/mpeg";
    }

    // что играет: уведомление в шторке и работа в фоне
    static void nowPlaying(boolean playing, String title, String artist) {
        Context c = inst != null ? inst : null;
        if (c == null) return;
        Intent i = new Intent(c, PlaybackService.class);
        i.setAction(PlaybackService.ACTION_UPDATE);
        i.putExtra("playing", playing);
        i.putExtra("title", title);
        i.putExtra("artist", artist);
        try {
            if (playing) c.startForegroundService(i);
            else c.startService(i);
        } catch (Throwable ignored) { }
    }
}
