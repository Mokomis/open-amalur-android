package org.openamalur.originalengine;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Arrays;
import java.util.Locale;

final class SetupDownload {
    static HttpURLConnection open(String address, String... allowedHosts) throws IOException {
        URL url = new URL(address);
        for (int redirect = 0; redirect <= 5; redirect++) {
            if (!"https".equals(url.getProtocol()) || url.getUserInfo() != null ||
                    !Arrays.asList(allowedHosts).contains(url.getHost().toLowerCase(Locale.ROOT))) {
                throw new IOException("Unexpected setup download address.");
            }
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            connection.setRequestProperty("Accept-Encoding", "identity");
            boolean connected = false;
            try {
                int status = connection.getResponseCode();
                if (status == HttpURLConnection.HTTP_OK) {
                    connected = true;
                    return connection;
                }
                String location = connection.getHeaderField("Location");
                if (status < 300 || status >= 400 || location == null) {
                    throw new IOException("Setup download failed (HTTP " + status + ").");
                }
                url = new URL(url, location);
            } finally {
                if (!connected) connection.disconnect();
            }
        }
        throw new IOException("Too many setup download redirects.");
    }
}
