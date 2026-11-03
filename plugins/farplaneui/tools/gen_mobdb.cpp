/**
 * Converts the mobdb addon's data (data/<zone>.lua: basic monster info by target index / name;
 * zonedata/<zone>.lua: PhoenixXI server data - drops, drop groups, steal, respawn, spawn conditions;
 * thtable.lua: treasure hunter brackets) into FarplaneUI's own per-zone files:
 *   <out>\<zone>.dat   tab-separated lines (see below)      <out>\th.dat   treasure hunter table
 *
 *   M  key(i<index> | n<name>)  name  flags  job  minLv  maxLv  immunities  respawn  mod0..mod11  drops(id,id)
 *      flags: 1 notorious 2 aggro 4 link 8 trueSight 16 sight 32 sound 64 blood 128 magic 256 ja 512 scent
 *   T  templateId  name  notorious  respawn  spawnTypes(a,b)  drops(item:rate;..)  groups(rate/total/item:w,item:w|..)  steal(id,id)  stealAudit
 *   S  spawnIndex  templateId          R  spawnIndex  min  max  script
 *   P  phId  phName  nmId  nmName  chance  cooldownMin  cooldownMax  type  dayOnly  nightOnly  status   (nmdata.lua: what a PH can pop)
 *   N  nmId  nmName  phId  phName  type  status                                                        (nmdata.lua: an NM's placeholders)
 *
 *   cl /std:c++latest /EHsc /I. gen_mobdb.cpp luatable.cpp      (from tools\)
 *   gen_mobdb.exe <mobdb addon dir> <out dir>
 */
#include "luatable.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace farplaneui;
namespace fs = std::filesystem;

namespace
{
    const char* kMods[12] = {"Slashing", "Piercing", "H2H", "Impact", "Fire", "Ice", "Wind", "Earth", "Lightning", "Water", "Light", "Dark"};
    auto Clean(std::string s) -> std::string
    {
        for (char& c : s)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        return s;
    }
    auto Num(double v) -> std::string
    {
        char b[32]{};
        std::snprintf(b, sizeof(b), "%g", v);
        return b;
    }
    void Basic(std::ofstream& out, const std::string& key, const lua::Value& m)
    {
        const char* fl[] = {"Notorious", "Aggro", "Link", "TrueSight", "Sight", "Sound", "Blood", "Magic", "JA", "Scent"};
        int flags = 0;
        for (int i = 0; i < 10; ++i)
            if (m[fl[i]].Bool())
                flags |= 1 << i;
        out << "M\t" << key << "\t" << Clean(m["Name"].Str()) << "\t" << flags << "\t" << m["Job"].Int() << "\t" << m["MinLevel"].Int() << "\t"
            << m["MaxLevel"].Int() << "\t" << m["Immunities"].Int() << "\t" << m["Respawn"].Int();
        for (const char* k : kMods)
            out << "\t" << Num(m["Modifiers"][k].Num(1.0));
        out << "\t";
        for (size_t i = 1; i <= m["Drops"].Size(); ++i)
            out << (i > 1 ? "," : "") << m["Drops"][static_cast<long long>(i)].Int();
        out << "\n";
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: gen_mobdb <mobdb addon dir> <out dir>\n");
        return 1;
    }
    const fs::path src = argv[1], dst = argv[2];
    fs::create_directories(dst);
    std::map<int, std::pair<fs::path, fs::path>> zones;
    for (const auto& e : fs::directory_iterator(src / "data"))
        if (e.path().extension() == ".lua")
            zones[std::atoi(e.path().stem().string().c_str())].first = e.path();
    if (fs::exists(src / "zonedata"))
        for (const auto& e : fs::directory_iterator(src / "zonedata"))
            if (e.path().extension() == ".lua")
                zones[std::atoi(e.path().stem().string().c_str())].second = e.path();

    // nmdata.lua is statement-style (placeholders[id] = {...}; notorious[id] = {...};): parse each line's table.
    std::map<int, std::vector<std::string>> nmLines;
    size_t nmCount = 0;
    {
        std::ifstream nf(src / "nmdata.lua");
        std::string line;
        while (std::getline(nf, line))
        {
            const bool ph = line.rfind("placeholders[", 0) == 0, nm = line.rfind("notorious[", 0) == 0;
            if (!ph && !nm)
                continue;
            const size_t lb = line.find('['), rb = line.find(']'), eq = line.find('=', rb);
            if (lb == std::string::npos || rb == std::string::npos || eq == std::string::npos)
                continue;
            const long long id = std::atoll(line.substr(lb + 1, rb - lb - 1).c_str());
            std::string rhs = line.substr(eq + 1);
            while (!rhs.empty() && (rhs.back() == ';' || rhs.back() == ' ' || rhs.back() == '\r'))
                rhs.pop_back();
            const lua::Value v = lua::Parse("return " + rhs);
            const int zone     = static_cast<int>((id >> 12) & 0xFFF);
            const auto& list   = v[ph ? "NMs" : "PHs"];
            for (size_t i = 1; i <= list.Size(); ++i)
            {
                const auto& e = list[static_cast<long long>(i)];
                const std::string eid = std::to_string(static_cast<long long>(e["Id"].Num()));
                std::string out;
                if (ph)
                    out = "P\t" + std::to_string(id) + "\t" + Clean(v["Name"].Str()) + "\t" + eid + "\t" + Clean(e["Name"].Str()) + "\t" + Num(e["Chance"].Num()) +
                          "\t" + std::to_string(e["CooldownMin"].Int()) + "\t" + std::to_string(e["CooldownMax"].Int()) + "\t" + e["Type"].Str() + "\t" +
                          (e["DayOnly"].Bool() ? "1" : "0") + "\t" + (e["NightOnly"].Bool() ? "1" : "0") + "\t" + e["Status"].Str();
                else
                    out = "N\t" + std::to_string(id) + "\t" + Clean(v["Name"].Str()) + "\t" + eid + "\t" + Clean(e["Name"].Str()) + "\t" + e["Type"].Str() + "\t" +
                          e["Status"].Str();
                nmLines[zone].push_back(out);
            }
            ++nmCount;
        }
    }
    for (const auto& [zone, lines] : nmLines)
        (void)zones[zone]; // make sure the zone gets a file

