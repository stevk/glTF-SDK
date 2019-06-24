#pragma once
#include "pch.h"
#include <GLTFSDK/IStreamWriter.h>
#include <fstream>
#include <filesystem>
#include <cassert>

using namespace std;
using namespace Microsoft::glTF;

class StreamWriter :
    public IStreamWriter
{
public:
    StreamWriter(std::experimental::filesystem::path pathBase);
    shared_ptr<std::ostream> GetOutputStream(const string& filename) const override;

private:
    std::experimental::filesystem::path m_pathBase;
};
