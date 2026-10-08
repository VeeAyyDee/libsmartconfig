// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.testapp;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.Uri;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import dev.libsmartconfig.codec.Touch2Encoder;
import dev.libsmartconfig.codec.LegacyEncoder;
import dev.libsmartconfig.codec.Protocol;
import java.net.Inet4Address;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class MainActivity extends Activity {
    private ConnectivityManager connectivity;
    private WifiManager wifi;
    private ConnectivityManager.NetworkCallback callback;
    private final Map<Network, NetworkCapabilities> networks = new LinkedHashMap<>();
    private final List<Network> choices = new ArrayList<>();
    private final List<View> inputs = new ArrayList<>();
    private Spinner networkPicker, protocolPicker, mode;
    private TextView networkInfo, status;
    private EditText ssid, password, bssid, reserved, key;
    private CheckBox confirmed;
    private Button start, cancel;
    private LinearLayout root;
    private ProvisionSession session;
    private Network sessionNetwork;
    private String sessionBssid;
    private byte[] sessionIpv4;
    private boolean foreground;
    private final SecureRandom random = new SecureRandom();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        connectivity = getSystemService(ConnectivityManager.class);
        wifi = getApplicationContext().getSystemService(WifiManager.class);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true);
        root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(28), dp(24), dp(24)); scroll.addView(root);
        // Target 35+ enforces edge-to-edge. Keep controls clear of system bars and keyboard.
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(scroll);
        text("LIBSMARTCONFIG / ANDROID LAB", 12, 0xff6ee7b7);
        text("Provision a device", 30, Color.WHITE);
        text("ESP-Touch v1 / v2 + AirKiss · IPv4", 14, 0xffa7bac8);
        text("1  Connect to Wi-Fi", 20, Color.WHITE);
        text("Connect this phone to the target 2.4 GHz access point. Reboot the ESP32-S3 provisioning example before each attempt.", 14, 0xffa7bac8);
        button("Open Wi-Fi settings", () -> startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS)), true);
        button("Allow permissions / refresh networks", this::permissions, true);
        button("App permission settings", () -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + getPackageName()))), true);
        networkPicker = spinner(new String[]{"Waiting for Wi-Fi…"});
        networkInfo = text("Select a Wi-Fi network. Mobile data is never used for the session.", 14, 0xffa7bac8);
        networkPicker.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) { describeNetwork(); }
            public void onNothingSelected(AdapterView<?> parent) { }
        });
        text("2  Check credentials", 20, Color.WHITE);
        ssid = field("SSID · 1–32 UTF-8 bytes", false);
        bssid = field("BSSID · AA:BB:CC:DD:EE:FF", false);
        password = field("Wi-Fi password · 0–64 UTF-8 bytes", true);
        protocolPicker = spinner(new String[]{"ESP-Touch v2", "ESP-Touch v1", "AirKiss"});
        mode = spinner(new String[]{"Plaintext · firmware default", "AES security 2 · fresh IV", "AES security 1 · legacy zero IV"});
        key = field("AES key · exactly 32 hex digits", true);
        key.setVisibility(View.GONE);
        mode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                key.setVisibility(pos == 0 ? View.GONE : View.VISIBLE);
                if (pos == 0) key.setText("");
            }
            public void onNothingSelected(AdapterView<?> p) { }
        });
        reserved = field("Reserved text · optional, 0–64 UTF-8 bytes", true);
        protocolPicker.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                mode.setAdapter(adapter(Arrays.asList(pos == 0 ? new String[]{"Plaintext · firmware default", "AES security 2 · fresh IV", "AES security 1 · legacy zero IV"}
                    : pos == 1 ? new String[]{"Plaintext"} : new String[]{"Plaintext", "AES · key is IV"})));
                mode.setSelection(0); key.setText("");
                reserved.setText(""); reserved.setVisibility(pos == 0 ? View.VISIBLE : View.GONE);
            }
            public void onNothingSelected(AdapterView<?> p) { }
        });
        confirmed = new CheckBox(this);
        confirmed.setText("I checked the selected network, exact SSID/BSSID, and 2.4 GHz band.");
        root.addView(confirmed); inputs.add(confirmed);
        text("3  Send and wait for a reply", 20, Color.WHITE);
        text("Experimental transport: 20 ms packet spacing, up to 120 seconds. Keep this screen open. Credentials are not saved. CBC and the reply do not authenticate a device.", 14, 0xffa7bac8);
        start = button("Start provisioning", this::begin, false);
        cancel = button("Cancel session", () -> stop("Canceled"), false); cancel.setEnabled(false);
        status = text("Ready · select Wi-Fi and enter credentials", 16, Color.WHITE);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    }

    private int dp(int v) { return (int)(v * getResources().getDisplayMetrics().density + .5f); }
    private TextView text(String value, int size, int color) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color);
        v.setPadding(0, dp(12), 0, dp(8)); root.addView(v); return v;
    }
    private Button button(String title, Runnable action, boolean input) {
        Button b = new Button(this); b.setText(title); b.setAllCaps(false); b.setOnClickListener(v -> action.run());
        root.addView(b); if (input) inputs.add(b); return b;
    }
    private EditText field(String hint, boolean secret) {
        EditText e = new EditText(this); e.setHint(hint); e.setSingleLine(true); e.setTextSize(16);
        e.setInputType(InputType.TYPE_CLASS_TEXT | (secret ? InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS));
        e.setSaveEnabled(false); e.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        root.addView(e); inputs.add(e); return e;
    }
    private Spinner spinner(String[] values) {
        Spinner s = new Spinner(this); s.setAdapter(adapter(Arrays.asList(values))); root.addView(s); inputs.add(s); return s;
    }
    private ArrayAdapter<String> adapter(List<String> values) {
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, values);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); return a;
    }
    private boolean granted(String p) { return checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED; }
    private void permissions() {
        List<String> needed = new ArrayList<>();
        if (!granted(Manifest.permission.ACCESS_FINE_LOCATION)) {
            needed.add(Manifest.permission.ACCESS_COARSE_LOCATION); needed.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        if (Build.VERSION.SDK_INT >= 33 && !granted(Manifest.permission.NEARBY_WIFI_DEVICES)) needed.add(Manifest.permission.NEARBY_WIFI_DEVICES);
        if (Build.VERSION.SDK_INT >= 37 && !granted("android.permission.ACCESS_LOCAL_NETWORK")) needed.add("android.permission.ACCESS_LOCAL_NETWORK");
        if (needed.isEmpty()) registerNetworks();
        else {
            status.setText("Location permission and enabled Location services allow Android to reveal SSID/BSSID. Nearby/local network permission allows LAN traffic. You can enter hidden metadata manually.");
            requestPermissions(needed.toArray(new String[0]), 1);
        }
    }
    @Override public void onRequestPermissionsResult(int code, String[] p, int[] results) {
        super.onRequestPermissionsResult(code, p, results);
        boolean denied = false; for (int r : results) denied |= r != PackageManager.PERMISSION_GRANTED;
        status.setText(denied ? "Some permissions were denied. Retry above or use App permission settings. Hidden SSID/BSSID may be entered manually; LAN denial can prevent sending/receiving." : "Permissions granted. Check the selected Wi-Fi network.");
        if (foreground) registerNetworks();
    }
    @Override protected void onStart() { super.onStart(); foreground = true; registerNetworks(); }
    @Override protected void onStop() {
        foreground = false; stop("Canceled because the app left the foreground"); unregisterNetworks();
        clearSecrets(); super.onStop();
    }
    private void unregisterNetworks() {
        if (callback != null) { connectivity.unregisterNetworkCallback(callback); callback = null; }
        networks.clear(); choices.clear();
    }
    private void registerNetworks() {
        unregisterNetworks();
        class WifiCallback extends ConnectivityManager.NetworkCallback {
            WifiCallback() { super(); }
            @android.annotation.TargetApi(31)
            WifiCallback(int flags) { super(flags); }
            @Override public void onCapabilitiesChanged(Network n, NetworkCapabilities caps) {
                runOnUiThread(() -> {
                    if (!foreground || callback != this) return;
                    networks.put(n, caps);
                    if (session != null && n.equals(sessionNetwork)) {
                        WifiInfo info = info(caps);
                        String current = info == null ? null : realBssid(info.getBSSID());
                        if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                            || !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
                            || (current != null && !current.equalsIgnoreCase(sessionBssid))
                            || (info != null && info.getFrequency() > 2500)) stop("Wi-Fi network changed. Check the selected access point and retry.");
                    }
                    if (session == null) updateNetworks();
                });
            }
            @Override public void onLinkPropertiesChanged(Network n, LinkProperties properties) {
                runOnUiThread(() -> {
                    if (callback != this) return;
                    if (session != null && n.equals(sessionNetwork) && !Arrays.equals(sessionIpv4, ipv4Bytes(properties)))
                        stop("Selected Wi-Fi IPv4 address changed or was lost. Retry on the current network.");
                });
            }
            @Override public void onLost(Network n) {
                runOnUiThread(() -> {
                    if (callback != this) return;
                    networks.remove(n);
                    if (session != null && n.equals(sessionNetwork)) stop("Selected Wi-Fi network was lost. Reconnect and retry.");
                    if (session == null) updateNetworks();
                });
            }
        }
        callback = Build.VERSION.SDK_INT >= 31 ? new WifiCallback(ConnectivityManager.NetworkCallback.FLAG_INCLUDE_LOCATION_INFO) : new WifiCallback();
        try {
            connectivity.registerNetworkCallback(new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN).build(), callback);
            updateNetworks();
        } catch (SecurityException e) { callback = null; status.setText("Network access permission unavailable. Retry permissions or open App settings."); }
    }
    private void updateNetworks() {
        Network previous = selected(); choices.clear(); choices.addAll(networks.keySet());
        List<String> labels = new ArrayList<>();
        for (Network n : choices) labels.add("Wi-Fi network " + n);
        if (labels.isEmpty()) labels.add("No Wi-Fi network available");
        networkPicker.setAdapter(adapter(labels));
        int index = choices.indexOf(previous); if (index >= 0) networkPicker.setSelection(index);
        describeNetwork();
    }
    private Network selected() {
        int i = networkPicker.getSelectedItemPosition(); return i >= 0 && i < choices.size() ? choices.get(i) : null;
    }
    private WifiInfo info(NetworkCapabilities caps) {
        if (caps == null) return null;
        if (Build.VERSION.SDK_INT >= 29 && caps.getTransportInfo() instanceof WifiInfo) return (WifiInfo)caps.getTransportInfo();
        // The callback WifiInfo replacement for getConnectionInfo arrived in Android 12.
        // Older phones may expose no transport info, but have a single connected STA.
        return Build.VERSION.SDK_INT < 31 ? wifi.getConnectionInfo() : null;
    }
    private String realBssid(String b) {
        if (b == null) return null;
        try { Touch2Encoder.mac(b); return b; } catch (IllegalArgumentException e) { return null; }
    }
    private String realSsid(WifiInfo info) {
        String s = info == null ? null : info.getSSID();
        if (s == null || s.equals(WifiManager.UNKNOWN_SSID)) return null;
        return s.startsWith("\"") && s.endsWith("\"") ? s.substring(1, s.length()-1) : null;
    }
    private void describeNetwork() {
        if (ssid == null) return;
        confirmed.setChecked(false);
        Network n = selected(); WifiInfo i = info(networks.get(n));
        String s = realSsid(i), b = i == null ? null : realBssid(i.getBSSID());
        if (s != null) ssid.setText(s); if (b != null) bssid.setText(b);
        networkInfo.setText(n == null ? "Connect to a 2.4 GHz Wi-Fi network, then refresh." :
            "Selected network " + n + "\n" + (b == null ? "SSID/BSSID hidden: allow precise Location and enable Location services, or enter and verify them manually." : "BSSID: " + b)
            + "\nBand: " + (i == null || i.getFrequency() <= 0 ? "unknown — verify 2.4 GHz manually" : i.getFrequency() + " MHz")
            + "\nIPv4: " + (ipv4(connectivity.getLinkProperties(n)) ? "available" : "unavailable"));
    }
    private static boolean ipv4(LinkProperties p) {
        return ipv4Bytes(p) != null;
    }
    private static byte[] ipv4Bytes(LinkProperties p) {
        if (p != null) for (LinkAddress a : p.getLinkAddresses()) if (a.getAddress() instanceof Inet4Address && !a.getAddress().isLinkLocalAddress()) return a.getAddress().getAddress();
        return null;
    }
    private void begin() {
        if (session != null) return;
        byte[] s = null, p = null, r = null, k = null;
        int[] lengths = null;
        try {
            if (Build.VERSION.SDK_INT >= 37 && !granted("android.permission.ACCESS_LOCAL_NETWORK"))
                throw new IllegalArgumentException("Local network permission is required. Tap Allow permissions / refresh networks, or enable it in App permission settings.");
            Network n = selected(); NetworkCapabilities caps = n == null ? null : connectivity.getNetworkCapabilities(n);
            if (caps == null || !caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN))
                throw new IllegalArgumentException("Select an available Wi-Fi network");
            if (!ipv4(connectivity.getLinkProperties(n))) throw new IllegalArgumentException("The selected Wi-Fi needs an IPv4 address");
            WifiInfo i = info(networks.get(n));
            if (i != null && i.getFrequency() > 2500) throw new IllegalArgumentException("This phone is on a 5/6 GHz radio. Connect to 2.4 GHz first");
            if (!confirmed.isChecked()) throw new IllegalArgumentException("Check the network, BSSID and 2.4 GHz confirmation first");
            String b = bssid.getText().toString(); byte[] mac = Touch2Encoder.mac(b);
            String actualBssid = i == null ? null : realBssid(i.getBSSID());
            if (actualBssid != null && !b.equalsIgnoreCase(actualBssid)) throw new IllegalArgumentException("BSSID does not match the selected Wi-Fi radio");
            String actualSsid = realSsid(i);
            if (actualSsid != null && !actualSsid.contentEquals(ssid.getText())) throw new IllegalArgumentException("SSID does not match the selected network");
            s = Touch2Encoder.utf8(ssid.getText()); p = Touch2Encoder.utf8(password.getText()); r = Touch2Encoder.utf8(reserved.getText());
            for (byte v : s) if (v == 0) throw new IllegalArgumentException("Firmware Wi-Fi credentials cannot contain NUL bytes");
            for (byte v : p) if (v == 0) throw new IllegalArgumentException("Firmware Wi-Fi credentials cannot contain NUL bytes");
            int security = mode.getSelectedItemPosition() == 1 ? 2 : mode.getSelectedItemPosition() == 2 ? 1 : 0;
            if (security != 0) k = Touch2Encoder.key(key.getText().toString());
            Protocol protocol = Protocol.values()[protocolPicker.getSelectedItemPosition()];
            int mark = protocol == Protocol.TOUCH2 ? random.nextInt(4) : protocol == Protocol.AIRKISS ? random.nextInt(256) : 9 + s.length + p.length;
            lengths = protocol == Protocol.TOUCH2 ? Touch2Encoder.encode(s, p, r, mac, security, k, mark, random)
                : protocol == Protocol.TOUCH1 ? LegacyEncoder.touch1(s, p, mac, ipv4Bytes(connectivity.getLinkProperties(n)))
                : LegacyEncoder.airkiss(s, p, mark, k);
            sessionNetwork = n; sessionBssid = b; sessionIpv4 = ipv4Bytes(connectivity.getLinkProperties(n));
            session = new ProvisionSession(n, wifi, connectivity, lengths, protocol, mark, new ProvisionSession.Listener() {
                public void progress(int count, long seconds) { runOnUiThread(() -> {
                    if (session != null && foreground) status.setText(protocol.label + " · " + count + " packets\n" + seconds + " seconds left · listening on UDP " + protocol.replyPort(mark));
                }); }
                public void finished(String result) { runOnUiThread(() -> {
                    session = null; sessionNetwork = null; sessionBssid = null; sessionIpv4 = null;
                    clearSecrets(); busy(false); status.setText(result);
                    if (foreground) updateNetworks();
                }); }
            });
            lengths = null; clearSecrets(); busy(true); status.setText("Opening acknowledgement listener…"); session.start();
        } catch (IllegalArgumentException | SecurityException e) { status.setText(e.getMessage()); }
        finally {
            for (byte[] b : new byte[][]{s, p, r, k}) if (b != null) Arrays.fill(b, (byte)0);
            if (lengths != null) Arrays.fill(lengths, 0);
        }
    }
    private void clearSecrets() { password.setText(""); key.setText(""); reserved.setText(""); }
    private void busy(boolean b) {
        for (View v : inputs) v.setEnabled(!b); start.setEnabled(!b); cancel.setEnabled(b);
        if (b) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private void stop(String reason) { if (session != null) { session.cancel(reason); cancel.setEnabled(false); } }
}
