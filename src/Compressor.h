#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <variant>
#include <condition_variable>

class Compressor
{
public:
    struct Params
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

        constexpr bool operator==(const Params &rhs) const
        {
            return scale == rhs.scale && quality == rhs.quality && toGray == rhs.toGray && format == rhs.format;
        }

        constexpr bool operator!=(const Params &rhs) const
        {
            return scale != rhs.scale || quality != rhs.quality || toGray != rhs.toGray || format != rhs.format;
        }
    };

    enum class Status
    {
        Uninitailized = 0,
        Idle,
        TaskStarted,
        TaskEnded,
        WaitingForExit,
        ThreadExit
    };

    using TaskHandle = uint32_t;

    static constexpr uint32_t InalidHandle = std::numeric_limits<uint32_t>::max();

    Compressor(uint32_t maxThread = std::thread::hardware_concurrency());
    ~Compressor();

    TaskHandle addCompressionTask(const cv::Mat &image, const Params &param);

    bool checkTaskFinished(Compressor::TaskHandle handle);

    void removeTask(Compressor::TaskHandle handle);

    std::vector<uchar> getCompressResult(Compressor::TaskHandle handle);

    static constexpr std::string_view formatEnumToString(Params::Format format)
    {
        switch (format)
        {
        case Params::JPEG:
            return ".jpg";
        case Params::PNG:
            return ".png";
        case Params::WEBP:
            return ".webp";
        default:
            return ".jpg";
        }
    }
    static constexpr std::wstring_view formatEnumToWstring(Params::Format format)
    {
        switch (format)
        {
        case Params::JPEG:
            return L".jpg";
        case Params::PNG:
            return L".png";
        case Params::WEBP:
            return L".webp";
        default:
            return L".jpg";
        }
    }

private:
    std::mutex                mMutex;
    std::condition_variable   mCondi;
    std::vector<std::jthread> mCompressWorkers;
    uint32_t                  mMaxThread;
    uint32_t                  mIdleThread = 0;
    TaskHandle                mGenId = 0;
    bool                      mThreadDestroy = false;

    struct Task
    {
        TaskHandle mId;

        cv::Mat            mRawImage;
        std::vector<uchar> mOutputImage;

        Params mCompressionParam;
        Status mStatus = Status::Uninitailized;
    };

    std::mutex       mTaskMutex;
    std::queue<Task> mQueuedTasks;

    std::vector<TaskHandle> mPendingRemoveTasks;

    std::mutex        mFinishedTaskMutex;
    std::vector<Task> mFinishedTasks;

    void compressThreadFunc();

    static bool compressImage(Task &task);
};
