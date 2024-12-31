#include "Compressor.h"

using namespace std::chrono_literals;

Compressor::Compressor() :
    mCompressThread([this]() {
        this->compressThreadFunc();
    }) {}

Compressor::~Compressor()
{
    while (this->mTaskStatus != Status::ThreadExit)
    {
        this->mTaskStatus = Status::WaitingForExit;
        this->mCondi.notify_one();
        std::this_thread::sleep_for(10ms);
    }
    this->mCompressThread.join();
}

bool Compressor::tryAddCompressionTask(const cv::Mat &image, std::variant<cv::Mat *, std::vector<uchar> *> output, const CompressionParams &param)
{
    Status from = Status::TaskEnded;
    if (this->mTaskStatus.compare_exchange_strong(from, Status::Idle) || from == Status::Idle)
    {
        this->mRawImage = image;
        this->mOutputImage = std::move(output);
        this->mCompressionParam = param;
        std::unique_lock lock{this->mMutex};
        this->mCondi.notify_one();
        return true;
    }
    return false;
}

bool Compressor::tryAddCompressionTask(const cv::Mat &image, std::vector<uchar> &output, const CompressionParams &param)
{
    return this->tryAddCompressionTask(image, &output, param);
}

bool Compressor::tryAddCompressionTask(const cv::Mat &image, cv::Mat &output, const CompressionParams &param)
{
    return this->tryAddCompressionTask(image, &output, param);
}

bool Compressor::checkTaskFinished()
{
    Status from = Status::TaskEnded;
    return this->mTaskStatus.compare_exchange_strong(from, Status::Idle);
}

void Compressor::compressThreadFunc()
{
    std::unique_lock lock{this->mMutex};
    Status           from = Status::Uninitailized;
    this->mTaskStatus.compare_exchange_strong(from, Status::Idle);
    while (true)
    {
        this->mCondi.wait(lock);
        Status from = Status::Idle;
        if (!this->mTaskStatus.compare_exchange_strong(from, Status::TaskStarted))
        {
            if (from == Status::WaitingForExit)
                break;
            else
                continue;
        }
        this->compressImage();
        this->mTaskStatus = Status::TaskEnded;
    }
    this->mTaskStatus = Status::ThreadExit;
}

// 图片压缩处理
bool Compressor::compressImage()
{
    // 调整尺寸
    if (this->mCompressionParam.scale > 0.0 && this->mCompressionParam.scale < 1.0)
    {
        cv::resize(this->mRawImage, this->mRawImage, cv::Size(), this->mCompressionParam.scale, this->mCompressionParam.scale, cv::INTER_LINEAR);
    }

    // 转换为灰度图
    if (this->mCompressionParam.toGray)
    {
        cv::cvtColor(this->mRawImage, this->mRawImage, cv::COLOR_BGR2GRAY);
    }

    try
    {
        std::vector<int> compression_params;
        switch (this->mCompressionParam.format)
        {
        case CompressionParams::JPEG:
            compression_params = {cv::IMWRITE_JPEG_QUALITY, this->mCompressionParam.quality};
            break;
        case CompressionParams::PNG:
            compression_params = {cv::IMWRITE_PNG_COMPRESSION, 10 - this->mCompressionParam.quality / 10};
            break;
        case CompressionParams::WEBP:
            compression_params = {cv::IMWRITE_WEBP_QUALITY, this->mCompressionParam.quality};
            break;
        default:
            return false;
        }
        if (this->mOutputImage.index() == 1)
        {
            return cv::imencode(formatEnumToString(this->mCompressionParam.format).data(),
                                this->mRawImage,
                                *std::get<std::vector<uchar> *>(this->mOutputImage),
                                compression_params);
        }
        else
        {
            std::vector<uchar> buf;
            if (!cv::imencode(formatEnumToString(this->mCompressionParam.format).data(),
                              this->mRawImage,
                              buf,
                              compression_params))
            {
                return false;
            }
            cv::imdecode(buf, cv::IMREAD_UNCHANGED, std::get<cv::Mat *>(this->mOutputImage));
            return true;
        }

    } catch (const cv::Exception &e)
    {
        std::cerr << "OpenCV Error: " << e.what() << '\n';
        return false;
    }
}