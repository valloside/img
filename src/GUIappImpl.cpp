#include "GUIapp.h"
#include <opencv2/opencv.hpp>
#include <imgui_internal.h>
#include "Compressor.h"
#include <filesystem>

enum ImageStatus
{
    UNLOADED = 0,
    PEDDING_FOR_COMPRESS = 1,
    COMPRESSING = 2,
    IMAGE_COMPRESSED = 3,
    COMPRESS_ERROR = 4,
    _count
};
struct TextureLoadResult
{
    enum Result : uint8_t
    {
        UNLOADED = 0,
        OK = 1,
        IMAGE_TOO_BIG = 2,
        INVALID_IMAGE = 3,
        OTHER_D3D_ERROR
    } result = UNLOADED;

    HRESULT errCode;
};

struct ImagetextureStatuseleter
{
    void operator()(ID3D11ShaderResourceView *data)
    {
        if (data) data->Release();
    }
};

struct CompressorManager
{
    static Compressor &get()
    {
        static Compressor compressor{};
        return compressor;
    }
};

using ImageTextureRes = std::unique_ptr<ID3D11ShaderResourceView, ImagetextureStatuseleter>;

struct ImageData
{
    cv::Mat                loadedImage{};
    std::vector<uchar>     compressedImage{};
    std::wstring           inputImagePath = L"";
    long                   inputImageSize = 0;
    Compressor::TaskHandle compressHandle = Compressor::InalidHandle;

    struct
    {
        cv::Mat         compressedImage{};
        std::string     filename;
        ImageTextureRes textureResource;
    } cache;

    Compressor::Params
        compressParams{.format = Compressor::Params::JPEG},
        compressParams_old = compressParams;

    ImageStatus imageStatus = ImageStatus::UNLOADED;

    TextureLoadResult textureStatus{};

    bool windowOpened = true;
    bool saved = false;

} emptyImageData;

auto i = sizeof(ImageData);

static std::vector<ImageData> openedImages{};

static uint32_t activeImageWindowIdx = 0;
static bool     activateLastWindow = false;

static std::vector<std::wstring> droppedImages{};

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

    std::vector<uchar> encodeImage(const cv::Mat &image, Compressor::Params::Format format)
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

    bool saveImageFile(std::wstring_view filePath, const std::vector<uchar> &buf, Compressor::Params::Format format)
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
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_DONTADDTORECENT | OFN_NOCHANGEDIR;

        if (!GetOpenFileNameW(&ofn))
            return {};

        return filePath;
    }

    // 将图片载入gpu
    TextureLoadResult loadTextureFromMemory(const cv::Mat &rawImage, ImageTextureRes &out_srv)
    {
        if (rawImage.cols > 16384 || rawImage.rows > 16384)
            return {.result = TextureLoadResult::IMAGE_TOO_BIG};
        if (rawImage.empty())
            return {.result = TextureLoadResult::INVALID_IMAGE};
        cv::Mat image;
        if (rawImage.channels() == 1)
            cv::cvtColor(rawImage, image, cv::COLOR_GRAY2RGBA);
        else if (rawImage.channels() == 3)
            cv::cvtColor(rawImage, image, cv::COLOR_BGR2RGBA);
        else if (rawImage.channels() == 4)
            cv::cvtColor(rawImage, image, cv::COLOR_BGRA2RGBA);
        else
            return {.result = TextureLoadResult::INVALID_IMAGE};

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
        HRESULT res = GUIapp::s_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
        if (res != S_OK)
            return {.result = TextureLoadResult::OTHER_D3D_ERROR, .errCode = res};

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        ID3D11ShaderResourceView *rawPtr = nullptr;
        res = GUIapp::s_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &rawPtr);
        if (res != S_OK)
            return {.result = TextureLoadResult::OTHER_D3D_ERROR, .errCode = res};

        out_srv.reset(rawPtr);
        pTexture->Release();

        return {.result = TextureLoadResult::OK};
    }

} // namespace

