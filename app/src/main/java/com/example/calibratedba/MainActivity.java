package com.example.calibratedba;

import android.Manifest;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
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

public class MainActivity extends AppCompatActivity {

    // 1. Nạp thư viện C++ (tên thư viện khớp với tên trong CMakeLists.txt)
    static {
        System.loadLibrary("calibratedba");
    }

    // 2. Khai báo 2 hàm native
    public native double[] nativeCalculateNoiseMetrics(short[] pcmData, int readSize, int sampleRate, double calibrationOffsetDbA);
    public native float[] nativeCalculateFFT(short[] pcmData, int readSize, int numBars);

    private static final int REQUEST_RECORD_AUDIO_PERMISSION = 200;
    private static final int SAMPLE_RATE = 44100; // Tần số lấy mẫu chuẩn 44.1kHz
    private static final int NUM_SPECTRUM_BARS = 32;

    // UI Components
    private TextView tvDbaValue, tvMinMax, tvStatus, tvOffset;
    private SpectrumView spectrumView;
    private Button btnStartStop, btnPlusOffset, btnMinusOffset;

    // Thu âm & Luồng (Thread)
    private AudioRecord audioRecord;
    private Thread recordingThread;
    private volatile boolean isRecording = false; // Flag an toàn giữa các luồng

    // Thông số đo lường
    private volatile double calibrationOffset = 0.0;
    private volatile double minDba = Double.MAX_VALUE;
    private volatile double maxDba = -Double.MAX_VALUE;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

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
        btnStartStop = findViewById(R.id.btnStartStop);
        btnPlusOffset = findViewById(R.id.btnPlusOffset);
        btnMinusOffset = findViewById(R.id.btnMinusOffset);
    }

    private void setupListeners() {
        // Nút Bắt đầu / Dừng duy nhất (Toggle Button)
        btnStartStop.setOnClickListener(v -> {
            if (isRecording) {
                stopRecording();
            } else {
                if (checkAudioPermission()) {
                    startRecording();
                } else {
                    requestAudioPermission();
                }
            }
        });

        // Nút chỉnh Offset (+)
        btnPlusOffset.setOnClickListener(v -> {
            calibrationOffset = calibrationOffset + 0.5;
            updateOffsetText();
        });

        // Nút chỉnh Offset (-)
        btnMinusOffset.setOnClickListener(v -> {
            calibrationOffset = calibrationOffset - 0.5;
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
            audioRecord = new AudioRecord(
                    MediaRecorder.AudioSource.MIC,
                    SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    minBufferSize
            );
        } catch (SecurityException e) {
            Toast.makeText(this, "Chưa cấp quyền Microphone!", Toast.LENGTH_SHORT).show();
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
        maxDba = -Double.MAX_VALUE;

        // Đổi nút thành DỪNG (màu đỏ)
        btnStartStop.setText("DỪNG");
        btnStartStop.setBackgroundTintList(ColorStateList.valueOf(Color.parseColor("#D50000")));
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

    // ================= XỬ LÝ DỮ LIỆU BẰNG NATIVE C++ (JNI) =================
    private void processAudioData(short[] pcmData, int readSize) {
        // 1. Gọi hàm C++ JNI tính toán các chỉ số LAeq, LAmax, LAmin đã lọc A-weighting
        double[] metrics = nativeCalculateNoiseMetrics(pcmData, readSize, SAMPLE_RATE, calibrationOffset);
        // 2. Gọi hàm C++ JNI tính mảng phổ tần số FFT
        float[] spectrum = nativeCalculateFFT(pcmData, readSize, NUM_SPECTRUM_BARS);

        if (metrics != null && metrics.length >= 3) {
            double laeq = metrics[0];
            double lamax = metrics[1];
            double lamin = metrics[2];

            // Cập nhật Min / Max
            if (lamin < minDba) minDba = lamin;
            if (lamax > maxDba) maxDba = lamax;

            // Đẩy kết quả về Main Thread (UI Thread) để cập nhật giao diện
            mainHandler.post(() -> {
                if (!isRecording) return; // Tránh cập nhật trễ sau khi bấm Dừng

                tvDbaValue.setText(String.format(Locale.US, "%.1f dBA", laeq));
                tvMinMax.setText(String.format(Locale.US, "Min: %.1f dBA | Max: %.1f dBA", minDba, maxDba));

                if (spectrum != null) {
                    spectrumView.updateSpectrum(spectrum);
                }
            });
        }
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

        // Đổi nút thành BẮT ĐẦU (màu xanh)
        btnStartStop.setText("BẮT ĐẦU");
        btnStartStop.setBackgroundTintList(ColorStateList.valueOf(Color.parseColor("#00C853")));
        tvStatus.setText("Trạng thái: Đã dừng");
        spectrumView.clear();
    }

    @Override
    protected void onStop() {
        super.onStop();
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