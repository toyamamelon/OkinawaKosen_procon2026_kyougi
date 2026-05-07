# include <Siv3D.hpp>
# include <cmath>
# include <utility>
#include <climits>

using namespace s3d;

constexpr double kPi = 3.14159265358979323846;

void drawMoveGuide(const Font& uiFont)
{
	Vec2 center{ Scene::Width() - 140, Scene::Height() - 140 };
	double r = 42;

	Array<Vec2> pts;
	for (int i = 0; i < 6; ++i)
	{
		double ang = Math::ToRadians(60 * i - 30);
		pts << center + Vec2{ Math::Cos(ang) * r, Math::Sin(ang) * r };
	}

	Polygon hex{ pts };
	hex.draw(ColorF{ 0.15, 0.15, 0.18, 0.85 });
	hex.drawFrame(2, Palette::White);

	const Array<Vec2> labelOffsets =
	{
		Vec2{ 1.00,  0.00 },
		Vec2{ 0.55, -0.95 },
		Vec2{ -0.55, -0.95 },
		Vec2{ -1.00,  0.00 },
		Vec2{ -0.55,  0.95 },
		Vec2{ 0.55,  0.95 }
	};

	for (int i = 0; i < 6; ++i)
	{
		Vec2 pos = center + labelOffsets[i] * (r + 24);
		uiFont(Format(i + 1)).drawAt(pos, Palette::Yellow);
	}

	uiFont(U"Move").drawAt(center, Palette::White);
}

void drawStatusUI(const Font& font, int selectedAgent)
{
	font(U"Tab : switch").draw(20, 20, Palette::White);
	font(U"1-6 : move").draw(20, 45, Palette::White);
	font(U"Selected : {}"_fmt(selectedAgent)).draw(20, 70, Palette::White);
}

void drawCarIcon(const Vec2& center, const ColorF& bodyColor)
{
	// ===== 車体 =====
	// 黒い縁
	RoundRect{ RectF{ center.x - 38, center.y - 14, 76, 30 }, 8 }.draw(Palette::Black);
	// 本体
	RoundRect{ RectF{ center.x - 34, center.y - 10, 68, 22 }, 7 }.draw(bodyColor);

	// ===== 上の屋根部分 =====
	RoundRect{ RectF{ center.x - 18, center.y - 28, 34, 18 }, 6 }.draw(Palette::Black);
	RoundRect{ RectF{ center.x - 14, center.y - 24, 26, 12 }, 4 }.draw(bodyColor);

	// ===== 窓 =====
	RoundRect{ RectF{ center.x - 14, center.y - 18, 16, 10 }, 3 }.draw(Palette::Black);
	RoundRect{ RectF{ center.x - 12, center.y - 16, 12, 6 }, 2 }.draw(Palette::White);

	// ===== タイヤ =====
	Circle{ center.x - 18, center.y + 16, 12 }.draw(Palette::Black);
	Circle{ center.x - 18, center.y + 16, 9 }.draw(ColorF{ 0.25, 0.25, 0.25 });

	Circle{ center.x + 18, center.y + 16, 12 }.draw(Palette::Black);
	Circle{ center.x + 18, center.y + 16, 9 }.draw(ColorF{ 0.25, 0.25, 0.25 });
}

// =========================
// Hex coord (axial)
// =========================
struct HexCoord
{
	int q = 0;
	int r = 0;
};

static const Array<HexCoord> HEX_DIRS =
{
	HexCoord{ +1,  0 },
	HexCoord{ +1, -1 },
	HexCoord{  0, -1 },
	HexCoord{ -1,  0 },
	HexCoord{ -1, +1 },
	HexCoord{  0, +1 }
};

// =========================
// Terrain
// =========================
enum class Terrain : uint8
{
	Plain,
	Mountain,
	Lake,
	Road
};

// =========================
// Cell
// =========================
struct Cell
{
	Terrain terrain = Terrain::Plain;
	bool walkable = true;
	int stepCost = 2; // 時間
	int fuelCost = 2; // 燃料
};


int terrainStepCost(Terrain t)
{
	switch (t)
	{
	case Terrain::Plain:    return 2;
	case Terrain::Mountain: return 3;
	case Terrain::Road:     return 1;
	case Terrain::Lake:     return INT_MAX / 4;
	}
	return INT_MAX / 4;
}

