#include "GUIapp.h"
#include <opencv2/opencv.hpp>
#include <imgui_internal.h>
#include <shlobj.h>
#include "Compressor.h"

namespace imageData
{
    static bool         hasDroppedFile = false;
    static std::wstring inputImagePath = L"";
    static long         inputImageSize = 0;

    static cv::Mat            loadedImage{};
    static std::vector<uchar> compressedImage{};

    static Compressor::CompressionParams
        compressParams{.format = Compressor::CompressionParams::JPEG},
        compressParams_old = compressParams;

    namespace cache
    {
        static cv::Mat compressedImage{};
    }

    enum ImageStatus
    {
        NO_IMAGE = 0,
        PEDDING_FOR_COMPRESS = 1,
        COMPRESSING = 2,
        IMAGE_COMPRESSED = 3,
        _count
    };
    static ImageStatus imageStatus = ImageStatus::NO_IMAGE;

    static ID3D11ShaderResourceView *textureResource;
} // namespace imageData

using namespace imageData;

namespace
{
    std::string wstringToUTF8string(const std::wstring &wstr)
    {
        if (wstr.empty())
            return {};

        int         size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    cv::Mat openImageFile(std::wstring_view filePath, long &size)
    {
        // 宽字符路径得用支持宽字符的文件操作
        FILE *file = _wfopen(filePath.data(), L"rb");
        if (!file)
            return {};

        // 读取文件到缓冲区
        fseek(file, 0, SEEK_END);
        long fileSize = size = ftell(file);
        fseek(file, 0, SEEK_SET);

        std::vector<uchar> buffer(fileSize);
        fread(buffer.data(), sizeof(uchar), fileSize, file);
        fclose(file);

        // 将文件解码为图像对象
        try
        {
            return cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
        } catch (const cv::Exception &e)
        {
            std::cerr << "OpenCV Error: " << e.what() << '\n';
            return {};
        }
    }

    std::vector<uchar> encodeImage(const cv::Mat &image, Compressor::CompressionParams::Format format)
    {
        try
        {
            std::vector<uchar> buf;
            cv::imencode(Compressor::formatEnumToString(format).data(), image, buf);
            return buf;
        } catch (const cv::Exception &e)
        {
            std::cerr << "OpenCV Error: " << e.what() << '\n';
            return {};
        }
    }

    bool saveImageFile(std::wstring_view filePath, const std::vector<uchar> &buf, Compressor::CompressionParams::Format format)
    {
        // 宽字符路径得用支持宽字符的文件操作
        std::wstring savePath = std::format(L"{}_output{}", filePath.substr(0, filePath.rfind('.')), Compressor::formatEnumToWstring(format));
        FILE        *file = nullptr;
        errno_t      err = _wfopen_s(&file, savePath.data(), L"wb");
        if (err != 0)
            return false;

        fwrite(buf.data(), sizeof(uchar), buf.size(), file);
        err = fclose(file);
        if (err != 0)
            return false;
        return true;
    }

    // 调用系统 api 打开图片选择对话
    std::wstring openFileDialog()
    {
        // 用宽字符，确保支持中文路径
        std::wstring  filePath(MAX_PATH, 0);
        OPENFILENAMEW ofn = {0};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFilter = L"Image Files\0*.jpg;*.jpeg;*.png;*.webp;*.bmp\0All Files\0*.*\0";
        ofn.lpstrFile = filePath.data();
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (!GetOpenFileNameW(&ofn))
            return {};

        return filePath;
    }

    // 将图片载入gpu
    bool loadTextureFromMemory(const cv::Mat &rawImage, ID3D11ShaderResourceView *&out_srv)
    {
        cv::Mat image;
        if (rawImage.channels() == 1)
            cv::cvtColor(rawImage, image, cv::COLOR_GRAY2RGBA);
        else if (rawImage.channels() == 3)
            cv::cvtColor(rawImage, image, cv::COLOR_BGR2RGBA);
        else if (rawImage.channels() == 4)
            cv::cvtColor(rawImage, image, cv::COLOR_BGRA2RGBA);
        else
            return false;

        // Create texture
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = image.cols;
        desc.Height = image.rows;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D       *pTexture = NULL;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = image.data;
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        GUIapp::s_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        GUIapp::s_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &out_srv);
        pTexture->Release();

        return true;
    }

} // namespace