void GUIapp::GUIappImpl::renderUI()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("DockSpace", nullptr,
                 ImGuiWindowFlags_NoDocking
                     | ImGuiWindowFlags_NoTitleBar
                     | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoBringToFrontOnFocus
                     | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(3);

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

    GUIapp::GUIappImpl::refreshOpenedImageStatus();

    // 显示图片区域
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30 / 255.0f, 30 / 255.0f, 30 / 255.0f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{4, 0});
    if (ImGui::Begin("Image Display", nullptr, ImGuiWindowFlags_NoMove))
    {
        if (!openedImages.empty())
        {
            ImGui::BeginTabBar("##OpenedImageTabs", ImGuiTabBarFlags_DrawSelectedOverline);
            for (auto openedImageIter = openedImages.begin(); openedImageIter != openedImages.end();)
            {
                if (openedImageIter->imageStatus == ImageStatus::UNLOADED)
                {
                    ++openedImageIter;
                    continue;
                }
                ImGuiTabItemFlags flag = ImGuiTabItemFlags_None;
                if (activateLastWindow && openedImageIter == --openedImages.end())
                {
                    activateLastWindow = false;
                    flag |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(openedImageIter->cache.filename.data(), &openedImageIter->windowOpened, flag))
                {
                    if (openedImageIter->imageStatus == ImageStatus::IMAGE_COMPRESSED
                        && openedImageIter->textureStatus.result == TextureLoadResult::UNLOADED)
                    {
                        openedImageIter->cache.compressedImage = cv::imdecode(openedImageIter->compressedImage, cv::IMREAD_UNCHANGED);
                        openedImageIter->textureStatus = loadTextureFromMemory(openedImageIter->cache.compressedImage,
                                                                               openedImageIter->cache.textureResource);
                    }
                    else if (!openedImageIter->textureStatus.result == TextureLoadResult::UNLOADED)
                    {
                        openedImageIter->textureStatus = loadTextureFromMemory(openedImageIter->loadedImage,
                                                                               openedImageIter->cache.textureResource);
                    }
                    activeImageWindowIdx = openedImageIter - openedImages.begin();
                    ImVec2 cursorPos = ImGui::GetCursorPos();
                    ImVec2 contentRegionAvail = ImGui::GetContentRegionAvail();
                    if (openedImageIter->textureStatus.result == TextureLoadResult::OK)
                    {
                        float imgDisplayScaleFactor = std::min(contentRegionAvail.x / openedImageIter->loadedImage.cols,
                                                               contentRegionAvail.y / openedImageIter->loadedImage.rows);
                        ImGui::SetCursorPos({cursorPos.x + (contentRegionAvail.x - openedImageIter->loadedImage.cols * imgDisplayScaleFactor) * 0.5f,
                                             cursorPos.y + (contentRegionAvail.y - openedImageIter->loadedImage.rows * imgDisplayScaleFactor) * 0.5f});
                        ImGui::Image((ImTextureID)openedImageIter->cache.textureResource.get(),
                                     ImVec2{openedImageIter->loadedImage.cols * imgDisplayScaleFactor,
                                            openedImageIter->loadedImage.rows * imgDisplayScaleFactor});
                    }
                    else if (openedImageIter->textureStatus.result == TextureLoadResult::IMAGE_TOO_BIG)
                    {
                        ImGui::Text("Image is too big. Unable to display image.");
                    }
                    else if (openedImageIter->textureStatus.result == TextureLoadResult::INVALID_IMAGE)
                    {
                        ImGui::Text("Invalid Image data.");
                    }
                    else if (openedImageIter->textureStatus.result == TextureLoadResult::OTHER_D3D_ERROR)
                    {
                        ImGui::Text("Direct3D Error, code: %ld.", openedImageIter->textureStatus.errCode);
                    }
                    ImGui::EndTabItem();
                    ++openedImageIter;
                }
                else if (!openedImageIter->windowOpened)
                {
                    CompressorManager::get().removeTask(openedImageIter->compressHandle);
                    openedImageIter = openedImages.erase(openedImageIter);
                }
                else
                {
                    ++openedImageIter;
                }
            }
            openedImages.shrink_to_fit();
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{26 / 255.0f, 26 / 255.0f, 26 / 255.0f, 1.0f});
    if (ImGui::Begin("Compress Options", nullptr, ImGuiWindowFlags_NoMove))
    {
        auto loadOneImage = [](ImageData &image) {
            image.loadedImage = openImageFile(image.inputImagePath, image.inputImageSize);
            if (!image.loadedImage.empty())
            {
                image.imageStatus = ImageStatus::PEDDING_FOR_COMPRESS;
                image.cache.filename = wstringToUTF8string(std::filesystem::path{image.inputImagePath}.filename());
                openedImages.emplace_back(std::move(image));
                activateLastWindow = true;
            }
            else
            {
                MessageBoxW(nullptr, std::format(L"Failed to load image:\n{}.", image.inputImagePath).data(), L"Error", MB_OK | MB_ICONERROR);
            }
        };

        Compressor::Params defaultParam = (openedImages.empty() ? emptyImageData : openedImages[activeImageWindowIdx]).compressParams;

        // 加载图片按钮
        if (ImGui::Button("Load Image"))
        {
            ImageData image{.inputImagePath = openFileDialog(), .compressParams = defaultParam};
            if (!image.inputImagePath.empty())
                loadOneImage(image);
        }
        // 加载拖拽的文件
        for (auto &&path : droppedImages)
        {
            ImageData image{.inputImagePath = path, .compressParams = defaultParam};
            loadOneImage(image);
        }
        droppedImages.clear();
        droppedImages.shrink_to_fit();

        ImageData &selectedImage = openedImages.empty() ? emptyImageData : openedImages[activeImageWindowIdx];
        ImGui::SameLine();
        const char *imageStatusString[ImageStatus::_count] = {
            "No Image Loaded",
            "Wait for Compressing",
            "Image Compressing",
            "Image Compressed Successfully",
            "Image Compresssion Failed",
        };
        ImGui::Text("%s", imageStatusString[selectedImage.imageStatus]);
        if (selectedImage.imageStatus == ImageStatus::IMAGE_COMPRESSED)
        {
            ImGui::SameLine();
            ImGui::Text("%.2f KiB -> %.2f KiB (%.2f%%)",
                        selectedImage.inputImageSize / 1024.0, selectedImage.compressedImage.size() / 1024.0,
                        selectedImage.compressedImage.size() * 100.0 / selectedImage.inputImageSize - 100);
        }

        if (ImGui::Button("Save Image") && selectedImage.imageStatus == ImageStatus::IMAGE_COMPRESSED)
        {
            selectedImage.saved = saveImageFile(selectedImage.inputImagePath, selectedImage.compressedImage, selectedImage.compressParams.format);
        }
        if (selectedImage.imageStatus == ImageStatus::IMAGE_COMPRESSED && selectedImage.saved)
        {
            ImGui::SameLine();
            ImGui::Text("Image Saved Successfully");
        }
        else
        {
            selectedImage.saved = false;
        }

        // 压缩参数调节滑块

        int scale = selectedImage.compressParams.scale * 100;
        ImGui::SliderInt("Compression Quality", &selectedImage.compressParams.quality, 0, 100);

        ImGui::SliderInt("Resize", &scale, 0, 100,
                         std::format("%d%% ({}x{})",
                                     (int)(selectedImage.loadedImage.cols * scale / 100.0),
                                     (int)(selectedImage.loadedImage.rows * scale / 100.0))
                             .data());
        selectedImage.compressParams.scale = scale / 100.0;

        int selectedFormat = selectedImage.compressParams.format;
        ImGui::RadioButton("jpeg", &selectedFormat, 0);
        ImGui::SameLine();
        ImGui::RadioButton("png", &selectedFormat, 1);
        ImGui::SameLine();
        ImGui::RadioButton("webp", &selectedFormat, 2);
        selectedImage.compressParams.format = (Compressor::Params::Format)selectedFormat;
    }
    ImGui::End();
    ImGui::PopStyleColor();

    // ImGui::ShowDemoWindow();

    ImGui::End();
}