    size_t mobs = 0, templates = 0;
    for (const auto& [zone, files] : zones)
    {
        std::ofstream out(dst / (std::to_string(zone) + ".dat"), std::ios::binary);
        out << "# FarplaneUI monster data, zone " << zone << ". Generated by tools/gen_mobdb.cpp from the mobdb addon (MIT; 2.x server data from PhoenixXI / LandSandBoat).\n";
        if (!files.first.empty())
        {
            const lua::Value d = lua::ParseFile(files.first.string());
            if (d["Names"].IsTable())
                for (const auto& [name, m] : d["Names"].tab->named)
                    Basic(out, "n" + Clean(name), m), ++mobs;
            if (d["Indices"].IsTable())
                for (const auto& [idx, m] : d["Indices"].tab->indexed)
                    Basic(out, "i" + std::to_string(idx), m), ++mobs;
        }
        if (!files.second.empty())
        {
            const lua::Value z = lua::ParseFile(files.second.string());
            if (z["Templates"].IsTable())
                for (const auto& [id, t] : z["Templates"].tab->indexed)
                {
                    out << "T\t" << id << "\t" << Clean(t["Name"].Str()) << "\t" << (t["Notorious"].Bool() ? 1 : 0) << "\t" << t["Respawn"].Int() << "\t";
                    for (size_t i = 1; i <= t["SpawnType"].Size(); ++i)
                        out << (i > 1 ? "," : "") << t["SpawnType"][static_cast<long long>(i)].Str();
                    out << "\t";
                    for (size_t i = 1; i <= t["Drops"].Size(); ++i)
                    {
                        const auto& dr = t["Drops"][static_cast<long long>(i)];
                        out << (i > 1 ? ";" : "") << dr["Item"].Int() << ":" << dr["Rate"].Int();
                    }
                    out << "\t";
                    for (size_t i = 1; i <= t["Groups"].Size(); ++i)
                    {
                        const auto& g = t["Groups"][static_cast<long long>(i)];
                        out << (i > 1 ? "|" : "") << g["Rate"].Int() << "/" << g["Total"].Int() << "/";
                        for (size_t k = 1; k <= g["Items"].Size(); ++k)
                            out << (k > 1 ? "," : "") << g["Items"][static_cast<long long>(k)]["Item"].Int() << ":" << g["Items"][static_cast<long long>(k)]["Weight"].Int();
                    }
                    out << "\t";
                    for (size_t i = 1; i <= t["Steal"].Size(); ++i)
                        out << (i > 1 ? "," : "") << t["Steal"][static_cast<long long>(i)].Int();
                    out << "\t" << (t["StealAudit"].Bool() ? 1 : 0) << "\n";
                    ++templates;
                }
            if (z["Spawns"].IsTable())
                for (const auto& [idx, tid] : z["Spawns"].tab->indexed)
                    out << "S\t" << idx << "\t" << tid.Int() << "\n";
            if (z["Respawn"].IsTable())
                for (const auto& [idx, r] : z["Respawn"].tab->indexed)
                    out << "R\t" << idx << "\t" << r["Min"].Int() << "\t" << r["Max"].Int() << "\t" << (r["Script"].Bool() ? 1 : 0) << "\n";
        }
        if (const auto it = nmLines.find(zone); it != nmLines.end())
            for (const auto& l : it->second)
                out << l << "\n";
    }
    // Treasure hunter: brackets, then one row per TH level.
    const lua::Value th = lua::ParseFile((src / "thtable.lua").string());
    if (th.IsTable())
    {
        std::ofstream out(dst / "th.dat", std::ios::binary);
        out << "B";
        for (size_t i = 1; i <= th["Brackets"].Size(); ++i)
            out << "\t" << th["Brackets"][static_cast<long long>(i)].Int();
        out << "\n";
        for (const auto& [lvl, row] : th["Table"].tab->indexed)
        {
            out << "L\t" << lvl;
            for (size_t i = 1; i <= row.Size(); ++i)
                out << "\t" << row[static_cast<long long>(i)].Int();
            out << "\n";
        }
    }
    std::printf("%zu zones, %zu mobs, %zu server templates, %zu NM / placeholder records -> %s\n", zones.size(), mobs, templates, nmCount, dst.string().c_str());
    return 0;
}
