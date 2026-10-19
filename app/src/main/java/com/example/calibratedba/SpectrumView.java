package com.example.calibratedba;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.View;

import androidx.annotation.Nullable;

public class SpectrumView extends View {
    private Paint barPaint;
    private Paint gridPaint;
    private Paint axisPaint;
    private Paint textPaint;
    private Paint legendPaint;

    private float[] magnitudes = new float[32]; // 32 cột tần số

    // Nhãn mốc tần số cho Trục Hoành (X-Axis)
    private final String[] freqLabels = {"20Hz", "100Hz", "500Hz", "1kHz", "5kHz", "10kHz", "20kHz"};
    // Nhãn mốc cường độ cho Trục Tung (Y-Axis)
    private final int[] dbLabels = {100, 75, 50, 25, 0};

    public SpectrumView(Context context) {
        super(context);
        init();
    }

    public SpectrumView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public SpectrumView(Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        // Cọ vẽ cột sóng
        barPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        barPaint.setStyle(Paint.Style.FILL);

        // Cọ vẽ đường lưới ngang
        gridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        gridPaint.setColor(Color.parseColor("#26FFFFFF"));
        gridPaint.setStrokeWidth(dpToPx(1));

        // Cọ vẽ trục tọa độ X, Y
        axisPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        axisPaint.setColor(Color.parseColor("#78909C"));
        axisPaint.setStrokeWidth(dpToPx(1.5f));

        // Cọ vẽ chữ số đơn vị trên 2 trục
        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setColor(Color.parseColor("#B0BEC5"));
        textPaint.setTextSize(spToPx(10));

        // Cọ vẽ chú thích (Legend)
        legendPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        legendPaint.setTextSize(spToPx(10.5f));
        legendPaint.setColor(Color.parseColor("#ECEFF1"));
    }

    public void updateSpectrum(float[] newMagnitudes) {
        if (newMagnitudes != null) {
            this.magnitudes = newMagnitudes;
            invalidate();
        }
    }

    public void clear() {
        for (int i = 0; i < magnitudes.length; i++) {
            magnitudes[i] = 0f;
        }
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();

        // Định nghĩa khoảng lề (Padding) để chừa chỗ ghi chữ cho các trục và Legend
        float leftPadding = dpToPx(44);   // Chừa chỗ bên trái cho trục Y (dB)
        float rightPadding = dpToPx(14);  // Chừa lề phải
        float topPadding = dpToPx(34);    // Chừa chỗ phía trên cho Chú thích màu (Legend)
        float bottomPadding = dpToPx(28); // Chừa chỗ bên dưới cho trục X (Hz)

        float chartLeft = leftPadding;
        float chartRight = width - rightPadding;
        float chartTop = topPadding;
        float chartBottom = height - bottomPadding;

        float chartWidth = chartRight - chartLeft;
        float chartHeight = chartBottom - chartTop;

        if (chartWidth <= 0 || chartHeight <= 0) return;

        // ================= 1. VẼ CHÚ THÍCH MÀU SẮC (LEGEND) Ở PHÍA TRÊN =================
        drawLegendItem(canvas, dpToPx(10), dpToPx(18), Color.parseColor("#00E676"), "An toàn (<50dB)");
        drawLegendItem(canvas, width * 0.37f, dpToPx(18), Color.parseColor("#FFD740"), "Vừa (50-80dB)");
        drawLegendItem(canvas, width * 0.70f, dpToPx(18), Color.parseColor("#FF5252"), "Ồn (>80dB)");

        // ================= 2. VẼ TRỤC TUNG (ĐƠN VỊ: dB) & ĐƯỜNG LƯỚI NGANG =================
        textPaint.setTextAlign(Paint.Align.RIGHT);
        for (int db : dbLabels) {
            // Tính tọa độ Y tương ứng với mức dB (từ 0 đến 100)
            float ratio = 1f - (db / 100f);
            float y = chartTop + ratio * chartHeight;

            // Vẽ đường lưới ngang
            canvas.drawLine(chartLeft, y, chartRight, y, gridPaint);

            // Vẽ nhãn đơn vị dB bên trái trục tung
            canvas.drawText(db + " dB", chartLeft - dpToPx(6), y + dpToPx(3), textPaint);
        }

        // ================= 3. VẼ CÁC CỘT PHỔ TẦN SỐ =================
        if (magnitudes != null && magnitudes.length > 0) {
            int numBars = magnitudes.length;
            float barWidth = chartWidth / numBars;
            float spacing = barWidth * 0.22f;

            for (int i = 0; i < numBars; i++) {
                float val = Math.max(0f, Math.min(100f, magnitudes[i]));
                float barHeight = (val / 100f) * chartHeight;

                float left = chartLeft + i * barWidth + (spacing / 2);
                float top = chartBottom - barHeight;
                float right = chartLeft + (i + 1) * barWidth - (spacing / 2);

                // Phân loại màu sắc theo ngưỡng cường độ âm thanh
                if (val > 80) {
                    barPaint.setColor(Color.parseColor("#FF5252")); // Đỏ: Quá ồn
                } else if (val > 50) {
                    barPaint.setColor(Color.parseColor("#FFD740")); // Vàng: Trung bình
                } else {
                    barPaint.setColor(Color.parseColor("#00E676")); // Xanh lá: An toàn
                }

                canvas.drawRect(left, top, right, chartBottom, barPaint);
            }
        }

        // ================= 4. VẼ KHUNG TRỤC TỌA ĐỘ X - Y =================
        // Đường trục Tung (Y)
        canvas.drawLine(chartLeft, chartTop, chartLeft, chartBottom, axisPaint);
        // Đường trục Hoành (X)
        canvas.drawLine(chartLeft, chartBottom, chartRight, chartBottom, axisPaint);

        // ================= 5. VẼ TRỤC HOÀNH (ĐƠN VỊ: TẦN SỐ Hz / kHz) =================
        textPaint.setTextAlign(Paint.Align.CENTER);
        int labelCount = freqLabels.length;
        for (int i = 0; i < labelCount; i++) {
            float x = chartLeft + (i * chartWidth / (labelCount - 1));
            // Vẽ vạch chia nhỏ trên trục hoành
            canvas.drawLine(x, chartBottom, x, chartBottom + dpToPx(4), axisPaint);
            // Vẽ chữ tần số (20Hz -> 20kHz)
            canvas.drawText(freqLabels[i], x, chartBottom + dpToPx(16), textPaint);
        }
    }

    // Hàm phụ trợ vẽ ô vuông màu + chữ giải thích trên thanh Legend
    private void drawLegendItem(Canvas canvas, float x, float y, int color, String label) {
        legendPaint.setColor(color);
        float boxSize = dpToPx(9);
        canvas.drawRect(x, y - boxSize + dpToPx(1), x + boxSize, y + dpToPx(1), legendPaint);

        legendPaint.setColor(Color.parseColor("#ECEFF1"));
        canvas.drawText(label, x + boxSize + dpToPx(4), y, legendPaint);
    }

    private float dpToPx(float dp) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, getResources().getDisplayMetrics());
    }

    private float spToPx(float sp) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sp, getResources().getDisplayMetrics());
    }
}