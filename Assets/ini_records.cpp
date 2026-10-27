// Assets/ini_records.cpp - the tuning pak record files. See ini_records.h.
#include "Assets/ini_records.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Assets {

bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::optional<float32> parseIniNumber(const std::string& token)
{
    std::string t = token;
    if (!t.empty() && (t.back() == 'f' || t.back() == 'F'))
        t.pop_back();
    if (t.empty())
        return std::nullopt;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (end != t.c_str() + t.size())
        return std::nullopt;
    return v;
}

const std::vector<std::string>* IniRecord::line(const std::string& key) const
{
    for (const auto& l : lines)
        if (!l.empty() && iequals(l[0], key))
            return &l;
    return nullptr;
}

std::vector<const std::vector<std::string>*> IniRecord::all(const std::string& key) const
{
    std::vector<const std::vector<std::string>*> out;
    for (const auto& l : lines)
        if (!l.empty() && iequals(l[0], key))
            out.push_back(&l);
    return out;
}

std::optional<float32> IniRecord::number(const std::string& key, std::size_t index) const
{
    const auto* l = line(key);
    if (!l || index >= l->size())
        return std::nullopt;
    return parseIniNumber((*l)[index]);
}

std::optional<std::string> IniRecord::text(const std::string& key, std::size_t index) const
{
    const auto* l = line(key);
    if (!l || index >= l->size())
        return std::nullopt;
    return (*l)[index];
}

std::vector<IniRecord> parseIniRecords(const std::string& text)
{
    std::vector<IniRecord> records;
    IniRecord* open = nullptr;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw))
    {
        if (const std::size_t hash = raw.find('#'); hash != std::string::npos)
            raw.erase(hash);
        std::vector<std::string> tokens;
        std::string tok;
        for (char c : raw)
        {
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
            {
                if (!tok.empty())
                    tokens.push_back(std::move(tok));
                tok.clear();
            }
            else
                tok += c;
        }
        if (!tok.empty())
            tokens.push_back(std::move(tok));
        if (tokens.empty())
            continue;
        if (iequals(tokens[0], "RECORD"))
        {
            records.push_back({tokens.size() > 1 ? tokens[1] : std::string(), {}});
            open = &records.back();
        }
        else if (iequals(tokens[0], "END"))
            open = nullptr;
        else if (open)
            open->lines.push_back(std::move(tokens));
    }
    return records;
}

const IniRecord* findRecord(const std::vector<IniRecord>& records, const std::string& name)
{
    for (const IniRecord& r : records)
        if (iequals(r.name, name))
            return &r;
    return nullptr;
}

} // namespace Assets