void GUIapp::GUIappImpl::renderUI()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::Begin("DockSpace", nullptr,
                 ImGuiWindowFlags_NoDocking
                     | ImGuiWindowFlags_NoTitleBar
                     | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoBringToFrontOnFocus
                     | ImGuiWindowFlags_NoNavFocus);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoTabBar);
        static bool is_first_time = true;
        if (is_first_time)
        {
            is_first_time = false;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_down_id = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.28f, nullptr, &dockspace_id);

            ImGui::DockBuilderDockWindow("Image Display", dockspace_id);
            ImGui::DockBuilderDockWindow("Compress Options", dock_down_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    // 显示图片区域
    if (ImGui::Begin("Image Display", nullptr, ImGuiWindowFlags_NoMove))
    {
        if (imageData::imageStatus != ImageStatus::NO_IMAGE)
        {
            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImVec2 contentRegionAvail = ImGui::GetContentRegionAvail();
            float  imgDisplayScaleFactor = std::min(contentRegionAvail.x / imageData::loadedImage.cols,
                                                    contentRegionAvail.y / imageData::loadedImage.rows);

            ImGui::SetCursorPos({cursorPos.x + (contentRegionAvail.x - imageData::loadedImage.cols * imgDisplayScaleFactor) * 0.5f,
                                 cursorPos.y + (contentRegionAvail.y - imageData::loadedImage.rows * imgDisplayScaleFactor) * 0.5f});
            ImGui::Image((ImTextureID)imageData::textureResource,
                         ImVec2{imageData::loadedImage.cols * imgDisplayScaleFactor, imageData::loadedImage.rows * imgDisplayScaleFactor});
        }
    }
    ImGui::End();

    if (ImGui::Begin("Compress Options", nullptr, ImGuiWindowFlags_NoMove))
    {
        // 加载图片按钮
        if (ImGui::Button("Load Image") || imageData::hasDroppedFile)
        {
            if (imageData::hasDroppedFile || (imageData::inputImagePath = openFileDialog(), !imageData::inputImagePath.empty()))
            {
                imageData::hasDroppedFile = false;
                imageData::loadedImage = openImageFile(imageData::inputImagePath, imageData::inputImageSize);
            }
            if (!imageData::loadedImage.empty())
            {
                // 使用 DirectX 上传纹理
                imageData::imageStatus = (ImageStatus)loadTextureFromMemory(imageData::loadedImage, imageData::textureResource);
            }
            else
            {
                imageData::imageStatus = ImageStatus::NO_IMAGE;
                MessageBoxA(nullptr, "Failed to load image.", "Error", MB_OK | MB_ICONERROR);
            }
        }
        ImGui::SameLine();
        const char *imageStatusString[ImageStatus::_count] = {
            "No Image Loaded",
            "Wait for Compressing",
            "Image Compressing",
            "Image Compressed Successfully",
        };
        ImGui::Text("%s", imageStatusString[imageData::imageStatus]);
        if (imageData::imageStatus == ImageStatus::IMAGE_COMPRESSED)
        {
            ImGui::SameLine();
            ImGui::Text("%.2f KiB -> %.2f KiB (%.2f%%)",
                        imageData::inputImageSize / 1024.0, imageData::compressedImage.size() / 1024.0,
                        imageData::compressedImage.size() * 100.0 / imageData::inputImageSize - 100);
        }

        // ImGui::SameLine();
        // ImGui::Text("%s", GUIapp::s_enableSleepOptimize ? "s_enableSleepOptimize" : "");

        static bool saved = false;
        if (ImGui::Button("Save Image") && imageData::imageStatus == ImageStatus::IMAGE_COMPRESSED)
        {
            saved = saveImageFile(imageData::inputImagePath, imageData::compressedImage, imageData::compressParams.format);
        }
        if (imageData::imageStatus == ImageStatus::IMAGE_COMPRESSED && saved)
        {
            ImGui::SameLine();
            ImGui::Text("Image Saved Successfully");
        }
        else
        {
            saved = false;
        }

        // 压缩参数调节滑块

        static int resizeFactor = 100, resizeFactor_old = resizeFactor;

        ImGui::SliderInt("Compression Quality", &imageData::compressParams.quality, 0, 100);

        ImGui::SliderInt("Resize", &resizeFactor, 0, 100,
                         std::format("%d%% ({}x{})",
                                     (int)(imageData::loadedImage.cols * resizeFactor / 100.0),
                                     (int)(imageData::loadedImage.rows * resizeFactor / 100.0))
                             .data());
        imageData::compressParams.scale = resizeFactor / 100.0;

        static int selectedFormat = 0;
        ImGui::RadioButton("jpeg", &selectedFormat, 0);
        ImGui::SameLine();
        ImGui::RadioButton("png", &selectedFormat, 1);
        ImGui::SameLine();
        ImGui::RadioButton("webp", &selectedFormat, 2);
        imageData::compressParams.format = (Compressor::CompressionParams::Format)selectedFormat;

        // 图片设计为一个状态机
        using namespace std::chrono_literals;
        static std::chrono::milliseconds unchangedDuration{0};

        // 压缩参数改变时
        if (imageData::imageStatus != ImageStatus::NO_IMAGE && imageData::imageStatus != ImageStatus::COMPRESSING
            && (imageData::compressParams_old.quality != imageData::compressParams.quality
                || resizeFactor_old != resizeFactor
                || imageData::compressParams_old.format != imageData::compressParams.format))
        {
            imageData::imageStatus = ImageStatus::PEDDING_FOR_COMPRESS;
            unchangedDuration = 0ms;
            imageData::compressParams_old.quality = imageData::compressParams.quality;
            imageData::compressParams_old.format = imageData::compressParams.format;
            resizeFactor_old = resizeFactor;
        }

        // 防抖设计
        static auto lastTime = std::chrono::steady_clock::now();
        auto        now = std::chrono::steady_clock::now();
        unchangedDuration += std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
        lastTime = now;

        static Compressor imgCompressor{};
        if (imageData::imageStatus == ImageStatus::PEDDING_FOR_COMPRESS && unchangedDuration > 360ms)
        {
            unchangedDuration = 0ms;
            imageData::imageStatus =
                imgCompressor.tryAddCompressionTask(imageData::loadedImage, imageData::compressedImage, imageData::compressParams)
                    ? ImageStatus::COMPRESSING
                    : ImageStatus::PEDDING_FOR_COMPRESS;
        }
        if (imageData::imageStatus == ImageStatus::COMPRESSING && imgCompressor.checkTaskFinished())
        {
            cache::compressedImage = cv::imdecode(imageData::compressedImage, cv::IMREAD_UNCHANGED);
            imageData::imageStatus = loadTextureFromMemory(cache::compressedImage, imageData::textureResource)
                                       ? ImageStatus::IMAGE_COMPRESSED
                                       : ImageStatus::PEDDING_FOR_COMPRESS;
        }
    }

    ImGui::End();

    // ImGui::ShowDemoWindow();

    ImGui::End();
}

void GUIapp::GUIappImpl::onDropImageFile(std::wstring &&path)
{
    hasDroppedFile = true;
    inputImagePath = std::move(path);
}