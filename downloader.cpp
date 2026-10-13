#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <filesystem>
#include <curl/curl.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace std;
namespace fs = std::filesystem;

// Validate an HTTP or HTTPS URL using libcurl's URL parser.
bool isValidURL(const string& url)
{
    if (url.empty())
    {
        return false;
    }

    CURLU* curlUrl = curl_url();

    if (curlUrl == nullptr)
    {
        return false;
    }

    CURLUcode result = curl_url_set(
        curlUrl,
        CURLUPART_URL,
        url.c_str(),
        0
    );

    if (result != CURLUE_OK)
    {
        curl_url_cleanup(curlUrl);
        return false;
    }

    char* scheme = nullptr;
    char* host = nullptr;

    CURLUcode schemeResult = curl_url_get(
        curlUrl,
        CURLUPART_SCHEME,
        &scheme,
        0
    );

    CURLUcode hostResult = curl_url_get(
        curlUrl,
        CURLUPART_HOST,
        &host,
        0
    );

    bool valid =
        schemeResult == CURLUE_OK &&
        hostResult == CURLUE_OK &&
        scheme != nullptr &&
        host != nullptr &&
        (string(scheme) == "http" || string(scheme) == "https") &&
        !string(host).empty();

    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(curlUrl);

    return valid;
}

// Read and validate command-line arguments.
string getURL(int argc, char* argv[])
{
    if (argc != 3)
    {
        throw invalid_argument(
            "Usage: ./fetchforge --file <URL>"
        );
    }

    string argument = argv[1];
    string url = argv[2];

    if (argument != "--file")
    {
        throw invalid_argument(
            "Invalid argument. Use --file <URL>."
        );
    }

    if (!isValidURL(url))
    {
        throw invalid_argument(
            "Invalid URL. Use a valid HTTP or HTTPS URL."
        );
    }

    return url;
}

// Extract a filename from the URL.
string getFilename(const string& url)
{
    size_t start = url.find("://") + 3;
    size_t pathStart = url.find('/', start);

    if (pathStart == string::npos)
    {
        return "download.bin";
    }

    size_t pathEnd = url.find_first_of("?#", pathStart);

    string path = url.substr(
        pathStart,
        pathEnd == string::npos
            ? string::npos
            : pathEnd - pathStart
    );

    // Remove trailing slashes.
    while (!path.empty() && path.back() == '/')
    {
        path.pop_back();
    }

    if (path.empty())
    {
        return "download.bin";
    }

    size_t lastSlash = path.find_last_of('/');
    string filename = path.substr(lastSlash + 1);

    if (filename.empty() || filename == "." || filename == "..")
    {
        return "download.bin";
    }

    // Avoid writing files with path separators in their names.
    if (filename.find('/') != string::npos ||
        filename.find('\\') != string::npos)
    {
        return "download.bin";
    }

    return filename;
}

// Find a filename that does not already exist.
fs::path getUniquePath(const fs::path& desiredPath)
{
    if (!fs::exists(desiredPath))
    {
        return desiredPath;
    }

    fs::path directory = desiredPath.parent_path();
    string stem = desiredPath.stem().string();
    string extension = desiredPath.extension().string();

    for (unsigned int index = 1; ; ++index)
    {
        fs::path candidate = directory /
            (stem + "_" + to_string(index) + extension);

        if (!fs::exists(candidate))
        {
            return candidate;
        }
    }
}

// Create a new temporary file without overwriting an existing file.
FILE* createTemporaryFile(const fs::path& path)
{
    int fd = open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_EXCL,
        0600
    );

    if (fd == -1)
    {
        return nullptr;
    }

    FILE* file = fdopen(fd, "wb");

    if (file == nullptr)
    {
        close(fd);
        unlink(path.c_str());
        return nullptr;
    }

    return file;
}

// Write downloaded data to a file.
size_t writeCallback(
    char* data,
    size_t size,
    size_t count,
    void* filePointer
)
{
    FILE* file = static_cast<FILE*>(filePointer);

    return fwrite(data, size, count, file);
}

// Download a file using libcurl.
void downloadFile(const string& url)
{
    CURL* curl = nullptr;
    FILE* file = nullptr;

    fs::create_directories("downloads");

string filename = getFilename(url);
fs::path desiredPath = fs::path("downloads") / filename;
fs::path outputPath = getUniquePath(desiredPath);
filename = outputPath.filename().string();
fs::path temporaryPath = outputPath.string() + ".part";

    cout << "\nURL: " << url << '\n';
    cout << "Connecting to Server...\n";

    // Initialize libcurl.
    CURLcode globalResult = curl_global_init(CURL_GLOBAL_DEFAULT);

    if (globalResult != CURLE_OK)
    {
        throw runtime_error(
            "Could not initialize libcurl."
        );
    }

    curl = curl_easy_init();

    if (curl == nullptr)
    {
        curl_global_cleanup();

        throw runtime_error(
            "Could not create a libcurl session."
        );
    }

    try
    {
        file = createTemporaryFile(temporaryPath);

        if (file == nullptr)
        {
            throw runtime_error(
                "Cannot create the output file. Check directory permissions."
            );
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        // Follow redirects safely.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);

        // Set connection and overall time limits.
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        // Treat HTTP error responses, such as 404, as failures.
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        // Identify the application to the server.
        curl_easy_setopt(
            curl,
            CURLOPT_USERAGENT,
            "FetchForge/1.0"
        );

        cout << "Downloading: " << filename << '\n';

        CURLcode result = curl_easy_perform(curl);

        // Check whether writing to the file failed.
        bool writeFailed = ferror(file) != 0;

        if (fclose(file) != 0)
        {
            writeFailed = true;
        }

        file = nullptr;

        if (result != CURLE_OK)
        {
            throw runtime_error(
                string("Download failed: ") +
                curl_easy_strerror(result)
            );
        }

        if (writeFailed)
        {
            throw runtime_error(
                "Failed to write the downloaded data to disk."
            );
        }

        // Obtain the HTTP response code.
        long responseCode = 0;

        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &responseCode
        );

        if (responseCode < 200 || responseCode >= 300)
        {
            throw runtime_error(
                "The server returned an unsuccessful HTTP response."
            );
        }

        // Rename the temporary file only after a successful download.
        fs::rename(temporaryPath, outputPath);

        cout << "\nDownload completed successfully!\n";
        cout << "Saved to: " << outputPath.string() << '\n';

        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    catch (...)
    {
        if (file != nullptr)
        {
            fclose(file);
        }

        if (curl != nullptr)
        {
            curl_easy_cleanup(curl);
        }

        curl_global_cleanup();

        // Remove an incomplete download.
        error_code error;
        fs::remove(temporaryPath, error);

        throw;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        string url = getURL(argc, argv);

        cout << "========================================\n";
        cout << "         FetchForge Downloader\n";
        cout << "========================================\n";

        downloadFile(url);
    }
    catch (const exception& error)
    {
        cerr << "\nError: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
