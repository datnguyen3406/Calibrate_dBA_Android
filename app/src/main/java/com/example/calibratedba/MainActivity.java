package com.example.calibratedba;

import android.Manifest;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.util.Locale;
import java.util.Random;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_RECORD_AUDIO_PERMISSION = 200;
    private static final int SAMPLE_RATE = 44100; // Tần số lấy mẫu chuẩn 44.1kHz
    private static final int NUM_SPECTRUM_BARS = 32;

    // UI Components
    private TextView tvDbaValue, tvMinMax, tvStatus, tvOffset;
    private SpectrumView spectrumView;
    private Button btnStart, btnStop, btnPlusOffset, btnMinusOffset;

    // Thu âm & Luồng (Thread)
    private AudioRecord audioRecord;
    private Thread recordingThread;
    private volatile boolean isRecording = false; // Flag an toàn giữa các luồng

    // Thông số đo lường
    private double calibrationOffset = 0.0;
    private double minDba = Double.MAX_VALUE;
    private double maxDba = 0.0;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        initViews();
        setupListeners();
    }

    private void initViews() {
        tvDbaValue = findViewById(R.id.tvDbaValue);
        tvMinMax = findViewById(R.id.tvMinMax);
        tvStatus = findViewById(R.id.tvStatus);
        tvOffset = findViewById(R.id.tvOffset);
        spectrumView = findViewById(R.id.spectrumView);
        btnStart = findViewById(R.id.btnStart);
        btnStop = findViewById(R.id.btnStop);
        btnPlusOffset = findViewById(R.id.btnPlusOffset);
        btnMinusOffset = findViewById(R.id.btnMinusOffset);
    }

    private void setupListeners() {
        // Nút Bắt đầu
        btnStart.setOnClickListener(v -> {
            if (checkAudioPermission()) {
                startRecording();
            } else {
                requestAudioPermission();
            }
        });

        // Nút Dừng
        btnStop.setOnClickListener(v -> stopRecording());

        // Nút chỉnh Offset (+)
        btnPlusOffset.setOnClickListener(v -> {
            calibrationOffset += 0.5;
            updateOffsetText();
        });

        // Nút chỉnh Offset (-)
        btnMinusOffset.setOnClickListener(v -> {
            calibrationOffset -= 0.5;
            updateOffsetText();
        });
    }

    private void updateOffsetText() {
        tvOffset.setText(String.format(Locale.US, "Hiệu chuẩn (Offset): %+.1f dB", calibrationOffset));
    }

    // ================= XỬ LÝ QUYỀN TRUY CẬP MICROPHONE =================
    private boolean checkAudioPermission() {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAudioPermission() {
        ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.RECORD_AUDIO},
                REQUEST_RECORD_AUDIO_PERMISSION);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_RECORD_AUDIO_PERMISSION) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                startRecording();
            } else {
                Toast.makeText(this, "Cần cấp quyền Microphone để đo độ ồn!", Toast.LENGTH_SHORT).show();
            }
        }
    }

    // ================= THU ÂM PCM TRÊN BACKGROUND THREAD =================
    private void startRecording() {
        if (isRecording) return;

        int minBufferSize = AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT
        );

        if (minBufferSize == AudioRecord.ERROR || minBufferSize == AudioRecord.ERROR_BAD_VALUE) {
            Toast.makeText(this, "Thiết bị không hỗ trợ cấu hình thu âm này!", Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            // Dùng VOICE_RECOGNITION để lấy âm thanh thô ít bị hãng can thiệp
            audioRecord = new AudioRecord(
                    MediaRecorder.AudioSource.VOICE_RECOGNITION,
                    SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    minBufferSize
            );
        } catch (SecurityException e) {
            return;
        }

        if (audioRecord.getState() != AudioRecord.STATE_INITIALIZED) {
            Toast.makeText(this, "Khởi tạo AudioRecord thất bại!", Toast.LENGTH_SHORT).show();
            return;
        }

        audioRecord.startRecording();
        isRecording = true;

        // Reset lại Min/Max mỗi lần bắt đầu mới
        minDba = Double.MAX_VALUE;
        maxDba = 0.0;

        // Cập nhật trạng thái nút bấm
        btnStart.setEnabled(false);
        btnStop.setEnabled(true);
        tvStatus.setText("Trạng thái: Đang thu âm & phân tích (Recording)...");

        // Khởi tạo Background Thread để đọc PCM liên tục
        recordingThread = new Thread(() -> {
            short[] pcmBuffer = new short[minBufferSize / 2];

            while (isRecording) {
                int readSize = audioRecord.read(pcmBuffer, 0, pcmBuffer.length);
                if (readSize > 0) {
                    processAudioData(pcmBuffer, readSize);
                }
            }
        }, "AudioRecorderThread");

        recordingThread.start();
    }

    // ================= XỬ LÝ DỮ LIỆU & DỮ LIỆU GIẢ LẬP (MOCK DATA) =================
    private void processAudioData(short[] pcmData, int readSize) {
        /*
         * TODO: KHI CÓ CODE C++ (JNI), CHỈ CẦN GỌI HÀM NATIVE TẠI ĐÂY:
         * double rawDba = nativeCalculateDBA(pcmData, readSize);
         * float[] spectrum = nativeCalculateFFT(pcmData, readSize);
         */

        // --- PHẦN DỮ LIỆU GIẢ LẬP ĐỂ TEST GIAO DIỆN ---
        // Tính bộ biên độ âm thanh thực từ mảng PCM để số nhảy theo tiếng nói thật khi test
        double sumSquares = 0;
        for (int i = 0; i < readSize; i++) {
            sumSquares += pcmData[i] * pcmData[i];
        }
        double rms = Math.sqrt(sumSquares / readSize);

        // Giả lập công thức đổi sang dBA + cộng với hệ số hiệu chuẩn (Offset)
        double mockDba = (rms > 1) ? (20 * Math.log10(rms) - 10) : 30.0;
        double finalDba = Math.max(25.0, Math.min(120.0, mockDba + calibrationOffset));

        // Tạo mảng 32 cột tần số giả lập dựa theo độ lớn âm thanh
        float[] mockSpectrum = new float[NUM_SPECTRUM_BARS];
        for (int i = 0; i < NUM_SPECTRUM_BARS; i++) {
            float baseLevel = (float) ((finalDba - 25.0) * 0.8);
            mockSpectrum[i] = Math.max(5f, baseLevel + random.nextInt(35) - 15);
        }
        // --- KẾT THÚC PHẦN DỮ LIỆU GIẢ LẬP ---

        // Cập nhật Min / Max
        if (finalDba < minDba) minDba = finalDba;
        if (finalDba > maxDba) maxDba = finalDba;
        // Đẩy kết quả về Main Thread (UI Thread) để vẽ giao diện
        mainHandler.post(() -> {
            if (!isRecording) return; // Tránh cập nhật trễ sau khi đã bấm Dừng
            tvDbaValue.setText(String.format(Locale.US, "%.1f dBA", finalDba));
            tvMinMax.setText(String.format(Locale.US, "Min: %.1f dBA   |   Max: %.1f dBA", minDba, maxDba));
            spectrumView.updateSpectrum(mockSpectrum);
        });
    }

    // ================= DỪNG THU ÂM & QUẢN LÝ VÒNG ĐỜI =================
    private void stopRecording() {
        isRecording = false;

        // Chờ luồng background kết thúc an toàn
        if (recordingThread != null) {
            try {
                recordingThread.join(300);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            recordingThread = null;
        }

        // Giải phóng AudioRecord để trả lại micro cho hệ thống
        if (audioRecord != null) {
            try {
                if (audioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                    audioRecord.stop();
                }
            } catch (IllegalStateException ignored) {}
            audioRecord.release();
            audioRecord = null;
        }

        // Cập nhật lại UI
        btnStart.setEnabled(true);
        btnStop.setEnabled(false);
        tvStatus.setText("Trạng thái: Đã dừng");
        spectrumView.clear();
    }

    @Override
    protected void onStop() {
        super.onStop();
        // Tự động dừng thu âm khi người dùng ẩn app xuống nền (bấm nút Home / chuyển app)
        if (isRecording) {
            stopRecording();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopRecording();
    }
}