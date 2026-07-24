#include "DataFile.hpp"
#include "Logger.hpp"
#include "miniz.h"

#include <fstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

std::vector<uint8_t> readDataFile(const std::string& relativePath) {
    // 1) Try ADOCAO-data.zip
    {
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof(zip));
        if (mz_zip_reader_init_file(&zip, "ADOCAO-data.zip", 0)) {
            size_t size = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&zip, relativePath.c_str(), &size, 0);
            mz_zip_reader_end(&zip);
            if (data) {
                std::vector<uint8_t> result((uint8_t*)data, (uint8_t*)data + size);
                mz_free(data);
                LOG_D("DataFile: loaded %s from ADOCAO-data.zip (%zu bytes)", relativePath.c_str(), size);
                return result;
            }
        }
    }

    // 2) Try ADOCAO-data/ folder
    {
        std::string path = "ADOCAO-data/" + relativePath;
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            size_t size = (size_t)f.tellg();
            f.seekg(0);
            std::vector<uint8_t> result(size);
            f.read((char*)result.data(), size);
            LOG_D("DataFile: loaded %s from ADOCAO-data/ (%zu bytes)", relativePath.c_str(), size);
            return result;
        }
    }

    // 3) Fallback: direct path
    {
        std::ifstream f(relativePath, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            size_t size = (size_t)f.tellg();
            f.seekg(0);
            std::vector<uint8_t> result(size);
            f.read((char*)result.data(), size);
            return result;
        }
    }

    LOG_D("DataFile: %s not found in zip, data folder, or direct path", relativePath.c_str());
    return {};
}
