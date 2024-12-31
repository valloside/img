#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <variant>
#include <condition_variable>

class Compressor
{
public:
    struct CompressionParams
    {
        double scale = 1.0;    // 尺寸缩放比例（0.0-1.0），默认1.0
        int    quality = 80;   // 压缩质量（0-100），默认80
        bool   toGray = false; // 是否转换为灰度图
        enum Format : uint8_t
        {
            JPEG = 0,
            PNG,
            WEBP,
            _count
        } format = JPEG; // 输出格式
    };

    Compressor();
    ~Compressor();

    bool tryAddCompressionTask(const cv::Mat &image, std::variant<cv::Mat *, std::vector<uchar> *> output, const CompressionParams &param);
    bool tryAddCompressionTask(const cv::Mat &image, std::vector<uchar> &output, const CompressionParams &param);
    bool tryAddCompressionTask(const cv::Mat &image, cv::Mat &output, const CompressionParams &param);

    bool checkTaskFinished();

    static constexpr std::string_view formatEnumToString(CompressionParams::Format format)
    {
        switch (format)
        {
        case CompressionParams::JPEG:
            return ".jpg";
        case CompressionParams::PNG:
            return ".png";
        case CompressionParams::WEBP:
            return ".webp";
        default:
            return ".jpg";
        }
    }
    static constexpr std::wstring_view formatEnumToWstring(CompressionParams::Format format)
    {
        switch (format)
        {
        case CompressionParams::JPEG:
            return L".jpg";
        case CompressionParams::PNG:
            return L".png";
        case CompressionParams::WEBP:
            return L".webp";
        default:
            return L".jpg";
        }
    }

    enum class Status
    {
        Uninitailized = 0,
        Idle,
        TaskStarted,
        TaskEnded,
        WaitingForExit,
        ThreadExit
    };

    std::atomic<Status> mTaskStatus = Status::Uninitailized;

private:
    std::mutex              mMutex;
    std::condition_variable mCondi;
    std::mutex              mOnDestroyMutex;
    std::condition_variable mDestroyCondi;
    CompressionParams       mCompressionParam;

    std::atomic<bool> mCompressionStarting = false;
    std::atomic<bool> mCompressionThreadExit = false;

    bool mHasTask = false;
    bool mDestroyThread = false;

    cv::Mat                                       mRawImage;
    std::variant<cv::Mat *, std::vector<uchar> *> mOutputImage;

    std::thread mCompressThread;

    void compressThreadFunc();

    bool compressImage();
};
