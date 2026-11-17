package org.openamalur.originalengine;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/** Shares only the report explicitly created by the user. */
public final class CrashReportProvider extends ContentProvider {
    static final String NAME = "open-amalur-crash-report.zip";

    static File reportFile(android.content.Context context) {
        File root = GameStorage.root(context);
        if (root == null) root = context.getExternalFilesDir(null);
        if (root == null) root = context.getCacheDir();
        return new File(new File(root, "diagnostics"), NAME);
    }

    private boolean valid(Uri uri) {
        return uri != null && "/latest".equals(uri.getPath())
                && (getContext().getPackageName() + ".crashreports").equals(uri.getAuthority());
    }

    @Override public boolean onCreate() { return true; }

    @Override public String getType(Uri uri) {
        return valid(uri) ? "application/zip" : null;
    }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!valid(uri) || !"r".equals(mode)) throw new FileNotFoundException("Report unavailable");
        return ParcelFileDescriptor.open(reportFile(getContext()), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                  String[] selectionArgs, String sortOrder) {
        if (!valid(uri)) return null;
        String[] columns = projection == null
                ? new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE} : projection;
        MatrixCursor result = new MatrixCursor(columns);
        Object[] row = new Object[columns.length];
        File file = reportFile(getContext());
        for (int index = 0; index < columns.length; ++index) {
            if (OpenableColumns.DISPLAY_NAME.equals(columns[index])) row[index] = NAME;
            else if (OpenableColumns.SIZE.equals(columns[index])) row[index] = file.length();
        }
        result.addRow(row);
        return result;
    }

    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String selection,
                                String[] args) { throw new UnsupportedOperationException(); }
}
