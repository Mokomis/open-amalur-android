package org.openamalur.originalengine;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

/** Reconstructs Veil archive entries from the matching retail entries. */
final class VeilPak {
    private static final int BLOCK_BYTES = 4096;
    private static final int MAX_ENTRY_BYTES = 64 * 1024 * 1024;
    private static final int MAX_PATCHED_ENTRIES = 128;
    private static final int MAX_DIRECTORY_ENTRIES = 250_000;
    private static final int RECORD_ALIGNMENT = 16;
    static final int DIRECTORY_FIELD = 16;
    private static final long MAGIC = 0x5645494c45443031L; // VEILED01

    private static final class Record {
        final long offset;
        final int length;

        Record(long offset, int length) {
            this.offset = offset;
            this.length = length;
        }
    }

    private static int readInt(RandomAccessFile file) throws IOException {
        return Integer.reverseBytes(file.readInt());
    }

    private static void writeInt(RandomAccessFile file, int value) throws IOException {
        file.writeInt(Integer.reverseBytes(value));
    }

    private static Map<Integer, Record> readDirectory(RandomAccessFile file, long offset, long extent)
            throws IOException {
        file.seek(0);
        if (file.readInt() != 0x4b41526c || readInt(file) != 1 ||
                readInt(file) != RECORD_ALIGNMENT || readInt(file) != BLOCK_BYTES) {
            throw invalidArchive();
        }
        if (offset < 32 || offset > extent - 8) throw invalidArchive();
        file.seek(offset);
        int count = readInt(file);
        file.readInt();
        if (count < 1 || count > MAX_DIRECTORY_ENTRIES || 12L * count > extent - offset - 8) {
            throw invalidArchive();
        }
        Map<Integer, Record> records = new TreeMap<>(Integer::compareUnsigned);
        for (int i = 0; i < count; i++) {
            int key = readInt(file);
            long start = Integer.toUnsignedLong(readInt(file)) * RECORD_ALIGNMENT;
            int size = readInt(file);
            if (start < 32 || start > offset - 8 || records.put(key, new Record(start, size)) != null) {
                throw invalidArchive();
            }
        }
        return records;
    }

    static long retailDirectory(RandomAccessFile file) throws IOException {
        file.seek(DIRECTORY_FIELD);
        return Long.reverseBytes(file.readLong());
    }

    private static byte[] readRecord(RandomAccessFile file, Record record, int expectedBytes)
            throws IOException {
        if (record == null || expectedBytes != record.length || expectedBytes > MAX_ENTRY_BYTES
                || expectedBytes <= 0) throw invalidArchive();
        file.seek(record.offset);
        if (readInt(file) != expectedBytes) throw invalidArchive();
        int blocks = readInt(file);
        byte[] source = new byte[expectedBytes];
        if (blocks == 0) {
            file.readFully(source);
            return source;
        }
        if (blocks != (expectedBytes + BLOCK_BYTES - 1) / BLOCK_BYTES) throw invalidArchive();
        int[] sizes = new int[blocks];
        for (int i = 0; i < blocks; i++) {
            sizes[i] = readInt(file);
            if (sizes[i] < 2 || sizes[i] > BLOCK_BYTES * 2) throw invalidArchive();
        }
        byte[] encoded = new byte[BLOCK_BYTES * 2];
        for (int i = 0; i < blocks; i++) {
            file.readFully(encoded, 0, sizes[i]);
            int offset = i * BLOCK_BYTES;
            new ApLibBlock(encoded, sizes[i]).decode(source, offset,
                    Math.min(BLOCK_BYTES, expectedBytes - offset));
        }
        return source;
    }

    private static final class ApLibBlock {
        private final byte[] data;
        private final int length;
        private int position;
        private int tag;
        private int bits;

        ApLibBlock(byte[] data, int length) {
            this.data = data;
            this.length = length;
        }

        private int read() throws IOException {
            if (position == length) throw invalidArchive();
            return data[position++] & 0xff;
        }

        private int bit() throws IOException {
            if (bits == 0) {
                tag = read();
                bits = 8;
            }
            return (tag >> --bits) & 1;
        }

        private int gamma() throws IOException {
            int value = 1;
            do {
                value = value * 2 + bit();
                if (value > BLOCK_BYTES * 2) throw invalidArchive();
            } while (bit() != 0);
            return value;
        }

