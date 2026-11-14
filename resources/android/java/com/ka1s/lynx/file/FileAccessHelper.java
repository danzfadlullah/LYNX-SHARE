package com.ka1s.lynx.file;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Android Storage Access Framework (SAF) helper for LYNX SHARE.
 * Provides document picker for opening/saving .lynx, .flp, .als, .flm files.
 * Called from C++ via JNI (see src/platform/AndroidJni.cpp).
 */
public class FileAccessHelper {
    private static final String TAG = "LYNX_FileAccess";
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_SAVE = 1002;
    private static final int REQUEST_FOLDER = 1003;

    private static FileAccessHelper instance;
    private static Activity activity;
    private static FileAccessCallback callback;

    public interface FileAccessCallback {
        void onFileOpened(Uri uri, String displayName);
        void onFileSaved(Uri uri, String displayName);
        void onFolderPicked(Uri uri, String displayName);
        void onError(String error);
        void onCancelled();
    }

    public static void initialize(Activity act) {
        activity = act;
        instance = new FileAccessHelper();
    }

    public static void setCallback(FileAccessCallback cb) {
        callback = cb;
    }

    // --- Open file (ACTION_OPEN_DOCUMENT) ---

    public static void openDocument(String mimeTypes) {
        if (activity == null) {
            if (callback != null) callback.onError("Activity not initialized");
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*"); // We'll filter by mime types in the picker

        // Add multiple mime types
        String[] mimes = mimeTypes.split(";");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimes);

        // Allow multiple selection? No, single file for project
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);

        activity.startActivityForResult(intent, REQUEST_OPEN);
    }

    // --- Save file (ACTION_CREATE_DOCUMENT) ---

    public static void saveDocument(String suggestedName, String mimeType) {
        if (activity == null) {
            if (callback != null) callback.onError("Activity not initialized");
            return;
        }

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeType);
        intent.putExtra(Intent.EXTRA_TITLE, suggestedName);

        activity.startActivityForResult(intent, REQUEST_SAVE);
    }

    // --- Pick folder (ACTION_OPEN_DOCUMENT_TREE) ---

    public static void pickFolder() {
        if (activity == null) {
            if (callback != null) callback.onError("Activity not initialized");
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        activity.startActivityForResult(intent, REQUEST_FOLDER);
    }

    // --- Handle activity result (called from MainActivity.onActivityResult) ---

    public static void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (callback == null) return;

        if (resultCode != Activity.RESULT_OK) {
            callback.onCancelled();
            return;
        }

        if (data == null || data.getData() == null) {
            callback.onError("No data returned");
            return;
        }

        Uri uri = data.getData();
        String displayName = getDisplayName(uri);

        switch (requestCode) {
            case REQUEST_OPEN:
                // Take persistable URI permission
                takePersistablePermission(uri);
                callback.onFileOpened(uri, displayName);
                break;
            case REQUEST_SAVE:
                takePersistablePermission(uri);
                callback.onFileSaved(uri, displayName);
                break;
            case REQUEST_FOLDER:
                takePersistablePermission(uri);
                callback.onFolderPicked(uri, displayName);
                break;
        }
    }

    private static void takePersistablePermission(Uri uri) {
        if (activity == null) return;
        ContentResolver resolver = activity.getContentResolver();
        int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        try {
            resolver.takePersistableUriPermission(uri, flags);
        } catch (SecurityException e) {
            Log.w(TAG, "Could not take persistable permission", e);
        }
    }

    private static String getDisplayName(Uri uri) {
        if (activity == null) return "unknown";
        ContentResolver resolver = activity.getContentResolver();
        try (Cursor cursor = resolver.query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                if (nameIndex >= 0) {
                    return cursor.getString(nameIndex);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not get display name", e);
        }
        return "unknown";
    }

    // --- Convert content:// URI to file path (copies to app private dir if needed) ---

    public static File uriToFile(Uri uri, String fallbackExtension) {
        if (activity == null) return null;
        ContentResolver resolver = activity.getContentResolver();

        // If it's already a file:// URI, return the file directly
        if ("file".equals(uri.getScheme())) {
            return new File(uri.getPath());
        }

        // For content:// URIs, we need to copy to a local file
        // Create a temporary file in the app's cache directory
        File cacheDir = activity.getCacheDir();
        String fileName = getDisplayName(uri);
        if (fileName == null || fileName.isEmpty()) {
            fileName = "lynx_project" + fallbackExtension;
        } else if (!fileName.contains(".")) {
            fileName += fallbackExtension;
        }

        File localFile = new File(cacheDir, fileName);

        try (InputStream in = resolver.openInputStream(uri);
             OutputStream out = new FileOutputStream(localFile)) {
            if (in == null) return null;
            byte[] buffer = new byte[8192];
            int len;
            while ((len = in.read(buffer)) > 0) {
                out.write(buffer, 0, len);
            }
            return localFile;
        } catch (Exception e) {
            Log.e(TAG, "Failed to copy URI to file", e);
            return null;
        }
    }

    // --- Save local file to content:// URI ---

    public static boolean saveFileToUri(File localFile, Uri uri) {
        if (activity == null || localFile == null || !localFile.exists()) return false;
        ContentResolver resolver = activity.getContentResolver();

        try (InputStream in = new java.io.FileInputStream(localFile);
             OutputStream out = resolver.openOutputStream(uri)) {
            if (out == null) return false;
            byte[] buffer = new byte[8192];
            int len;
            while ((len = in.read(buffer)) > 0) {
                out.write(buffer, 0, len);
            }
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to save file to URI", e);
            return false;
        }
    }

    // --- Release persistable permission ---

    public static void releasePermission(Uri uri) {
        if (activity == null) return;
        ContentResolver resolver = activity.getContentResolver();
        try {
            resolver.releasePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "Could not release persistable permission", e);
        }
    }
}