#include "Level.hpp"

#include <fstream>

bool Level::load(const std::string& path)
{
    std::ifstream in{ path };
    if (!in.is_open())
        return false;

    rows.clear();
    spores.clear();
    spikes.clear();
    decor.clear();

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            rows.push_back(line);
    }
    if (rows.empty())
        return false;

    height = static_cast<int>(rows.size());
    width = static_cast<int>(rows.front().size());

    for (int r = 0; r < height; r++)
    {
        if (static_cast<int>(rows[r].size()) != width)
            return false;
        for (int c = 0; c < width; c++)
        {
            const Cell cell{ c, r };
            switch (rows[r][c])
            {
                case 'P': playerStart = cell; break;
                case 'S': shrine = cell; break;
                case 'M': slime = cell; break;
                case 'D': door = cell; break;
                case 'o': spores.push_back(cell); break;
                case '^': spikes.push_back(cell); break;
                case 'v': case 'm': case 'c':
                    decor.push_back({ rows[r][c], cell });
                    break;
                default: break;
            }
        }
    }
    return true;
}

bool Level::solid(int c, int r) const
{
    if (c < 0 || r < 0 || c >= width || r >= height)
        return true;
    return rows[r][c] == '#';
}
