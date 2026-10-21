# Calibrate_dBA_Android 🎙️📊

> **Đề tài 2 - Môn Lập trình ứng dụng di động**

Ứng dụng đo mức độ ồn âm thanh (**dBA Meter**) kết hợp phân tích phổ tần số (**Spectrum Analyzer**) thời gian thực trên hệ điều hành Android. Ứng dụng được tối ưu hiệu năng bằng cách xử lý tín hiệu âm thanh PCM thông qua thư viện native C++ (NDK/JNI).

---

## 🌟 Tính năng nổi bật

- **Đo mức độ ồn dBA thời gian thực**:
  - Thu âm từ micro thiết bị bằng `AudioRecord` ở định dạng PCM 16-bit, tần số lấy mẫu 44.1 kHz (`VOICE_RECOGNITION`).
  - Chuyển dữ liệu âm thanh xuống C++ (JNI) để xử lý và tính toán mức cường độ dBA chuẩn xác.
- **Phân tích phổ tần số (FFT Spectrum)**:
  - Hiển thị trực quan phổ tần số gồm 32 cột dải tần thông qua Custom View (`SpectrumView`).
  - Tự động đổi màu cột biểu đồ theo mức độ an toàn của âm thanh:
    - 🟢 **Xanh lá**: An toàn (< 50 dB)
    - 🟡 **Vàng**: Mức trung bình (50 - 80 dB)
    - 🔴 **Đỏ**: Mức nguy hại (> 80 dB)
- **Hiệu chuẩn độ chính xác (Calibration Offset)**:
  - Cho phép điều chỉnh tăng/giảm hệ số Offset (mỗi bước ±0.5 dB) để cân chỉnh kết quả đo khớp với máy đo độ ồn chuyên dụng.
- **Theo dõi Min / Max**:
  - Tự động ghi nhận và hiển thị giá trị dBA nhỏ nhất (Min) và lớn nhất (Max) trong suốt phiên thu âm.
- **Tối ưu hiệu năng & Quản lý vòng đời**:
  - Thu âm và tính toán PCM trên **Background Thread** riêng biệt, tránh gây giật/lag giao diện (UI Thread).
  - Tự động dừng thu âm và giải phóng tài nguyên Micro khi ứng dụng di chuyển xuống nền hoặc đóng hoàn toàn.

---

## 🛠️ Công nghệ & Kiến trúc

| Thành phần | Công nghệ / Thư viện |
| :--- | :--- |
| **Ngôn ngữ lập trình** | Java (Android SDK) & C++17 (Android NDK / JNI) |
| **Giao diện (UI)** | Custom View (`SpectrumView`), Canvas Drawing, Material Components |
| **Thu âm (Audio Engine)** | `AudioRecord` (Mono, 16-bit PCM, 44.1kHz) |
| **Native Build Engine** | CMake 3.22+ & NDK |
| **Quản lý luồng** | Java Background Thread & Main Handler (`Looper.getMainLooper()`) |

---

## 📐 Cấu trúc dự án

```text
Calibrate_dBA_Android/
├── app/
│   ├── src/
│   │   ├── main/
│   │   │   ├── cpp/
│   │   │   │   ├── CMakeLists.txt        # File cấu hình build CMake cho C++
│   │   │   │   └── native-lib.cpp        # Thuật toán C++ tính dBA & FFT qua JNI
│   │   │   ├── java/com/example/calibratedba/
│   │   │   │   ├── MainActivity.java     # Quản lý giao diện, cấp quyền, AudioRecord, gọi Native JNI
│   │   │   │   └── SpectrumView.java     # Custom View vẽ biểu đồ phổ tần số & các trục tọa độ
│   │   │   ├── res/
│   │   │   │   ├── layout/activity_main.xml # Giao diện chính của ứng dụng
│   │   │   │   └── values/               # Colors, Strings, Themes
│   │   │   └── AndroidManifest.xml       # Khai báo quyền RECORD_AUDIO
│   └── build.gradle.kts                  # Cấu hình Gradle module app
├── build.gradle.kts                      # Cấu hình Gradle root project
└── README.md                             # Tài liệu giới thiệu dự án
```

---

## 🔄 Luồng hoạt động Native JNI

1. **Java (`MainActivity.java`)**:
   - Nạp thư viện native: `System.loadLibrary("calibratedba")`.
   - Thu thập mẫu âm thanh `short[] pcmBuffer` từ `AudioRecord`.
   - Truyền dữ liệu xuống C++ qua 2 hàm native:
     ```java
     public native double nativeCalculateDBA(short[] pcmData, int readSize);
     public native float[] nativeCalculateFFT(short[] pcmData, int readSize, int numBars);
     ```

2. **C++ (`native-lib.cpp`)**:
   - `nativeCalculateDBA`: Tính RMS (Root Mean Square) từ mảng PCM, chuyển đổi sang giá trị decibel (dBA).
   - `nativeCalculateFFT`: Phân tích biên độ dải tần chia làm `numBars` cột để hiển thị trên biểu đồ.

---

## 🚀 Hướng dẫn Build & Chạy ứng dụng

### Yêu cầu môi trường
- **Android Studio**: Jellyfish / Koala / Ladybug hoặc mới hơn.
- **Android SDK**: API Level 24 (Android 7.0) trở lên.
- **Android NDK & CMake**: Cài đặt qua SDK Manager trong Android Studio (`Tools -> SDK Manager -> SDK Tools -> NDK & CMake`).

### Các bước thực hiện
1. Clone hoặc mở thư mục dự án `Calibrate_dBA_Android` trong Android Studio.
2. Đợi Gradle Sync hoàn tất quá trình tải dependencies và cấu hình C++ CMake.
3. Kết nối thiết bị Android thật (khuyên dùng để thử nghiệm Microphone) hoặc Android Emulator.
4. Nhấn nút **Run** (hoặc tổ hợp phím `Shift + F10`) để build và cài đặt ứng dụng.
5. Khi mở ứng dụng lần đầu, cấp quyền **Microphone** khi được hỏi để bắt đầu thu âm và đo độ ồn.
