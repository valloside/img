#include "ConsoleApp.h"
#include "Compressor.h"
#include <filesystem>

namespace
{
    namespace fs = std::filesystem;

    cv::Mat loadImage(const std::string &input_path)
    {
        try
        {
            cv::Mat image = cv::imread(input_path, cv::IMREAD_UNCHANGED);
            if (image.empty())
                return {};
            return image;
        } catch (const cv::Exception &e)
        {
            std::cerr << "OpenCV Error: " << e.what() << '\n';
            return {};
        }
    }

    static constexpr Compressor::Params::Format stringToFormatEnum(std::string_view format)
    {
        if (format == ".jpg" || format == ".jpeg")
            return Compressor::Params::JPEG;
        else if (format == ".png")
            return Compressor::Params::PNG;
        else if (format == ".webp")
            return Compressor::Params::WEBP;
        else
            return Compressor::Params::_count;
    }

} // namespace

int ConsoleApp::start(int argc, char *argv[])
{
    if (argc < 4 || argc > 6)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <input_path> <output_path> <quality> [scale] [to_gray]\n"
                  << "  <quality>: Compression quality (0-100)\n"
                  << "  [scale]: Scaling factor (default: 1.0)\n"
                  << "  [to_gray]: Convert to grayscale (0 or 1, default: 0)\n";
        return EXIT_FAILURE;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];
    int         quality = std::stoi(argv[3]);
    double      scale = (argc >= 5) ? std::stod(argv[4]) : 1.0;
    bool        toGray = (argc == 6) ? (std::stoi(argv[5]) != 0) : false;
    std::string formatString = fs::path(output_path).extension().string();
    auto        format = stringToFormatEnum(formatString);

    if (format == Compressor::Params::_count)
    {
        std::cerr << "Error: Input file does not exist: " << formatString << '\n';
        return EXIT_FAILURE;
    }

    if (!fs::exists(input_path))
    {
        std::cerr << "Error: unsupport output file format: " << input_path << '\n';
        return EXIT_FAILURE;
    }

    cv::Mat image = loadImage(input_path);
    if (image.empty())
        std::cerr << "Error: Cannot open file: " << input_path << '\n';
    Compressor             compressor;
    Compressor::TaskHandle handle = compressor.addCompressionTask(image, {.scale = scale, .quality = quality, .toGray = toGray, .format = format});

    while (!compressor.checkTaskFinished(handle))
        std::this_thread::sleep_for(std::chrono::milliseconds{20});

    std::vector<uchar> out = compressor.getCompressResult(handle);
    if (out.empty())
    {
        std::cerr << "Error: Failed to compress image: " << output_path << '\n';
        return EXIT_SUCCESS;
    }

    std::string savePath = std::format("{}_output{}", input_path.substr(0, input_path.rfind('.')), formatString);
    FILE       *file = nullptr;
    errno_t     err = fopen_s(&file, savePath.data(), "wb");
    if (err != 0)
    {
        std::cerr << "Error: Failed to save image to: " << output_path << '\n';
        return EXIT_SUCCESS;
    }

    fwrite(out.data(), sizeof(uchar), out.size(), file);
    err = fclose(file);
    if (err != 0)
    {
        std::cerr << "Error: Failed to save image to: " << output_path << '\n';
        return EXIT_SUCCESS;
    }
    std::cout << "Image saved to:" << output_path << '\n';
    return EXIT_SUCCESS;
}