void GUIapp::GUIappImpl::onDropImageFile(std::wstring &&path)
{
    droppedImages.emplace_back(std::move(path));
}

void GUIapp::GUIappImpl::refreshOpenedImageStatus()
{
    using namespace std::chrono_literals;
    static std::chrono::milliseconds unchangedDuration{0};

    static auto lastTime = std::chrono::steady_clock::now();

    Compressor &imgCompressor = CompressorManager::get();
    for (auto &&image : openedImages)
    {
        auto now = std::chrono::steady_clock::now();
        unchangedDuration += std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
        lastTime = now;
        // 压缩参数改变时
        if (image.imageStatus != ImageStatus::UNLOADED && image.imageStatus != ImageStatus::COMPRESSING
            && (image.compressParams_old.quality != image.compressParams.quality
                || image.compressParams_old.scale != image.compressParams.scale
                || image.compressParams_old.format != image.compressParams.format))
        {
            image.imageStatus = ImageStatus::PEDDING_FOR_COMPRESS;
            unchangedDuration = 0ms;
            image.compressParams_old.quality = image.compressParams.quality;
            image.compressParams_old.format = image.compressParams.format;
            image.compressParams_old.scale = image.compressParams.scale;
        }

        if (image.imageStatus == ImageStatus::PEDDING_FOR_COMPRESS
            && (unchangedDuration > 360ms || image.compressedImage.empty()))
        {
            unchangedDuration = 0ms;
            image.compressHandle = imgCompressor.addCompressionTask(image.loadedImage, image.compressParams);
            image.imageStatus = ImageStatus::COMPRESSING;
        }
        if (image.imageStatus == ImageStatus::COMPRESSING && imgCompressor.checkTaskFinished(image.compressHandle))
        {
            image.imageStatus = ImageStatus::IMAGE_COMPRESSED;
            image.compressedImage = imgCompressor.getCompressResult(image.compressHandle);
            if (image.compressedImage.empty())
                image.imageStatus = ImageStatus::COMPRESS_ERROR;
        }
    }
}
