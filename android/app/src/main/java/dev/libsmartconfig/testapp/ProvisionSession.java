// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.testapp;

import android.net.Network;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiManager;
import android.os.SystemClock;
import dev.libsmartconfig.codec.Acknowledgement;
import dev.libsmartconfig.codec.Protocol;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

/** One foreground session; both sockets are bound to the same explicit Wi-Fi network. */
final class ProvisionSession implements Runnable {
    interface Listener {
        void progress(int packets, long remainingSeconds);
        void finished(String result);
    }
    private final Network network;
    private final WifiManager wifi;
    private final ConnectivityManager connectivity;
    private final int[] lengths;
    private final int mark;
    private final Protocol protocol;
    private final Listener listener;
    private final AtomicReference<String> outcome = new AtomicReference<>();
    private volatile DatagramSocket sender, receiver;
    private final Thread worker;

    ProvisionSession(Network network, WifiManager wifi, ConnectivityManager connectivity, int[] lengths, Protocol protocol, int mark, Listener listener) {
        this.network = network; this.wifi = wifi; this.lengths = lengths; this.mark = mark; this.listener = listener;
        this.connectivity = connectivity;
        this.protocol = protocol;
        worker = new Thread(this, "smartconfig-session");
    }
    void start() { worker.start(); }
    void cancel(String reason) {
        outcome.compareAndSet(null, reason);
        DatagramSocket s = sender, r = receiver;
        if (s != null) s.close(); if (r != null) r.close();
        worker.interrupt();
    }
    @Override public void run() {
        WifiManager.MulticastLock lock = null;
        try {
            lock = wifi.createMulticastLock("smartconfig-ack");
            lock.setReferenceCounted(false); lock.acquire();
            receiver = new DatagramSocket(null);
            network.bindSocket(receiver);
            receiver.bind(new InetSocketAddress(protocol.replyPort(mark)));
            sender = new DatagramSocket(null);
            network.bindSocket(sender);
            sender.bind(new InetSocketAddress(0));
            // Experimental administratively scoped multicast; no raw-frame padding.
            InetAddress destination = InetAddress.getByAddress(new byte[]{(byte)239, (byte)255, 0, 1});
            byte[] payload = new byte[1112];
            byte[] ack = new byte[2048]; // A larger buffer lets us reject overlong replies, not truncate to 7.
            DatagramPacket incoming = new DatagramPacket(ack, ack.length);
            long deadline = SystemClock.elapsedRealtime() + 120_000;
            long nextSend = 0, nextUpdate = 0;
            int sent = 0;
            while (outcome.get() == null) {
                long now = SystemClock.elapsedRealtime();
                if (now >= deadline) { outcome.compareAndSet(null, "No response within 120 seconds. Check the key, password, AP band, and firmware; reboot the board before retrying."); break; }
                if (now >= nextSend) {
                    sender.send(new DatagramPacket(payload, lengths[sent % lengths.length], destination, 7001));
                    sent++;
                    nextSend = SystemClock.elapsedRealtime() + 20;
                }
                if (now >= nextUpdate) { listener.progress(sent, Math.max(0, (deadline - now + 999) / 1000)); nextUpdate = now + 1000; }
                receiver.setSoTimeout((int)Math.max(1, Math.min(nextSend - SystemClock.elapsedRealtime(), deadline - SystemClock.elapsedRealtime())));
                try {
                    incoming.setLength(ack.length); receiver.receive(incoming);
                    String mac = Acknowledgement.parse(protocol, ack, incoming.getLength(), mark, incoming.getAddress().getAddress());
                    if (mac != null && incoming.getAddress() instanceof Inet4Address
                        && !incoming.getAddress().isAnyLocalAddress() && !incoming.getAddress().isMulticastAddress()
                        && !incoming.getAddress().isLoopbackAddress()) {
                        // First valid reply ends the session: repeats cannot produce duplicate results.
                        outcome.compareAndSet(null, protocol.label + " acknowledgement received\n" + (protocol == Protocol.AIRKISS ? mac : "Device MAC: " + mac)
                            + "\nSource IP: " + incoming.getAddress().getHostAddress()
                            + "\nThis reply is not cryptographic device authentication.");
                    }
                } catch (SocketTimeoutException expected) { /* Continue paced transmission. */ }
            }
        } catch (SecurityException e) {
            outcome.compareAndSet(null, "Permission error. Allow local network / nearby device access in App settings, then retry.");
        } catch (IOException e) {
            // A link teardown can fail the socket before Android dispatches onLost.
            // Allow the callback/state update a short bounded window to classify it.
            long grace = SystemClock.elapsedRealtime() + 250;
            while (outcome.get() == null && SystemClock.elapsedRealtime() < grace) {
                NetworkCapabilities caps = connectivity.getNetworkCapabilities(network);
                if (wifi.getWifiState() != WifiManager.WIFI_STATE_ENABLED || caps == null
                    || !caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                    outcome.compareAndSet(null, "Selected Wi-Fi network was lost. Reconnect and retry.");
                    break;
                }
                try { Thread.sleep(25); } catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); break; }
            }
            outcome.compareAndSet(null, socketError(e));
        } catch (Exception e) {
            outcome.compareAndSet(null, socketError(e));
        } finally {
            if (sender != null) sender.close(); if (receiver != null) receiver.close();
            if (lock != null && lock.isHeld()) lock.release();
            Arrays.fill(lengths, 0);
            listener.finished(outcome.get() == null ? "Session ended" : outcome.get());
        }
    }
    private static String socketError(Exception e) {
        return "Socket error (" + e.getClass().getSimpleName()
            + "). Check Wi-Fi, local network permissions, VPN policy, and whether another sender occupies the reply port.";
    }
}