int terrainFuelCost(Terrain t)
{
	switch (t)
	{
	case Terrain::Plain:    return 1;
	case Terrain::Mountain: return 2;
	case Terrain::Road:     return 2;
	case Terrain::Lake:     return INT_MAX / 4;
	}
	return INT_MAX / 4;
}

bool terrainWalkable(Terrain t)
{
	return (t != Terrain::Lake);
}
// =========================
// Agent
// =========================
enum class AgentType : uint8
{
	Patrol,   // 巡回車
	Supply    // 補給車
};

struct Agent
{
	int id = -1;
	int cellId = -1;
	AgentType type = AgentType::Patrol;
	int patrolIndex = -1;   // 巡回車だけ使う
};

struct PatrolState
{
	int fuel = 10;
	int fuelMax = 10;
	int udon = 0;
	HashSet<int> visitedToday;
};

struct Spot
{
	int id = -1;
	int cellId = -1;
	int series = 0;
	int stock = 1;
};

// =========================
// HexMap
// =========================
class HexMap
{
public:
	int width = 0;
	int height = 0;
	Array<Cell> cells;

	void init(int h, int w)
	{
		height = h;
		width = w;
		cells.assign(h * w, Cell{});
	}

	bool inBoundsRC(int row, int col) const
	{
		return (0 <= row && row < height && 0 <= col && col < width);
	}

	int indexRC(int row, int col) const
	{
		return row * width + col;
	}

	std::pair<int, int> rcFromIndex(int id) const
	{
		return { id / width, id % width };
	}

	static HexCoord oddrToAxial(int row, int col)
	{
		int q = col - (row - (row & 1)) / 2;
		int r = row;
		return { q, r };
	}

	static std::pair<int, int> axialToOddr(const HexCoord& h)
	{
		int row = h.r;
		int col = h.q + (h.r - (h.r & 1)) / 2;
		return { row, col };
	}

	HexCoord coordOf(int cellId) const
	{
		auto rc = rcFromIndex(cellId);
		return oddrToAxial(rc.first, rc.second);
	}

	int cellIdOf(const HexCoord& h) const
	{
		auto rc = axialToOddr(h);
		if (!inBoundsRC(rc.first, rc.second))
		{
			return -1;
		}
		return indexRC(rc.first, rc.second);
	}

	bool isValidCellId(int id) const
	{
		return (0 <= id && id < static_cast<int>(cells.size()));
	}

	bool isWalkable(int id) const
	{
		if (!isValidCellId(id))
		{
			return false;
		}
		return cells[id].walkable;
	}

	int neighbor(int cellId, int dir) const
	{
		if (!isValidCellId(cellId) || dir < 0 || dir >= 6)
		{
			return -1;
		}

		HexCoord h = coordOf(cellId);
		HexCoord nh{ h.q + HEX_DIRS[dir].q, h.r + HEX_DIRS[dir].r };
		int nid = cellIdOf(nh);

		if (nid == -1 || !isWalkable(nid))
		{
			return -1;
		}
		return nid;
	}

	Vec2 cellCenter(int cellId, double hexSize, const Vec2& origin) const
	{
		auto rc = rcFromIndex(cellId);
		HexCoord h = oddrToAxial(rc.first, rc.second);

		const double x = origin.x + hexSize * (std::sqrt(3.0) * h.q + std::sqrt(3.0) * 0.5 * h.r);
		const double y = origin.y + hexSize * (1.5 * h.r);

		return { x, y };
	}

	Polygon hexPolygon(const Vec2& center, double hexSize) const
	{
		Array<Vec2> pts;
		pts.reserve(6);

		for (int i = 0; i < 6; ++i)
		{
			const double ang = (60.0 * i - 30.0) * kPi / 180.0;
			pts << (center + Vec2{ hexSize * std::cos(ang), hexSize * std::sin(ang) });
		}

		return Polygon{ pts };
	}

	int hexDistance(int a, int b) const
	{
		HexCoord A = coordOf(a);
		HexCoord B = coordOf(b);

		int dx = A.q - B.q;
		int dz = A.r - B.r;
		int dy = -dx - dz;

		return (Abs(dx) + Abs(dy) + Abs(dz)) / 2;
	}

