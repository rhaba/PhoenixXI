/**
 * Farplane UI - built-in monster data (see mobdb.hpp; file format in tools/gen_mobdb.cpp).
 */

#include "mobdb.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace farplaneui
{
    namespace
    {
        auto Split(const std::string& s, char sep) -> std::vector<std::string>
        {
            std::vector<std::string> out;
            size_t b = 0;
            for (;;)
            {
                const size_t e = s.find(sep, b);
                out.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
                if (e == std::string::npos)
                    return out;
                b = e + 1;
            }
        }
        // Letters and digits only, lower case (mobdb's SameName: "Bull [Herd1]" == "bullherd1").
        auto Key(const char* s) -> std::string
        {
            std::string k;
            for (const char* p = s != nullptr ? s : ""; *p != '\0'; ++p)
                if (std::isalnum(static_cast<unsigned char>(*p)))
                    k += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
            return k;
        }
        auto Rate(double r) -> std::string
        {
            char b[32]{};
            ::sprintf_s(b, "%.2f", r);
            std::string t = b;
            while (!t.empty() && t.back() == '0')
                t.pop_back();
            if (!t.empty() && t.back() == '.')
                t.pop_back();
            return t + "%";
        }
    } // namespace

    void MobDb::Load(const std::string& dir, uint32_t zone)
    {
        if (zone == zone_ && dir == dir_)
            return;
        zone_ = zone, dir_ = dir;
        byIndex_.clear(), byName_.clear(), templates_.clear(), spawns_.clear(), respawnOverride_.clear();
        placeholders_.clear(), notorious_.clear();
        if (!thLoaded_)
        {
            thLoaded_ = true;
            std::ifstream th(dir + "th.dat");
            std::string line;
            while (std::getline(th, line))
            {
                const auto f = Split(line, '\t');
                if (f.size() > 1 && f[0] == "B")
                    for (size_t i = 1; i < f.size(); ++i)
                        thBrackets_.push_back(std::atoi(f[i].c_str()));
                else if (f.size() > 2 && f[0] == "L")
                    for (size_t i = 2; i < f.size(); ++i)
                        thTable_[std::atoi(f[1].c_str())].push_back(std::atoi(f[i].c_str()));
            }
        }
        std::ifstream in(dir + std::to_string(zone) + ".dat");
        if (!in)
        {
            status_ = "no data for zone " + std::to_string(zone) + " in " + dir;
            return;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            const auto f = Split(line, '\t');
            if (f[0] == "M" && f.size() >= 22)
            {
                MobInfo m;
                m.name = f[2];
                const int fl = std::atoi(f[3].c_str());
                m.notorious = fl & 1, m.aggro = fl & 2, m.link = fl & 4, m.trueSight = fl & 8, m.sight = fl & 16;
                m.sound = fl & 32, m.blood = fl & 64, m.magic = fl & 128, m.ja = fl & 256, m.scent = fl & 512;
                m.job = std::atoi(f[4].c_str()), m.minLevel = std::atoi(f[5].c_str()), m.maxLevel = std::atoi(f[6].c_str());
                m.immunities = static_cast<uint32_t>(std::strtoul(f[7].c_str(), nullptr, 10));
                m.respawn = std::atoi(f[8].c_str());
                for (int i = 0; i < MobInfo::kModCount; ++i)
                    m.mods[i] = static_cast<float>(std::atof(f[9 + static_cast<size_t>(i)].c_str()));
                if (f.size() > 21 && !f[21].empty())
                    for (const auto& d : Split(f[21], ','))
                        m.drops.push_back(std::atoi(d.c_str()));
                if (f[1][0] == 'i')
                    byIndex_[static_cast<uint32_t>(std::atoi(f[1].c_str() + 1))] = std::move(m);
                else
                    byName_[f[1].substr(1)] = std::move(m);
            }
            else if (f[0] == "T" && f.size() >= 10)
            {
                MobServer t;
                t.name = f[2], t.notorious = f[3] == "1";
                t.respawnMin = t.respawnMax = std::atoi(f[4].c_str());
                if (!f[5].empty())
                    t.spawnTypes = Split(f[5], ',');
                if (!f[6].empty())
                    for (const auto& d : Split(f[6], ';'))
                        if (const size_t c = d.find(':'); c != std::string::npos)
                            t.drops.emplace_back(std::atoi(d.substr(0, c).c_str()), std::atoi(d.substr(c + 1).c_str()));
                if (!f[7].empty())
                    for (const auto& g : Split(f[7], '|'))
                    {
                        const auto p = Split(g, '/');
                        if (p.size() < 3)
                            continue;
                        MobServer::Group gr;
                        gr.rate = std::atoi(p[0].c_str()), gr.total = std::max(1, std::atoi(p[1].c_str()));
                        for (const auto& it : Split(p[2], ','))
                            if (const size_t c = it.find(':'); c != std::string::npos)
                                gr.items.emplace_back(std::atoi(it.substr(0, c).c_str()), std::atoi(it.substr(c + 1).c_str()));
                        t.groups.push_back(std::move(gr));
                    }
                if (!f[8].empty())
                    for (const auto& s : Split(f[8], ','))
                        t.steal.push_back(std::atoi(s.c_str()));
                t.stealAudit = f[9] == "1";
                templates_[std::atoi(f[1].c_str())] = std::move(t);
            }
            else if (f[0] == "S" && f.size() >= 3)
                spawns_[std::atoi(f[1].c_str())] = std::atoi(f[2].c_str());
            else if (f[0] == "P" && f.size() >= 12)
            {
                NmRecord& r = placeholders_[static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10))];
                r.name      = f[2];
                NmLink l;
                l.id = static_cast<uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10)), l.name = f[4], l.chance = std::atof(f[5].c_str());
                l.cooldownMin = std::atoi(f[6].c_str()), l.cooldownMax = std::atoi(f[7].c_str()), l.type = f[8];
                l.dayOnly = f[9] == "1", l.nightOnly = f[10] == "1", l.status = f[11];
                r.links.push_back(std::move(l));
            }
            else if (f[0] == "N" && f.size() >= 7)
            {
                NmRecord& r = notorious_[static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10))];
                r.name      = f[2];
                NmLink l;
                l.id = static_cast<uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10)), l.name = f[4], l.type = f[5], l.status = f[6];
                r.links.push_back(std::move(l));
            }
            else if (f[0] == "R" && f.size() >= 5)
            {
                MobServer r;
                r.respawnMin = std::atoi(f[2].c_str()), r.respawnMax = std::atoi(f[3].c_str()), r.respawnScript = f[4] == "1";
                respawnOverride_[std::atoi(f[1].c_str())] = std::move(r);
            }
        }
        status_ = std::to_string(byIndex_.size() + byName_.size()) + " mobs, " + std::to_string(templates_.size()) + " spawn templates (zone " +
                  std::to_string(zone) + ")";
    }

    auto MobDb::Find(uint32_t targetIndex, const char* name) const -> const MobInfo*
    {
        if (auto it = byIndex_.find(targetIndex); it != byIndex_.end())
            return &it->second;
        if (name != nullptr)
            if (auto it = byName_.find(name); it != byName_.end())
                return &it->second;
        return nullptr;
    }

    auto MobDb::Placeholder(uint32_t serverId) const -> const NmRecord*
    {
        const auto it = placeholders_.find(serverId);
        return it != placeholders_.end() ? &it->second : nullptr;
    }
    auto MobDb::Notorious(uint32_t serverId) const -> const NmRecord*
    {
        const auto it = notorious_.find(serverId);
        return it != notorious_.end() ? &it->second : nullptr;
    }
    auto MobDb::SameName(const char* a, const char* b) -> bool
    {
        return a != nullptr && b != nullptr && Key(a) == Key(b);
    }

    auto MobDb::Server(uint32_t serverId, const char* memoryName) const -> const MobServer*
    {
        if (serverId == 0 || ((serverId >> 12) & 0xFFF) != zone_)
            return nullptr;
        const int spawn = static_cast<int>(serverId & 0xFFF);
        const auto s = spawns_.find(spawn);
        if (s == spawns_.end())
            return nullptr;
        const auto t = templates_.find(s->second);
        if (t == templates_.end() || Key(memoryName) != Key(t->second.name.c_str()))
            return nullptr; // custom layout: the name in memory isn't the server's name for this id
        static MobServer copy; // template + this spawn's respawn override
        copy = t->second;
        if (const auto r = respawnOverride_.find(spawn); r != respawnOverride_.end())
            copy.respawnMin = r->second.respawnMin, copy.respawnMax = r->second.respawnMax, copy.respawnScript = r->second.respawnScript;
        return &copy;
    }

    // xi.combat.treasureHunter.getDropRate: a rate falls into the first bracket it is at least, and is
    // replaced by that bracket's value at this TH level. Guaranteed (10000) and zero pass through.
    auto MobDb::DropRate(int rate, int th) const -> int
    {
        if (rate >= 10000 || rate <= 0 || thBrackets_.empty())
            return std::clamp(rate, 0, 10000);
        const auto row = thTable_.find(std::clamp(th, 0, 14));
        if (row == thTable_.end())
            return rate;
        for (size_t b = 0; b < thBrackets_.size(); ++b)
            if (rate >= thBrackets_[b])
                return b < row->second.size() ? row->second[b] : rate;
        return rate;
    }

    // Each roll is independent, so an item on several rolls shows the chance of at least one.
    auto MobDb::Drops(const MobServer& m, int th) const -> std::vector<DropLine>
    {
        struct Acc
        {
            double miss{1};
            int rolls{0};
            std::string group;
        };
        std::map<int, Acc> acc;
        std::vector<int> order;
        auto add = [&](int item, double chance, const std::string& group) {
            if (acc.find(item) == acc.end())
                order.push_back(item);
            Acc& a = acc[item];
            a.miss *= 1.0 - chance;
            ++a.rolls;
            a.group = group;
        };
        for (const auto& [item, rate] : m.drops)
            add(item, DropRate(rate, th) / 10000.0, {});
        for (const auto& g : m.groups)
        {
            const double gc = DropRate(g.rate, th) / 10000.0;
            bool equal      = true;
            for (const auto& it : g.items)
                equal &= it.second == g.items.front().second;
            for (const auto& [item, weight] : g.items)
            {
                const double share = static_cast<double>(weight) / g.total;
                char b[32]{};
                if (equal)
                    ::sprintf_s(b, "1 of %d", static_cast<int>(std::lround(static_cast<double>(g.total) / std::max(1, weight))));
                else
                    ::sprintf_s(b, "%s pick", Rate(share * 100).c_str());
                add(item, gc * share, b);
            }
        }
        std::vector<DropLine> out;
        for (const int item : order)
        {
            const Acc& a = acc[item];
            DropLine l;
            l.item = item, l.rate = (1.0 - a.miss) * 100.0, l.rolls = a.rolls;
            l.text = a.rolls > 1 ? Rate(l.rate) + " (" + std::to_string(a.rolls) + " rolls)" : !a.group.empty() ? Rate(l.rate) + " (" + a.group + ")" : Rate(l.rate);
            out.push_back(std::move(l));
        }
        std::stable_sort(out.begin(), out.end(), [](const DropLine& a, const DropLine& b) { return a.rate > b.rate; });
        return out;
    }
} // namespace farplaneui
