#pragma once
#include "pch.h"
#include <GLTFSDK/IStreamReader.h>
#include <fstream>
#include <filesystem>
#include <cassert>

using namespace std;
using namespace Microsoft::glTF;

class StreamReader :
    public IStreamReader
{
public:
    StreamReader(std::experimental::filesystem::path pathBase);
    std::shared_ptr<std::istream> GetInputStream(const std::string& filename) const override;

private:
    std::experimental::filesystem::path m_pathBase;
};