	Array<int> findPath(int start, int goal) const
	{
		Array<int> open;
		open << start;

		HashTable<int, int> came;
		HashTable<int, int> gScore;
		HashTable<int, int> fScore;

		gScore[start] = 0;
		fScore[start] = hexDistance(start, goal);

		while (!open.isEmpty())
		{
			int current = open.front();
			int best = fScore[current];

			for (int n : open)
			{
				if (fScore[n] < best)
				{
					best = fScore[n];
					current = n;
				}
			}

			if (current == goal)
			{
				Array<int> path;
				int cur = goal;

				while (came.contains(cur))
				{
					path << cur;
					cur = came[cur];
				}

				path << start;
				path.reverse();
				return path;
			}

			open.remove(current);

			for (int dir = 0; dir < 6; ++dir)
			{
				int next = neighbor(current, dir);
				if (next == -1) continue;

				int tentative = gScore[current] + cells[current].stepCost;

				if (!gScore.contains(next) || tentative < gScore[next])
				{
					came[next] = current;
					gScore[next] = tentative;
					fScore[next] = tentative + hexDistance(next, goal);

					if (!open.includes(next))
						open << next;
				}
			}
		}

		return {};
	}
};

// =========================
// Simple simulator
// =========================
class HexSimulator
{
public:
	HexMap map;
	Array<Agent> agents;
	Array<Spot> spots;
	Array<PatrolState> patrolStates;

	int addPatrolAgent(int cellId, int fuel = 10, int fuelMax = 10)
	{
		const int patrolIndex = static_cast<int>(patrolStates.size());
		patrolStates << PatrolState{ fuel, fuelMax, 0, {} };

		const int id = static_cast<int>(agents.size());
		agents << Agent{ id, cellId, AgentType::Patrol, patrolIndex };

		return id;
	}

	int addSupplyAgent(int cellId)
	{
		const int id = static_cast<int>(agents.size());
		agents << Agent{ id, cellId, AgentType::Supply, -1 };
		return id;
	}

	bool moveAgent(int agentId, int toCell)
	{
		if (agentId < 0 || agentId >= static_cast<int>(agents.size()))
		{
			return false;
		}

		Agent& a = agents[agentId];

		if (toCell == a.cellId)
		{
			return true;
		}

		if (!map.isValidCellId(toCell) || !map.isWalkable(toCell))
		{
			return false;
		}

		// 隣接チェック
		bool adjacent = false;
		for (int dir = 0; dir < 6; ++dir)
		{
			if (map.neighbor(a.cellId, dir) == toCell)
			{
				adjacent = true;
				break;
			}
		}

		if (!adjacent)
		{
			return false;
		}

		// 現在地のコストを使う
		const Cell& cell = map.cells[a.cellId];

		const int stepCost = cell.stepCost;
		const int fuelCost = cell.fuelCost;

		// 巡回車だけ燃料を使う
		if (a.type == AgentType::Patrol)
		{
			if (a.patrolIndex < 0 || a.patrolIndex >= static_cast<int>(patrolStates.size()))
			{
				return false;
			}

			PatrolState& ps = patrolStates[a.patrolIndex];
			if (ps.fuel < fuelCost)
			{
				return false;
			}
			ps.fuel -= fuelCost;
		}

		a.cellId = toCell;
		tryCollectUdon(agentId);
		refillFuelIfNeeded();
		return true;
	}

	bool moveAgentDir(int agentId, int dir)
	{
		if (agentId < 0 || agentId >= static_cast<int>(agents.size()))
		{
			return false;
		}

		int to = map.neighbor(agents[agentId].cellId, dir);
		if (to == -1)
		{
			return false;
		}

		return moveAgent(agentId, to);
	}

	bool tryCollectUdon(int agentId)
	{
		if (agentId < 0 || agentId >= static_cast<int>(agents.size()))
		{
			return false;
		}

		Agent& a = agents[agentId];
		if (a.type != AgentType::Patrol)
		{
			return false;
		}

		if (a.patrolIndex < 0 || a.patrolIndex >= static_cast<int>(patrolStates.size()))
		{
			return false;
		}

		PatrolState& ps = patrolStates[a.patrolIndex];

		for (auto& spot : spots)
		{
			if (spot.cellId != a.cellId)
			{
				continue;
			}

			// そのスポットは今日もう取っている
			if (ps.visitedToday.contains(spot.id))
			{
				return false;
			}

			if (spot.stock <= 0)
			{
				return false;
			}

			--spot.stock;
			++ps.udon;
			ps.visitedToday.insert(spot.id);
			return true;
		}

		return false;
	}

