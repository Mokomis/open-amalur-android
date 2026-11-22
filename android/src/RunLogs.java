package org.openamalur.originalengine;

import java.io.File;
import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.Arrays;

/** The native engine writes one run-<seconds> log folder per launch under the storage root. */
final class RunLogs {
    /** Run folders kept after pruning. Crash reports use the newest two. */
    static final int KEEP = 10;

    private RunLogs() {}

    static long runNumber(File file) {
        String name = file.getName();
        if (!name.matches("run-[0-9]{1,19}")) return -1;
        try { return Long.parseLong(name.substring(4)); }
        catch (NumberFormatException invalid) { return -1; }
    }

    /** Run folders under root, newest first. */
    static File[] newestFirst(File root) {
        File[] runs = root == null ? null
                : root.listFiles(file -> file.isDirectory() && runNumber(file) >= 0);
        if (runs == null) return new File[0];
        Arrays.sort(runs, (a, b) -> Long.compare(runNumber(b), runNumber(a)));
        return runs;
    }

    /** Deletes all but the newest {@link #KEEP} run folders. Returns how many were removed. */
    static int prune(File root) {
        File[] runs = newestFirst(root);
        int removed = 0;
        for (int i = KEEP; i < runs.length; i++) {
            try {
                Path run = runs[i].toPath();
                // Only real folders directly under the storage root; never follow links.
                if (Files.isSymbolicLink(run) || !run.toRealPath().getParent()
                        .equals(root.toPath().toRealPath())) continue;
                deleteTree(run);
                removed++;
            } catch (IOException | SecurityException failure) {
                android.util.Log.w("VeilSetup", "Could not remove old run log " + runs[i].getName(), failure);
            }
        }
        return removed;
    }

    private static void deleteTree(Path folder) throws IOException {
        Files.walkFileTree(folder, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attributes)
                    throws IOException {
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }

            @Override public FileVisitResult postVisitDirectory(Path directory, IOException failure)
                    throws IOException {
                if (failure != null) throw failure;
                Files.delete(directory);
                return FileVisitResult.CONTINUE;
            }
        });
    }
}
