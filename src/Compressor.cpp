#include "Compressor.h"

using namespace std::chrono_literals;

Compressor::Compressor(uint32_t maxThread) :
    mMaxThread(maxThread)
{
    this->mCompressWorkers.reserve(this->mMaxThread);
}

Compressor::~Compressor()
{
    std::unique_lock lock{this->mMutex};
    this->mThreadDestroy = true;
    this->mCondi.notify_all();
}

Compressor::TaskHandle Compressor::addCompressionTask(const cv::Mat &image, const Params &param)
{
    TaskHandle ret;
    {
        std::unique_lock lock{this->mMutex};
        this->mQueuedTasks.emplace(this->mGenId, image, std::vector<uchar>{}, param);
        ret = this->mGenId++;
        if (this->mIdleThread == 0 && this->mCompressWorkers.size() < this->mMaxThread)
            this->mCompressWorkers.emplace_back([this]() {
                this->compressThreadFunc();
            });
    }
    this->mCondi.notify_one();
    return ret;
}
bool Compressor::checkTaskFinished(Compressor::TaskHandle handle)
{
    std::unique_lock lock{this->mFinishedTaskMutex};

    auto iter = std::find_if(this->mFinishedTasks.begin(),
                             this->mFinishedTasks.end(),
                             [handle](const Compressor::Task &task) {
                                 return task.mId == handle;
                             });
    return iter != this->mFinishedTasks.end();
}

void Compressor::removeTask(Compressor::TaskHandle handle)
{
    if (handle == Compressor::InalidHandle)
        return;
    std::unique_lock lock{this->mFinishedTaskMutex};
    std::size_t      erased = std::erase_if(this->mFinishedTasks, [handle](const Compressor::Task &task) { return task.mId == handle; });
    if (erased == 0)
        this->mPendingRemoveTasks.emplace_back(handle);
    return;
}

std::vector<uchar> Compressor::getCompressResult(Compressor::TaskHandle handle)
{
    std::unique_lock lock{this->mFinishedTaskMutex};

    auto iter = std::find_if(this->mFinishedTasks.begin(),
                             this->mFinishedTasks.end(),
                             [handle](const Compressor::Task &task) {
                                 return task.mId == handle;
                             });
    if (iter != this->mFinishedTasks.end())
    {
        std::vector<uchar> ret = std::move(iter->mOutputImage);
        this->mFinishedTasks.erase(iter);
        return ret;
    }
    else
    {
        return {};
    }
}

void Compressor::compressThreadFunc()
{
#if _POSIX_THREADS
    pthread_setname_np(pthread_self(), "Compressing Thread");
#endif
    std::unique_lock lock{this->mMutex};
    while (true)
    {
        if (this->mQueuedTasks.empty())
        {
            ++mIdleThread;
            this->mCondi.wait(lock);
            --mIdleThread;
            if (this->mThreadDestroy)
                break;
            if (this->mQueuedTasks.empty())
                continue;
        }

        Task task = std::move(this->mQueuedTasks.front());
        this->mQueuedTasks.pop();

        lock.unlock();
        Compressor::compressImage(task);
        {
            std::unique_lock lock2{this->mFinishedTaskMutex};
            if (std::erase_if(this->mPendingRemoveTasks, [&task](TaskHandle id) { return task.mId == id; }) == 0)
            {
                this->mFinishedTasks.emplace_back(std::move(task));
            }
        }
        lock.lock();
    }
}

// 图片压缩处理
bool Compressor::compressImage(Task &task)
{
    // 调整尺寸
    if (task.mCompressionParam.scale > 0.0 && task.mCompressionParam.scale < 1.0)
    {
        cv::resize(task.mRawImage, task.mRawImage, cv::Size(), task.mCompressionParam.scale, task.mCompressionParam.scale, cv::INTER_LINEAR);
    }

    // 转换为灰度图
    if (task.mCompressionParam.toGray)
    {
        cv::cvtColor(task.mRawImage, task.mRawImage, cv::COLOR_BGR2GRAY);
    }

    try
    {
        std::vector<int> compression_params;
        switch (task.mCompressionParam.format)
        {
        case Params::JPEG:
            compression_params = {cv::IMWRITE_JPEG_QUALITY, task.mCompressionParam.quality};
            break;
        case Params::PNG:
            compression_params = {cv::IMWRITE_PNG_COMPRESSION, 10 - task.mCompressionParam.quality / 10};
            break;
        case Params::WEBP:
            compression_params = {cv::IMWRITE_WEBP_QUALITY, task.mCompressionParam.quality};
            break;
        default:
            return false;
        }
        return cv::imencode(formatEnumToString(task.mCompressionParam.format).data(),
                            task.mRawImage,
                            task.mOutputImage,
                            compression_params);
    } catch (const cv::Exception &e)
    {
        std::cerr << "OpenCV Error: " << e.what() << '\n';
        return false;
    }
}