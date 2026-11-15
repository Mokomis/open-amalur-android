package org.openamalur.originalengine;

import java.io.DataInputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

final class CompilerDownload {
    private static final String CAB_URL = "https://download.microsoft.com/download/1/7/1/"
            + "1718CCC4-6315-4D8E-9543-8E28A4E18C4C/Jun2010_D3DCompiler_43_x86.cab";
    private static final String CAB_SHA256 =
            "293ced557ad732abd2737333df39b08216f31601d7ab65b743fe51b4efb8b6f0";
    private static final int CAB_BYTES = 934679;
    private static final int FIRST_BLOCK_OFFSET = 270;
    private static final int FOLDER_BYTES = 2116055;
    static final int DLL_BYTES = 2106216;

    static byte[] fetch() throws IOException {
        byte[] cabinet = new byte[CAB_BYTES];
        HttpURLConnection connection = SetupDownload.open(CAB_URL, "download.microsoft.com");
        try (DataInputStream input = new DataInputStream(connection.getInputStream())) {
            input.readFully(cabinet);
            if (input.read() != -1) throw new IOException("Unexpected DirectX download size.");
        } finally {
            connection.disconnect();
        }
        return extract(cabinet);
    }

    // This cabinet has one MSZIP folder with the compiler as its first file.
    // Verify the exact archive before using its layout.
    static byte[] extract(byte[] cabinet) throws IOException {
        if (cabinet.length != CAB_BYTES ||
                !CAB_SHA256.equals(GameFiles.hex(GameFiles.sha256().digest(cabinet)))) {
            throw new IOException("The Microsoft shader compiler download could not be verified.");
        }
        ByteBuffer blocks = ByteBuffer.wrap(cabinet).order(ByteOrder.LITTLE_ENDIAN);
        blocks.position(FIRST_BLOCK_OFFSET);
        byte[] folder = new byte[FOLDER_BYTES];
        int offset = 0;
        Inflater inflater = new Inflater(true);
        try {
            while (offset < folder.length) {
                blocks.getInt();
                int compressed = blocks.getShort() & 0xffff;
                int unpacked = blocks.getShort() & 0xffff;
                int start = blocks.position();
                if (compressed < 2 || compressed > blocks.remaining() || unpacked <= 0 ||
                        unpacked > folder.length - offset || blocks.getShort(start) != 0x4b43) {
                    throw new IOException("Invalid Microsoft shader compiler archive.");
                }
                inflater.reset();
                int dictionary = Math.min(offset, 32768);
                if (dictionary != 0) inflater.setDictionary(folder, offset - dictionary, dictionary);
                inflater.setInput(cabinet, start + 2, compressed - 2);
                if (inflater.inflate(folder, offset, unpacked) != unpacked || !inflater.finished()) {
                    throw new IOException("Incomplete Microsoft shader compiler archive.");
                }
                offset += unpacked;
                blocks.position(start + compressed);
            }
        } catch (DataFormatException error) {
            throw new IOException("Could not extract the Microsoft shader compiler.", error);
        } finally {
            inflater.end();
        }
        return Arrays.copyOf(folder, DLL_BYTES);
    }
}