	void refillFuelIfNeeded()
	{
		for (auto& patrol : agents)
		{
			if (patrol.type != AgentType::Patrol)
			{
				continue;
			}

			if (patrol.patrolIndex < 0 || patrol.patrolIndex >= static_cast<int>(patrolStates.size()))
			{
				continue;
			}

			for (const auto& supply : agents)
			{
				if (supply.type == AgentType::Supply && supply.cellId == patrol.cellId)
				{
					patrolStates[patrol.patrolIndex].fuel = patrolStates[patrol.patrolIndex].fuelMax;
					break;
				}
			}
		}
	}

	void beginNewDay()
	{
		for (auto& ps : patrolStates)
		{
			ps.visitedToday.clear();
		}
	}
};

void drawAllAgentInfoUI(const Font& font, const HexSimulator& sim)
{
	const double x = 20.0;
	const double lineH = 24.0;
	const double headerH = 28.0;
	const double panelH = headerH + lineH * sim.agents.size() + 16.0;

	const double y = Max(20.0, Scene::Height() - panelH - 20.0);

	RectF panel{ x - 6, y - 10, 320, panelH };
	panel.draw(ColorF{ 0.1, 0.1, 0.12, 0.75 });
	panel.drawFrame(1, Palette::White);

	font(U"Agents").draw(x, y - 2, Palette::White);

	for (size_t i = 0; i < sim.agents.size(); ++i)
	{
		const Agent& a = sim.agents[i];
		const String typeText = (a.type == AgentType::Patrol) ? U"巡回車" : U"補給車";

		String statusText;
		if (a.type == AgentType::Patrol && a.patrolIndex >= 0 && a.patrolIndex < static_cast<int>(sim.patrolStates.size()))
		{
			const PatrolState& ps = sim.patrolStates[a.patrolIndex];
			statusText = U"Fuel {}/{}  Udon {}"_fmt(ps.fuel, ps.fuelMax, ps.udon);
		}
		else
		{
			statusText = U"Fuel ∞  Udon -";
		}

		const double yy = y + headerH + lineH * i;

		font(U"#{}  {}  {}  Cell {}"_fmt(
			i, typeText, statusText, a.cellId
		)).draw(x, yy, Palette::White);
	}
}
ColorF agentColor(const Agent& a, int selectedAgentIndex, int index)
{
	if (a.type == AgentType::Patrol)
	{
		return (index == selectedAgentIndex)
			? ColorF{ 0.95, 0.20, 0.20 }   // 赤
		: ColorF{ 0.55, 0.00, 0.00 };  // 濃い赤
	}
	else
	{
		return (index == selectedAgentIndex)
			? ColorF{ 0.20, 0.55, 0.95 }   // 青
		: ColorF{ 0.00, 0.20, 0.55 };  // 濃い青
	}
}
// =========================
// Main
// =========================
void Main()
{
	Window::Resize(960, 640);
	Window::SetStyle(WindowStyle::Sizable);
	Scene::SetBackground(ColorF{ 0.15, 0.18, 0.22 });

	HexSimulator sim;
	sim.map.init(5, 5);
	// --- ここからマップ作成 ---

	auto setTerrain = [&](int r, int c, Terrain t)
		{
			if (!sim.map.inBoundsRC(r, c))
			{
				return;
			}

			int id = sim.map.indexRC(r, c);
			auto& cell = sim.map.cells[id];

			cell.terrain = t;
			cell.walkable = terrainWalkable(t);
			cell.stepCost = terrainStepCost(t);
			cell.fuelCost = terrainFuelCost(t);
		};

	setTerrain(1, 2, Terrain::Mountain);
	setTerrain(1, 3, Terrain::Mountain);

	setTerrain(2, 2, Terrain::Lake);
	setTerrain(3, 2, Terrain::Lake);

	setTerrain(2, 0, Terrain::Road);
	setTerrain(2, 1, Terrain::Road);
	setTerrain(1, 1, Terrain::Road);
	setTerrain(0, 1, Terrain::Road);

	setTerrain(0, 3, Terrain::Plain);
	// エージェント配置
	sim.addPatrolAgent(sim.map.indexRC(2, 1), 10, 10);
	sim.addSupplyAgent(sim.map.indexRC(4, 4));

	//スポット配置
	sim.spots << Spot{ 0, sim.map.indexRC(0, 3), 0, 1 };
	sim.spots << Spot{ 1, sim.map.indexRC(3, 1), 1, 2 };
	sim.spots << Spot{ 2, sim.map.indexRC(4, 3), 2, 1 };

	const double hexSize = 32.0;
	const Vec2 origin{ 140, 120 };
	const Font font{ 18 };
	const Font uiFont{ 20 };

	int selectedAgent = 0;
	int targetSpot = 0;
	Array<int> currentPath;
	bool pathDirty = true;

	while (System::Update())
	{

		// エージェント切り替え
		if (KeyTab.down())
		{
			selectedAgent = (selectedAgent + 1) % sim.agents.size();
		}

		// 1～6 キーで6方向移動
		if (Key1.down()) { sim.moveAgentDir(selectedAgent, 0); pathDirty = true; }
		if (Key2.down()) { sim.moveAgentDir(selectedAgent, 1); pathDirty = true; }
		if (Key3.down()) { sim.moveAgentDir(selectedAgent, 2); pathDirty = true; }
		if (Key4.down()) { sim.moveAgentDir(selectedAgent, 3); pathDirty = true; }
		if (Key5.down()) { sim.moveAgentDir(selectedAgent, 4); pathDirty = true; }
		if (Key6.down()) { sim.moveAgentDir(selectedAgent, 5); pathDirty = true; }
		if (KeySpace.down())
		{
			if (currentPath.size() >= 2)
			{
				sim.moveAgent(selectedAgent, currentPath[1]);
				pathDirty = true;
			}
			else if (currentPath.size() == 1)
			{
				targetSpot = (targetSpot + 1) % sim.spots.size();
				pathDirty = true;
			}
		}

		if (pathDirty)
		{
			int start = sim.agents[selectedAgent].cellId;
			int goal = sim.spots[targetSpot].cellId;
			currentPath = sim.map.findPath(start, goal);
			pathDirty = false;
		}

		// 盤面描画
		for (int id = 0; id < sim.map.width * sim.map.height; ++id)
		{
			Vec2 c = sim.map.cellCenter(id, hexSize, origin);
			Polygon hex = sim.map.hexPolygon(c, hexSize - 1.5);

			ColorF fill = ColorF{ 0.85, 0.85, 0.85 };

			if (sim.map.cells[id].terrain == Terrain::Mountain) fill = ColorF{ 0.1, 0.6, 0.2 };
			if (sim.map.cells[id].terrain == Terrain::Lake)     fill = ColorF{ 0.2, 0.45, 0.8 };
			if (sim.map.cells[id].terrain == Terrain::Road)     fill = ColorF{ 0.9, 0.8, 0.35 };

			hex.draw(fill);

			if (currentPath.includes(id))
			{
				hex.draw(ColorF{ 1.0, 0.2, 0.2, 0.35 }); // 薄い赤
			}

			hex.drawFrame(1, Palette::Black);
			font(id).drawAt(12, c, Palette::Black);
		}
		// エージェント描画
		for (int i = 0; i < static_cast<int>(sim.agents.size()); ++i)
		{
			const auto& a = sim.agents[i];
			Vec2 p = sim.map.cellCenter(a.cellId, hexSize, origin);

			ColorF col = agentColor(a, selectedAgent, i);
			drawCarIcon(p, col);
		}

		auto spotColor = [](int series)
			{
				static const Array<ColorF> colors =
				{
					ColorF{ 1.0, 0.3, 0.3 },
					ColorF{ 0.3, 1.0, 0.3 },
					ColorF{ 0.3, 0.5, 1.0 },
					ColorF{ 1.0, 0.8, 0.3 },
					ColorF{ 0.8, 0.3, 1.0 },
					ColorF{ 0.3, 1.0, 1.0 }
				};
				return colors[series % colors.size()];
			};

		auto drawSpot = [&](const Spot& s)
			{
				Vec2 p = sim.map.cellCenter(s.cellId, hexSize, origin);
				ColorF col = spotColor(s.series);

				Array<Vec2> pts =
				{
					p + Vec2{  0, -11 },
					p + Vec2{ 11,   0 },
					p + Vec2{  0,  11 },
					p + Vec2{ -11,   0 }
				};

				Polygon diamond{ pts };
				diamond.draw(col);
				diamond.drawFrame(2, Palette::Black);

				for (int i = 0; i < s.stock; ++i)
				{
					font(Format(s.stock)).drawAt(p + Vec2{ 0, 18 }, Palette::White);
				}
			};

		for (const auto& s : sim.spots)
		{
			drawSpot(s);
		}

		drawMoveGuide(uiFont);
		drawStatusUI(font, selectedAgent);
		drawAllAgentInfoUI(font, sim);
	}
}
