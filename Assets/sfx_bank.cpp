#include "Assets/sfx_bank.h"
#include "Core/platform.h"

#include "Assets/binary_reader.h"

#include <iostream>

namespace Assets {

bool SfxBank::load(const std::string& dir)
{
    loaded_ = false;
    records_.clear();
    nameToIndex_.clear();
    names_.clear();

    const std::string lstPath = dir + "/sfx.lst";
    const std::string sdtPath = dir + "/sfx.SDT";
    const std::string rawPath = dir + "/sfx.RAW";

    // sfx.lst: lines starting with "-- " give the names, in record order.
    {
        std::ifstream lst(Engine::Platform::resolvePath(lstPath), std::ios::binary);
        if (!lst)
        {
            std::cerr << "[sfx] cannot open " << lstPath << "\n";
            return false;
        }
        std::string line;
        size_t idx = 0;
        while (std::getline(lst, line))
        {
            if (!line.empty() && line.back() == '\r') // CRLF file: getline keeps the '\r' off Windows
                line.pop_back();
            if (line.size() > 3 && line.compare(0, 3, "-- ") == 0)
            {
                nameToIndex_[line.substr(3)] = idx++;
                names_.push_back(line.substr(3));
            }
        }
    }

    // sfx.SDT: array of 20-byte records.
    {
        auto sdt = readFileBytes(sdtPath);
        if (!sdt)
        {
            std::cerr << "[sfx] " << sdt.error().message << "\n";
            return false;
        }
        if (sdt->size() % 20 != 0)
        {
            std::cerr << "[sfx] SDT size " << sdt->size() << " not multiple of 20\n";
            return false;
        }
        records_.resize(sdt->size() / 20);
        ByteReader r(*sdt);
        for (Record& rec : records_)
        {
            uint32 unused = 0;
            r.readU32(rec.offset);
            r.readU32(rec.length);
            r.readU32(rec.sampleRate);
            r.readU32(unused);
            r.readU32(unused);
        }
    }

    rawFile_.open(Engine::Platform::resolvePath(rawPath), std::ios::binary | std::ios::ate);
    if (!rawFile_)
    {
        std::cerr << "[sfx] cannot open " << rawPath << "\n";
        return false;
    }
    rawSize_ = static_cast<uint64>(rawFile_.tellg());

    loaded_ = true;
    std::cout << "[sfx] loaded " << nameToIndex_.size() << " names, " << records_.size() << " records\n";
    return true;
}

bool SfxBank::getSample(const std::string& name, SfxSample& out) const
{
    const auto it = nameToIndex_.find(name);
    if (!loaded_ || it == nameToIndex_.end() || it->second >= records_.size())
        return false;

    const Record& rec = records_[it->second];
    if (static_cast<uint64>(rec.offset) + rec.length > rawSize_)
        return false;

    std::vector<uint8_t> pcm(rec.length);
    {
        std::lock_guard<std::mutex> lock(rawMutex_);
        rawFile_.clear(); // a previous short read would otherwise leave failbit set forever
        rawFile_.seekg(static_cast<std::streamoff>(rec.offset), std::ios::beg);
        rawFile_.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(rec.length));
        if (rawFile_.gcount() != static_cast<std::streamsize>(rec.length))
            return false;
    }
    out.pcm = std::move(pcm);
    out.sampleRate = rec.sampleRate;
    return true;
}

} // namespace Assets
