package org.openamalur.originalengine;

import android.app.AlertDialog;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;

/** Gives app prompts the same dark and gold treatment as Veil. */
final class VeilDialogs {
    private VeilDialogs() {}

    static AlertDialog show(AlertDialog.Builder builder) {
        return show(builder, null);
    }

    static AlertDialog show(AlertDialog.Builder builder, View keyboardField) {
        AlertDialog dialog = builder.create();
        Window window = dialog.getWindow();
        if (window != null) {
            float density = dialog.getContext().getResources().getDisplayMetrics().density;
            GradientDrawable panel = new GradientDrawable();
            panel.setColor(0xff101718);
            panel.setStroke(Math.max(1, Math.round(density)), 0xffaa9365);
            window.setBackgroundDrawable(panel);
            if (keyboardField != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                window.setDimAmount(1f);
                window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            }
        }
        if (keyboardField != null) keyboardField.requestFocus();
        dialog.show();
        TextView title = dialog.findViewById(
                dialog.getContext().getResources().getIdentifier("alertTitle", "id", "android"));
        if (title != null) title.setTextColor(0xffe5d3ad);
        TextView message = dialog.findViewById(android.R.id.message);
        if (message != null) message.setTextColor(0xffe7e2d8);
        ListView list = dialog.getListView();
        if (list != null) {
            list.setDividerHeight(1);
            list.setDivider(new android.graphics.drawable.ColorDrawable(0xff635a44));
            for (int i = 0; i < list.getChildCount(); i++) {
                View child = list.getChildAt(i);
                if (child instanceof TextView) ((TextView) child).setTextColor(0xffeee9df);
            }
        }
        for (int id : new int[]{AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE,
                AlertDialog.BUTTON_NEUTRAL}) {
            Button button = dialog.getButton(id);
            if (button == null) continue;
            button.setTextColor(0xffd7bd7b);
            button.setAllCaps(false);
        }
        return dialog;
    }

    static void styleButton(Button button) {
        float density = button.getResources().getDisplayMetrics().density;
        GradientDrawable background = new GradientDrawable();
        background.setColor(0xff192222);
        background.setStroke(Math.max(1, Math.round(density)), 0xffaa9365);
        button.setBackground(background);
        button.setTextColor(0xffece5d3);
        button.setAllCaps(false);
        button.setPadding(Math.round(16 * density), Math.round(9 * density),
                Math.round(16 * density), Math.round(9 * density));
    }
}
