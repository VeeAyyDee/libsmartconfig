// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.testapp;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.ScrollView;
import android.widget.TextView;
import android.view.WindowManager;
import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;

/** Separate test APK. Drives the real UI using ONLY the isolated lab's public credentials. */
public final class LabInstrumentation extends Instrumentation {
    private Bundle arguments;
    private Activity activity;
    private final List<View> views = new ArrayList<>();
    @Override public void onCreate(Bundle args) { super.onCreate(args); arguments=args; start(); }
    private void collect(View view) {
        views.add(view);
        if(view instanceof ViewGroup) for(int i=0;i<((ViewGroup)view).getChildCount();i++) collect(((ViewGroup)view).getChildAt(i));
    }
    private EditText field(String hint) {
        for(View v:views) if(v instanceof EditText && ((EditText)v).getHint().toString().startsWith(hint)) return (EditText)v;
        throw new AssertionError("Missing field: "+hint);
    }
    private Button button(String label) {
        for(View v:views) if(v instanceof Button && ((Button)v).getText().toString().equals(label)) return (Button)v;
        throw new AssertionError("Missing button: "+label);
    }
    private String status() {
        String[] value={""};
        runOnMainSync(() -> { for(View v:views) if(v instanceof TextView && v.getAccessibilityLiveRegion()==View.ACCESSIBILITY_LIVE_REGION_POLITE) value[0]=((TextView)v).getText().toString(); });
        return value[0];
    }
    @Override public void onStart() {
        Bundle output=new Bundle();
        try {
            Intent launch=new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=startActivitySync(launch); waitForIdleSync(); SystemClock.sleep(1800);
            int protocol=Integer.parseInt(arguments.getString("protocol","0")), mode=Integer.parseInt(arguments.getString("mode","0"));
            String scenario=arguments.getString("scenario","success");
            runOnMainSync(() -> {
                collect(activity.getWindow().getDecorView());
                List<Spinner> spinners=new ArrayList<>(); for(View v:views) if(v instanceof Spinner) spinners.add((Spinner)v);
                spinners.get(1).setSelection(protocol);
            });
            waitForIdleSync();
            runOnMainSync(() -> {
                List<Spinner> spinners=new ArrayList<>(); for(View v:views) if(v instanceof Spinner) spinners.add((Spinner)v);
                spinners.get(2).setSelection(mode);
            });
            waitForIdleSync();
            runOnMainSync(() -> {
                field("SSID").setText("SmartConfig-Lab"); field("BSSID").setText(arguments.getString("bssid","80:65:99:df:56:81"));
                field("Wi-Fi password").setText(scenario.equals("wrong-password") ? "WrongPass123" : "LabPass123");
                if(mode!=0) field("AES key").setText(scenario.equals("wrong-key") ? "ffffffffffffffffffffffffffffffff" : "000102030405060708090a0b0c0d0e0f");
                for(View v:views) if(v instanceof CheckBox) ((CheckBox)v).setChecked(true);
                button("Start provisioning").performClick();
            });
            if(scenario.equals("cancel")) { SystemClock.sleep(1000); runOnMainSync(() -> button("Cancel session").performClick()); }
            if(scenario.equals("background")) { SystemClock.sleep(1000); runOnMainSync(() -> activity.moveTaskToBack(true)); }
            if(scenario.equals("wifi-loss")) {
                SystemClock.sleep(700);
                try(android.os.ParcelFileDescriptor ignored=getUiAutomation().executeShellCommand("cmd wifi set-wifi-enabled disabled")) { SystemClock.sleep(500); }
            }
            long deadline=SystemClock.elapsedRealtime()+125000; String result;
            do {
                SystemClock.sleep(500); result=status();
                if(!result.contains("packets") && !result.startsWith("Opening")) break;
            } while(SystemClock.elapsedRealtime()<deadline);
            if(scenario.equals("success") && !result.contains("acknowledgement received")) throw new AssertionError(result);
            if(scenario.equals("cancel") && !result.equals("Canceled")) throw new AssertionError(result);
            if(scenario.equals("background") && !result.contains("left the foreground")) throw new AssertionError(result);
            if(scenario.equals("wifi-loss") && !result.startsWith("Selected Wi-Fi network was lost")
                && !result.startsWith("Selected Wi-Fi IPv4 address changed")) throw new AssertionError(result);
            if(scenario.startsWith("wrong-") && !result.startsWith("No response")) throw new AssertionError(result);
            runOnMainSync(() -> {
                if(field("Wi-Fi password").length()!=0 || field("AES key").length()!=0 || field("Reserved").length()!=0) throw new AssertionError("Secret field retained");
            });
            if(arguments.getString("capture","false").equals("true")) {
                // Test APK only: secrets have been cleared; capture public lab UI for layout QA.
                runOnMainSync(() -> activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_SECURE));
                for(int direction:new int[]{View.FOCUS_UP,View.FOCUS_DOWN}) {
                    runOnMainSync(() -> { for(View v:views) if(v instanceof ScrollView) ((ScrollView)v).fullScroll(direction); });
                    waitForIdleSync(); SystemClock.sleep(300);
                    Bitmap bitmap=getUiAutomation().takeScreenshot();
                    if(bitmap==null) throw new AssertionError("Screenshot unavailable");
                    try(FileOutputStream f=new FileOutputStream(new File(getTargetContext().getCacheDir(),direction==View.FOCUS_UP?"preview-top.png":"preview-bottom.png"))) {
                        bitmap.compress(Bitmap.CompressFormat.PNG,100,f);
                    }
                    bitmap.recycle();
                }
                runOnMainSync(() -> activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE));
            }
            output.putString("stream","PASS "+scenario+" protocol="+protocol+" mode="+mode+"\n"+result+"\n");
            finish(Activity.RESULT_OK,output);
        } catch(Throwable e) {
            output.putString("stream","FAIL: "+e.getClass().getSimpleName()+": "+e.getMessage()+"\n");
            finish(Activity.RESULT_CANCELED,output);
        }
    }
}
