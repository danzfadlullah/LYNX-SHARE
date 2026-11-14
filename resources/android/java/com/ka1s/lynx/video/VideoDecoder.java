package com.ka1s.lynx.video;

import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.graphics.Bitmap;
import android.graphics.ImageFormat;
import android.media.MediaCodecInfo;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Android video decoder using MediaExtractor + MediaCodec.
 * Called from C++ via JNI (see src/ui/VideoMedia.cpp #elif JUCE_ANDROID).
 * Expected C++ JNI signatures:
 *   - constructor: (Ljava/lang/String;)V
 *   - prepare(): V (async, calls onPrepared)
 *   - seek(double): V
 *   - decodeFrame(): V (decodes one frame, calls onFrame)
 *   - seekAudio(double): V
 *   - release(): V
 * Callbacks from Java to C++ (native methods):
 *   - onPrepared(int width, int height, double duration, double fps)
 *   - onFrame(Bitmap bitmap, long timestampUs)
 *   - onAudio(float[] data, int channels, int frames, long timestampUs)
 */
public class VideoDecoder {
    private static final String TAG = "LYNX_VideoDecoder";

    private final String path;
    private MediaExtractor extractor;
    private MediaCodec videoDecoder;
    private MediaCodec audioDecoder;
    private MediaFormat videoFormat;
    private MediaFormat audioFormat;
    private int videoTrackIndex = -1;
    private int audioTrackIndex = -1;
    private boolean prepared = false;
    private boolean released = false;

    // Video output
    private int videoWidth = 0;
    private int videoHeight = 0;
    private double videoDuration = 0.0;
    private double videoFrameRate = 0.0;

    // Synchronization for async prepare
    private final CountDownLatch prepareLatch = new CountDownLatch(1);
    private volatile String prepareError = null;

    // Frame callback
    private Bitmap pendingFrame = null;
    private long pendingFrameTimeUs = 0;
    private final Object frameLock = new Object();
    private volatile boolean frameReady = false;

    // Audio callback buffer
    private float[] pendingAudio = null;
    private int pendingAudioChannels = 0;
    private int pendingAudioFrames = 0;
    private long pendingAudioTimeUs = 0;
    private final Object audioLock = new Object();
    private volatile boolean audioReady = false;

    public VideoDecoder(String path) {
        this.path = path;
    }

    public void prepare() {
        new Thread(() -> {
            try {
                doPrepare();
            } catch (Exception e) {
                Log.e(TAG, "prepare failed", e);
                prepareError = e.getMessage();
            } finally {
                prepareLatch.countDown();
            }
        }).start();
    }

    private void doPrepare() throws Exception {
        extractor = new MediaExtractor();
        extractor.setDataSource(path);

        int trackCount = extractor.getTrackCount();
        for (int i = 0; i < trackCount; i++) {
            MediaFormat format = extractor.getTrackFormat(i);
            String mime = format.getString(MediaFormat.KEY_MIME);
            if (mime != null) {
                if (mime.startsWith("video/") && videoTrackIndex == -1) {
                    videoTrackIndex = i;
                    videoFormat = format;
                    videoWidth = format.getInteger(MediaFormat.KEY_WIDTH);
                    videoHeight = format.getInteger(MediaFormat.KEY_HEIGHT);
                    if (format.containsKey(MediaFormat.KEY_DURATION)) {
                        videoDuration = format.getLong(MediaFormat.KEY_DURATION) / 1_000_000.0; // us to seconds
                    }
                    if (format.containsKey(MediaFormat.KEY_FRAME_RATE)) {
                        videoFrameRate = format.getFloat(MediaFormat.KEY_FRAME_RATE);
                    }
                } else if (mime.startsWith("audio/") && audioTrackIndex == -1) {
                    audioTrackIndex = i;
                    audioFormat = format;
                }
            }
        }

        if (videoTrackIndex >= 0) {
            extractor.selectTrack(videoTrackIndex);
            String videoMime = videoFormat.getString(MediaFormat.KEY_MIME);
            videoDecoder = MediaCodec.createDecoderByType(videoMime);
            videoDecoder.configure(videoFormat, null, null, 0);
            videoDecoder.start();
        }

        if (audioTrackIndex >= 0) {
            extractor.selectTrack(audioTrackIndex);
            String audioMime = audioFormat.getString(MediaFormat.KEY_MIME);
            audioDecoder = MediaCodec.createDecoderByType(audioMime);
            audioDecoder.configure(audioFormat, null, null, 0);
            audioDecoder.start();
        }

        prepared = true;
        // Notify C++ onPrepared
        onPrepared(videoWidth, videoHeight, videoDuration, videoFrameRate);
    }

    public void seek(double seconds) {
        if (!prepared || videoDecoder == null) return;
        long targetUs = (long) (seconds * 1_000_000);
        extractor.seekTo(targetUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
        videoDecoder.flush();
    }

    public void seekAudio(double seconds) {
        if (!prepared || audioDecoder == null) return;
        long targetUs = (long) (seconds * 1_000_000);
        extractor.seekTo(targetUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
        audioDecoder.flush();
    }

    public void decodeFrame() {
        if (!prepared || videoDecoder == null) return;

        // Feed input
        int inputIndex = videoDecoder.dequeueInputBuffer(10000);
        if (inputIndex >= 0) {
            ByteBuffer inputBuffer = videoDecoder.getInputBuffer(inputIndex);
            int sampleSize = extractor.readSampleData(inputBuffer, 0);
            if (sampleSize < 0) {
                videoDecoder.queueInputBuffer(inputIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
            } else {
                long presentationTimeUs = extractor.getSampleTime();
                videoDecoder.queueInputBuffer(inputIndex, 0, sampleSize, presentationTimeUs, 0);
                extractor.advance();
            }
        }

        // Get output
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        int outputIndex = videoDecoder.dequeueOutputBuffer(info, 10000);
        if (outputIndex >= 0) {
            if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                // End of stream
                videoDecoder.releaseOutputBuffer(outputIndex, false);
                return;
            }

            // Get the output frame as Bitmap
            // Note: This requires API 29+ (MediaCodec.createOutputSurface) or manual YUV->RGB conversion
            // For simplicity, we use a Surface approach on API 29+, fallback to manual conversion
            if (android.os.Build.VERSION.SDK_INT >= 29) {
                // Use ImageReader or similar for zero-copy
                // For now, manual conversion from YUV
                ByteBuffer outputBuffer = videoDecoder.getOutputBuffer(outputIndex);
                Bitmap bitmap = yuvToBitmap(outputBuffer, info, videoWidth, videoHeight);
                if (bitmap != null) {
                    synchronized (frameLock) {
                        pendingFrame = bitmap;
                        pendingFrameTimeUs = info.presentationTimeUs;
                        frameReady = true;
                    }
                    onFrame(bitmap, info.presentationTimeUs);
                }
            } else {
                // Pre-API 29: manual YUV conversion
                ByteBuffer outputBuffer = videoDecoder.getOutputBuffer(outputIndex);
                Bitmap bitmap = yuvToBitmapLegacy(outputBuffer, info, videoWidth, videoHeight);
                if (bitmap != null) {
                    synchronized (frameLock) {
                        pendingFrame = bitmap;
                        pendingFrameTimeUs = info.presentationTimeUs;
                        frameReady = true;
                    }
                    onFrame(bitmap, info.presentationTimeUs);
                }
            }
            videoDecoder.releaseOutputBuffer(outputIndex, false);
        } else if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
            // Format changed, reconfigure
            MediaFormat newFormat = videoDecoder.getOutputFormat();
            videoWidth = newFormat.getInteger(MediaFormat.KEY_WIDTH);
            videoHeight = newFormat.getInteger(MediaFormat.KEY_HEIGHT);
        }
    }

    public void release() {
        released = true;
        if (videoDecoder != null) {
            videoDecoder.stop();
            videoDecoder.release();
            videoDecoder = null;
        }
        if (audioDecoder != null) {
            audioDecoder.stop();
            audioDecoder.release();
            audioDecoder = null;
        }
        if (extractor != null) {
            extractor.release();
            extractor = null;
        }
    }

    // --- Native callbacks (called from Java, implemented in C++) ---

    private native void onPrepared(int width, int height, double duration, double fps);
    private native void onFrame(Bitmap bitmap, long timestampUs);
    private native void onAudio(float[] data, int channels, int frames, long timestampUs);

    // --- YUV to Bitmap conversion ---

    private Bitmap yuvToBitmap(ByteBuffer buffer, MediaCodec.BufferInfo info, int width, int height) {
        // API 29+: Use MediaCodec.getOutputImage (requires ImageReader setup)
        // Fallback to manual conversion for now
        return yuvToBitmapLegacy(buffer, info, width, height);
    }

    private Bitmap yuvToBitmapLegacy(ByteBuffer buffer, MediaCodec.BufferInfo info, int width, int height) {
        // Simple NV21/YV12 to RGB conversion
        // Most Android codecs output YUV420 semi-planar (NV21) or planar (YV12)
        // This is a basic implementation - production code should use RenderScript or libyuv
        try {
            int ySize = width * height;
            int uvSize = ySize / 4; // For NV21, UV plane is 1/4 of Y

            byte[] yuv = new byte[ySize + uvSize * 2];
            buffer.rewind();
            buffer.get(yuv, 0, Math.min(yuv.length, info.size));

            Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            int[] rgb = new int[ySize];
            nv21ToRgb(yuv, width, height, rgb);
            bitmap.setPixels(rgb, 0, width, 0, 0, width, height);
            return bitmap;
        } catch (Exception e) {
            Log.e(TAG, "YUV to Bitmap conversion failed", e);
            return null;
        }
    }

    private static void nv21ToRgb(byte[] nv21, int width, int height, int[] rgb) {
        final int frameSize = width * height;
        int yIndex = 0;
        int uvIndex = frameSize;

        int a = 0xFF << 24;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int yVal = (0xFF & nv21[yIndex]) - 16;
                if (yVal < 0) yVal = 0;

                int uvOffset = uvIndex + (y / 2) * width + (x & ~1) - frameSize;
                int v = (0xFF & nv21[uvOffset + 1]) - 128;
                int u = (0xFF & nv21[uvOffset]) - 128;

                yVal = yVal * 1192;
                int r = (yVal + 1634 * v);
                int g = (yVal - 833 * v - 400 * u);
                int b = (yVal + 2066 * u);

                r = Math.min(262143, Math.max(0, r));
                g = Math.min(262143, Math.max(0, g));
                b = Math.min(262143, Math.max(0, b));

                rgb[yIndex] = a | ((r << 6) & 0xFF0000) | ((g >> 2) & 0xFF00) | ((b >> 10) & 0xFF);
                yIndex++;
            }
        }
    }
}