        void decode(byte[] target, int start, int size) throws IOException {
            int cursor = start;
            int end = start + size;
            int previousDistance = 0;
            boolean afterMatch = false;
            target[cursor++] = (byte) read();
            while (true) {
                if (bit() == 0) {
                    if (cursor == end) throw invalidArchive();
                    target[cursor++] = (byte) read();
                    afterMatch = false;
                    continue;
                }
                int distance;
                int count;
                if (bit() == 0) {
                    int high = gamma();
                    if (!afterMatch && high == 2) {
                        distance = previousDistance;
                        count = gamma();
                    } else {
                        distance = ((high - (afterMatch ? 2 : 3)) << 8) | read();
                        count = gamma();
                        if (distance < 128 || distance >= 32000) count += 2;
                        else if (distance >= 1280) count++;
                    }
                } else if (bit() == 0) {
                    int command = read();
                    distance = command >> 1;
                    if (distance == 0) break;
                    count = 2 + (command & 1);
                } else {
                    distance = 0;
                    for (int i = 0; i < 4; i++) distance = distance * 2 + bit();
                    if (distance > cursor - start || cursor == end) throw invalidArchive();
                    target[cursor] = distance == 0 ? 0 : target[cursor - distance];
                    cursor++;
                    afterMatch = false;
                    continue;
                }
                if (distance <= 0 || distance > cursor - start || count > end - cursor) {
                    throw invalidArchive();
                }
                for (int i = 0; i < count; i++, cursor++) target[cursor] = target[cursor - distance];
                previousDistance = distance;
                afterMatch = true;
            }
            if (cursor != end || position != length) throw invalidArchive();
        }
    }

    static void apply(RandomAccessFile patch, File initialFile, long retailBytes, long retailDirectory,
                      InputStream recipe, MessageDigest digest) throws IOException {
        try (RandomAccessFile initial = new RandomAccessFile(initialFile, "r");
             DataInputStream edits = new DataInputStream(new BufferedInputStream(recipe))) {
            Map<Integer, Record> patchRecords = readDirectory(patch, retailDirectory, retailBytes);
            Map<Integer, Record> initialRecords = readDirectory(initial, retailDirectory(initial), initial.length());
            if (edits.readLong() != MAGIC) throw invalidRecipe();
            int count = edits.readInt();
            if (count < 1 || count > MAX_PATCHED_ENTRIES) throw invalidRecipe();

            long end = retailBytes;
            long previousKey = -1;
            for (int i = 0; i < count; i++) {
                int key = edits.readInt();
                int slot = edits.readUnsignedByte();
                int sourceBytes = edits.readInt();
                int targetBytes = edits.readInt();
                long unsignedKey = Integer.toUnsignedLong(key);
                if (unsignedKey <= previousKey || slot > 1 || targetBytes < 1 ||
                        targetBytes > MAX_ENTRY_BYTES) throw invalidRecipe();
                previousKey = unsignedKey;

                byte[] source = slot == 0
                        ? readRecord(patch, patchRecords.get(key), sourceBytes)
                        : readRecord(initial, initialRecords.get(key), sourceBytes);
                byte[] sourceHash = new byte[32];
                byte[] targetHash = new byte[32];
                edits.readFully(sourceHash);
                edits.readFully(targetHash);
                if (!Arrays.equals(sourceHash, digest.digest(source))) throw invalidArchive();

                byte[] target = applyEdits(edits, source, targetBytes);
                if (!Arrays.equals(targetHash, digest.digest(target))) throw invalidRecipe();
                long aligned = (end + RECORD_ALIGNMENT - 1) & ~(RECORD_ALIGNMENT - 1L);
                patch.seek(end);
                patch.write(new byte[(int) (aligned - end)]);
                writeInt(patch, targetBytes);
                writeInt(patch, 0);
                patch.write(target);
                patchRecords.put(key, new Record(aligned, targetBytes));
                end = patch.getFilePointer();
            }
            if (edits.read() != -1) throw invalidRecipe();
            writeInt(patch, patchRecords.size());
            writeInt(patch, 0);
            for (Map.Entry<Integer, Record> entry : patchRecords.entrySet()) {
                writeInt(patch, entry.getKey());
                writeInt(patch, (int) (entry.getValue().offset / RECORD_ALIGNMENT));
                writeInt(patch, entry.getValue().length);
            }
            long length = patch.getFilePointer();
            patch.seek(DIRECTORY_FIELD);
            patch.writeLong(Long.reverseBytes(end));
            patch.setLength(length);
            patch.getFD().sync();
        }
    }

    private static byte[] applyEdits(DataInputStream edits, byte[] source, int size) throws IOException {
        byte[] target = new byte[size];
        int position = 0;
        int opcode;
        while ((opcode = edits.readUnsignedByte()) != 0) {
            if (opcode != 1 && opcode != 2) throw invalidRecipe();
            int offset = edits.readInt();
            int count = edits.readInt();
            if (offset < 0 || count <= 0 || count > size - position || count > source.length - offset) {
                throw invalidRecipe();
            }
            if (opcode == 1) {
                System.arraycopy(source, offset, target, position, count);
            } else {
                edits.readFully(target, position, count);
                for (int i = 0; i < count; i++) target[position + i] ^= source[offset + i];
            }
            position += count;
        }
        if (position != size) throw invalidRecipe();
        return target;
    }

    private static IOException invalidArchive() {
        return new IOException("A Veil source record does not match the supported Re-Reckoning archives.");
    }

    private static IOException invalidRecipe() {
        return new IOException("The Veil menu edits did not pass verification. Try reinstalling this APK.");
    }
}
