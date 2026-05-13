# include <Siv3D.hpp>
# include <cmath>
# include <utility>
# include <climits>
# include <queue>

using namespace s3d;

constexpr double kPi = 3.14159265358979323846;

// =========================
// Terrain / RoadLevel
// =========================
enum class Terrain : uint8 { Plain, Mountain, Lake, Road };
enum class RoadLevel : uint8 { Clear = 0, Crowded = 1, Jammed = 2 };
enum class AgentType : uint8 { Patrol, Supply };

// =========================
// HexCoord (axial)
// =========================
struct HexCoord { int q = 0, r = 0; };

static const Array<HexCoord> HEX_DIRS =
{
	{ +1,  0 }, { +1, -1 }, {  0, -1 },
	{ -1,  0 }, { -1, +1 }, {  0, +1 }
};

// =========================
// MapConfig  ← 試合を通じて変わらない固定情報
// =========================
struct CellInfo
{
	Terrain terrain = Terrain::Plain;
	bool    walkable = true;
};

struct SpotInfo
{
	int id = -1;
	int cellId = -1;
	int series = 0;
	int stockMax = 1;
};

struct AgentInit
{
	int       id = -1;
	AgentType type = AgentType::Patrol;
	int       cellId = -1;
	int       fuelMax = 10; // 補給車は無視
};

struct MapConfig
{
	int width = 0;
	int height = 0;
	Array<CellInfo>  cells;
	Array<SpotInfo>  spots;
	Array<AgentInit> agentInits;

	void init(int h, int w)
	{
		height = h; width = w;
		cells.assign(h * w, CellInfo{});
	}

	bool inBoundsRC(int r, int c) const
	{
		return (0 <= r && r < height && 0 <= c && c < width);
	}

	int indexRC(int r, int c) const { return r * width + c; }

	std::pair<int, int> rcFromIndex(int id) const { return { id / width, id % width }; }

	static HexCoord oddrToAxial(int row, int col)
	{
		return { col - (row - (row & 1)) / 2, row };
	}

	static std::pair<int, int> axialToOddr(const HexCoord& h)
	{
		return { h.r, h.q + (h.r - (h.r & 1)) / 2 };
	}

	HexCoord coordOf(int cellId) const
	{
		auto rc = rcFromIndex(cellId);
		return oddrToAxial(rc.first, rc.second);
	}

	int cellIdOf(const HexCoord& h) const
	{
		auto rc = axialToOddr(h);
		if (!inBoundsRC(rc.first, rc.second)) return -1;
		return indexRC(rc.first, rc.second);
	}

	bool isValid(int id) const { return 0 <= id && id < (int)cells.size(); }

	int neighbor(int cellId, int dir) const
	{
		if (!isValid(cellId) || dir < 0 || dir >= 6) return -1;
		HexCoord h = coordOf(cellId);
		int nid = cellIdOf({ h.q + HEX_DIRS[dir].q, h.r + HEX_DIRS[dir].r });
		if (nid == -1 || !cells[nid].walkable) return -1;
		return nid;
	}

	Vec2 cellCenter(int cellId, double hexSize, const Vec2& origin) const
	{
		auto rc = rcFromIndex(cellId);
		HexCoord h = oddrToAxial(rc.first, rc.second);
		return {
			origin.x + hexSize * (std::sqrt(3.0) * h.q + std::sqrt(3.0) * 0.5 * h.r),
			origin.y + hexSize * 1.5 * h.r
		};
	}

	Polygon hexPolygon(const Vec2& center, double hexSize) const
	{
		Array<Vec2> pts;
		for (int i = 0; i < 6; ++i)
		{
			double ang = (60.0 * i - 30.0) * kPi / 180.0;
			pts << center + Vec2{ hexSize * std::cos(ang), hexSize * std::sin(ang) };
		}
		return Polygon{ pts };
	}

	int hexDistance(int a, int b) const
	{
		HexCoord A = coordOf(a), B = coordOf(b);
		int dx = A.q - B.q, dz = A.r - B.r, dy = -dx - dz;
		return (Abs(dx) + Abs(dy) + Abs(dz)) / 2;
	}
};

// =========================
// コスト関数  ← 1か所にまとめる
// =========================
int getStepCost(int cellId, const MapConfig& map, const Array<RoadLevel>& roadLevels)
{
	switch (map.cells[cellId].terrain)
	{
	case Terrain::Plain:    return 2;
	case Terrain::Mountain: return 3;
	case Terrain::Lake:     return INT_MAX / 4;
	case Terrain::Road:
		switch (roadLevels[cellId])
		{
		case RoadLevel::Clear:   return 1;
		case RoadLevel::Crowded: return 2;
		case RoadLevel::Jammed:  return 4;
		}
	}
	return INT_MAX / 4;
}

int getFuelCost(int cellId, const MapConfig& map)
{
	switch (map.cells[cellId].terrain)
	{
	case Terrain::Plain:    return 1;
	case Terrain::Mountain: return 2;
	case Terrain::Road:     return 2;
	default:                return INT_MAX / 4;
	}
}

// A* : MapConfig + 今日のroadLevelsを受け取る
Array<int> findPath(int start, int goal,
	const MapConfig& map, const Array<RoadLevel>& roadLevels)
{
	if (!map.isValid(start) || !map.isValid(goal)) return {};
	if (start == goal) return { start };

	using T = std::pair<int, int>;
	std::priority_queue<T, std::vector<T>, std::greater<T>> open;

	HashTable<int, int> came, gScore;
	gScore[start] = 0;
	open.push({ map.hexDistance(start, goal), start });

	while (!open.empty())
	{
		auto top = open.top(); open.pop();
		int f = top.first, cur = top.second;

		if (cur == goal)
		{
			Array<int> path;
			for (int c = goal; came.contains(c); c = came[c]) path << c;
			path << start;
			path.reverse();
			return path;
		}

		for (int dir = 0; dir < 6; ++dir)
		{
			int next = map.neighbor(cur, dir);
			if (next == -1) continue;

			int tentative = gScore[cur] + getStepCost(cur, map, roadLevels);
			if (!gScore.contains(next) || tentative < gScore[next])
			{
				came[next] = cur;
				gScore[next] = tentative;
				open.push({ tentative + map.hexDistance(next, goal), next });
			}
		}
	}
	return {};
}

// =========================
// DayState  ← サーバーから毎日受け取る情報
// =========================
struct AgentState
{
	int cellId = -1;
	int fuel = 0;  // 補給車は無視
};

struct PatrolExtra
{
	int          udon = 0;
	HashSet<int> visitedToday;
};

struct DayState
{
	int day = 1;
	int totalDays = 6;
	int stepInDay = 0;
	int stepsPerDay = 20;

	Array<RoadLevel>  roadLevels;  // cellId -> 道路状態（毎日サーバーから来る）
	Array<AgentState> agentStates; // agentId -> 位置・燃料
	Array<int>        spotStock;   // spotId  -> 今日の在庫

	// 試合通算・ビジュアライザ用（通信では来ない、自前管理）
	Array<PatrolExtra> patrolExtras; // 巡回車インデックス -> うどん数・訪問記録

	// --- サーバーから受け取ったデータで初期化するイメージ ---
	void initFromConfig(const MapConfig& cfg)
	{
		int numCells = cfg.width * cfg.height;
		roadLevels.assign(numCells, RoadLevel::Clear);

		agentStates.resize(cfg.agentInits.size());
		for (int i = 0; i < (int)cfg.agentInits.size(); ++i)
		{
			agentStates[i].cellId = cfg.agentInits[i].cellId;
			agentStates[i].fuel = cfg.agentInits[i].fuelMax;
		}

		spotStock.resize(cfg.spots.size());
		for (int i = 0; i < (int)cfg.spots.size(); ++i)
			spotStock[i] = cfg.spots[i].stockMax;

		// 巡回車の数だけ PatrolExtra を確保
		int patrolCount = 0;
		for (const auto& a : cfg.agentInits)
			if (a.type == AgentType::Patrol) ++patrolCount;
		patrolExtras.resize(patrolCount);
	}

	void beginNewDay()
	{
		stepInDay = 0;
		++day;
		for (auto& pe : patrolExtras) pe.visitedToday.clear();
		// spotStock は サーバーから受け取るか、ここで stockMax に戻す
	}

	bool isGameOver() const { return day > totalDays; }
};

// =========================
// HexSimulator  ← MapConfig + DayState を使う
// =========================
class HexSimulator
{
public:
	MapConfig cfg;
	DayState  day;

	// agentId が何番目の巡回車か（-1なら補給車）
	int patrolIndexOf(int agentId) const
	{
		int pi = 0;
		for (int i = 0; i < agentId; ++i)
			if (cfg.agentInits[i].type == AgentType::Patrol) ++pi;
		return (cfg.agentInits[agentId].type == AgentType::Patrol) ? pi : -1;
	}

	bool moveAgent(int agentId, int toCell)
	{
		if (agentId < 0 || agentId >= (int)cfg.agentInits.size()) return false;

		AgentState& as = day.agentStates[agentId];
		AgentType   type = cfg.agentInits[agentId].type;

		if (toCell == as.cellId) return true; // 待機

		if (!cfg.isValid(toCell) || !cfg.cells[toCell].walkable) return false;

		// 隣接チェック
		bool adjacent = false;
		for (int dir = 0; dir < 6; ++dir)
			if (cfg.neighbor(as.cellId, dir) == toCell) { adjacent = true; break; }
		if (!adjacent) return false;

		// 現在地基準のコスト
		int steps = getStepCost(as.cellId, cfg, day.roadLevels);
		int fuel = getFuelCost(as.cellId, cfg);

		// ステップ不足
		if (day.stepInDay + steps > day.stepsPerDay) return false;

		// 巡回車：燃料チェック
		if (type == AgentType::Patrol)
		{
			if (as.fuel < fuel) return false;
			as.fuel -= fuel;
		}

		as.cellId = toCell;
		day.stepInDay += steps;

		tryCollectUdon(agentId);
		refillFuelAtCell(toCell);

		return true;
	}

	bool moveAgentDir(int agentId, int dir)
	{
		if (agentId < 0 || agentId >= (int)cfg.agentInits.size()) return false;
		int to = cfg.neighbor(day.agentStates[agentId].cellId, dir);
		if (to == -1) return false;
		return moveAgent(agentId, to);
	}

	bool tryCollectUdon(int agentId)
	{
		if (cfg.agentInits[agentId].type != AgentType::Patrol) return false;
		int pi = patrolIndexOf(agentId);
		if (pi < 0) return false;

		PatrolExtra& pe = day.patrolExtras[pi];
		AgentState& as = day.agentStates[agentId];

		for (int si = 0; si < (int)cfg.spots.size(); ++si)
		{
			if (cfg.spots[si].cellId != as.cellId) continue;
			if (pe.visitedToday.contains(si))      return false;
			if (day.spotStock[si] <= 0)            return false;

			--day.spotStock[si];
			++pe.udon;
			pe.visitedToday.emplace(si);
			return true;
		}
		return false;
	}

	bool refillFuelAtCell(int cellId)
	{
		if (!cfg.isValid(cellId)) return false;

		bool       hasSupply = false;
		Array<int> patrolIds;

		for (int i = 0; i < (int)cfg.agentInits.size(); ++i)
		{
			if (day.agentStates[i].cellId != cellId) continue;
			if (cfg.agentInits[i].type == AgentType::Supply) hasSupply = true;
			else if (cfg.agentInits[i].type == AgentType::Patrol) patrolIds << i;
		}

		if (!hasSupply) return false;

		for (int id : patrolIds)
			day.agentStates[id].fuel = cfg.agentInits[id].fuelMax;

		return !patrolIds.isEmpty();
	}

	int totalUdon() const
	{
		int sum = 0;
		for (const auto& pe : day.patrolExtras) sum += pe.udon;
		return sum;
	}

	// 日付を進める（移動とは独立した操作）
	// 本番ではサーバーから次の DayState を受け取って上書きする
	void endDay()
	{
		day.beginNewDay();

		// ローカルシミュレーター用: 在庫を最大値まで補充
		// 本番ではサーバーから来る spotStock で上書きする
		for (int si = 0; si < (int)cfg.spots.size(); ++si)
			day.spotStock[si] = cfg.spots[si].stockMax;
	}

	Array<int> pathTo(int agentId, int goalCell) const
	{
		return findPath(day.agentStates[agentId].cellId, goalCell, cfg, day.roadLevels);
	}
};

// =========================
// Draw helpers
// =========================
void drawCarIcon(const Vec2& c, const ColorF& col)
{
	RoundRect{ RectF{ c.x - 38, c.y - 14, 76, 30 }, 8 }.draw(Palette::Black);
	RoundRect{ RectF{ c.x - 34, c.y - 10, 68, 22 }, 7 }.draw(col);
	RoundRect{ RectF{ c.x - 18, c.y - 28, 34, 18 }, 6 }.draw(Palette::Black);
	RoundRect{ RectF{ c.x - 14, c.y - 24, 26, 12 }, 4 }.draw(col);
	RoundRect{ RectF{ c.x - 14, c.y - 18, 16, 10 }, 3 }.draw(Palette::Black);
	RoundRect{ RectF{ c.x - 12, c.y - 16, 12,  6 }, 2 }.draw(Palette::White);
	Circle{ c.x - 18, c.y + 16, 12 }.draw(Palette::Black);
	Circle{ c.x - 18, c.y + 16,  9 }.draw(ColorF{ 0.25,0.25,0.25 });
	Circle{ c.x + 18, c.y + 16, 12 }.draw(Palette::Black);
	Circle{ c.x + 18, c.y + 16,  9 }.draw(ColorF{ 0.25,0.25,0.25 });
}

ColorF spotColor(int series)
{
	static const Array<ColorF> cols =
	{
		{1.0,0.3,0.3},{0.3,1.0,0.3},{0.3,0.5,1.0},
		{1.0,0.8,0.3},{0.8,0.3,1.0},{0.3,1.0,1.0}
	};
	return cols[series % cols.size()];
}

void drawMoveGuide(const Font& f)
{
	Vec2 center{ Scene::Width() - 140.0, Scene::Height() - 140.0 };
	double r = 42;
	Array<Vec2> pts;
	for (int i = 0; i < 6; ++i)
	{
		double ang = Math::ToRadians(60 * i - 30);
		pts << center + Vec2{ Math::Cos(ang) * r, Math::Sin(ang) * r };
	}
	Polygon hex{ pts };
	hex.draw(ColorF{ 0.15,0.15,0.18,0.85 });
	hex.drawFrame(2, Palette::White);
	const Array<Vec2> off = { {1,0},{0.55,-0.95},{-0.55,-0.95},{-1,0},{-0.55,0.95},{0.55,0.95} };
	for (int i = 0; i < 6; ++i)
		f(Format(i + 1)).drawAt(center + off[i] * (r + 24), Palette::Yellow);
	f(U"Move").drawAt(center, Palette::White);
}

// =========================
// Main
// =========================
void Main()
{
	Window::Resize(960, 640);
	Window::SetStyle(WindowStyle::Sizable);
	Scene::SetBackground(ColorF{ 0.15,0.18,0.22 });

	// --- MapConfig を作る（本番ではサーバーからパース）---
	HexSimulator sim;
	sim.cfg.init(5, 5);

	auto setTerrain = [&](int r, int c, Terrain t)
		{
			if (!sim.cfg.inBoundsRC(r, c)) return;
			int id = sim.cfg.indexRC(r, c);
			sim.cfg.cells[id].terrain = t;
			sim.cfg.cells[id].walkable = (t != Terrain::Lake);
		};

	setTerrain(1, 2, Terrain::Mountain); setTerrain(1, 3, Terrain::Mountain);
	setTerrain(2, 2, Terrain::Lake);     setTerrain(3, 2, Terrain::Lake);
	setTerrain(2, 0, Terrain::Road);     setTerrain(2, 1, Terrain::Road);
	setTerrain(1, 1, Terrain::Road);     setTerrain(0, 1, Terrain::Road);

	// エージェント初期設定
	sim.cfg.agentInits << AgentInit{ 0, AgentType::Patrol, sim.cfg.indexRC(2,1), 10 };
	sim.cfg.agentInits << AgentInit{ 1, AgentType::Supply, sim.cfg.indexRC(4,4), 0 };

	// スポット設定
	sim.cfg.spots << SpotInfo{ 0, sim.cfg.indexRC(0,3), 0, 1 };
	sim.cfg.spots << SpotInfo{ 1, sim.cfg.indexRC(3,1), 1, 2 };
	sim.cfg.spots << SpotInfo{ 2, sim.cfg.indexRC(4,3), 2, 1 };

	// --- DayState を初期化（本番では1日目のサーバー応答をパース）---
	sim.day.totalDays = 6;
	sim.day.stepsPerDay = 20;
	sim.day.initFromConfig(sim.cfg);

	const double hexSize = 32.0;
	const Vec2   origin{ 140, 120 };
	const Font   font{ 18 };
	const Font   uiFont{ 20 };

	int  selectedAgent = 0;
	int  targetSpot = 0;
	bool pathDirty = true;
	Array<int> currentPath;

	while (System::Update())
	{
		if (sim.day.isGameOver())
		{
			Rect{ 0,0,Scene::Width(),Scene::Height() }.draw(ColorF{ 0,0,0,0.65 });
			font(U"=== 試合終了 ===").drawAt(Scene::Center() + Vec2{ 0,-30 }, Palette::Yellow);
			font(U"獲得うどん: {}玉"_fmt(sim.totalUdon())).drawAt(Scene::Center(), Palette::White);
			continue;
		}

		// --- 入力 ---
		if (KeyTab.down()) { selectedAgent = (selectedAgent + 1) % (int)sim.cfg.agentInits.size(); pathDirty = true; }

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
				targetSpot = (targetSpot + 1) % (int)sim.cfg.spots.size();
				pathDirty = true;
			}
		}

		// Enter: 日付を進める（残りステップに関わらず強制的に翌日へ）
		if (KeyEnter.down())
		{
			sim.endDay();
			pathDirty = true;
		}

		if (pathDirty)
		{
			currentPath = sim.pathTo(selectedAgent, sim.cfg.spots[targetSpot].cellId);
			pathDirty = false;
		}

		// --- 盤面描画 ---
		for (int id = 0; id < sim.cfg.width * sim.cfg.height; ++id)
		{
			Vec2    c = sim.cfg.cellCenter(id, hexSize, origin);
			Polygon hex = sim.cfg.hexPolygon(c, hexSize - 1.5);

			ColorF fill{ 0.85,0.85,0.85 };
			switch (sim.cfg.cells[id].terrain)
			{
			case Terrain::Mountain: fill = ColorF{ 0.1,0.6,0.2 }; break;
			case Terrain::Lake:     fill = ColorF{ 0.2,0.45,0.8 }; break;
			case Terrain::Road:
				switch (sim.day.roadLevels[id])
				{
				case RoadLevel::Clear:   fill = ColorF{ 0.9,0.8,0.35 }; break;
				case RoadLevel::Crowded: fill = ColorF{ 0.95,0.55,0.1 }; break;
				case RoadLevel::Jammed:  fill = ColorF{ 0.85,0.15,0.15 }; break;
				}
				break;
			default: break;
			}

			hex.draw(fill);
			if (currentPath.includes(id)) hex.draw(ColorF{ 1,0.2,0.2,0.35 });
			if (id == sim.cfg.spots[targetSpot].cellId) hex.draw(ColorF{ 1,1,0,0.25 });
			hex.drawFrame(1, Palette::Black);
			font(id).drawAt(12, c, Palette::Black);
		}

		// スポット描画
		for (int si = 0; si < (int)sim.cfg.spots.size(); ++si)
		{
			const auto& s = sim.cfg.spots[si];
			Vec2 p = sim.cfg.cellCenter(s.cellId, hexSize, origin);
			ColorF col = spotColor(s.series);
			int   stk = sim.day.spotStock[si];

			Array<Vec2> pts = { p + Vec2{0,-11}, p + Vec2{11,0}, p + Vec2{0,11}, p + Vec2{-11,0} };
			Polygon diamond{ pts };
			diamond.draw(stk > 0 ? col : ColorF{ col.r, col.g, col.b, 0.3 });
			diamond.drawFrame(2, Palette::Black);
			font(stk).drawAt(p + Vec2{ 0,18 }, Palette::White);
		}

		// エージェント描画
		for (int i = 0; i < (int)sim.cfg.agentInits.size(); ++i)
		{
			Vec2 p = sim.cfg.cellCenter(sim.day.agentStates[i].cellId, hexSize, origin);

			ColorF col;
			if (sim.cfg.agentInits[i].type == AgentType::Patrol)
				col = (i == selectedAgent) ? ColorF{ 0.95,0.20,0.20 } : ColorF{ 0.55,0,0 };
			else
				col = (i == selectedAgent) ? ColorF{ 0.20,0.55,0.95 } : ColorF{ 0,0.20,0.55 };

			drawCarIcon(p, col);
		}

		// UI
		drawMoveGuide(uiFont);
		font(U"Tab:switch  1-6:move  Space:auto  Enter:next day").draw(20, 20, Palette::White);
		font(U"Day {}/{}  Step {}/{}"_fmt(
			sim.day.day, sim.day.totalDays,
			sim.day.stepInDay, sim.day.stepsPerDay))
			.draw(20, 48, Palette::Cyan);
		font(U"Udon: {}玉"_fmt(sim.totalUdon())).draw(20, 72, Palette::Yellow);
		font(U"Selected: {}"_fmt(selectedAgent)).draw(20, 96, Palette::White);

		// エージェント情報パネル
		{
			const double x = 20, lineH = 24, headH = 28;
			const double panelH = headH + lineH * sim.cfg.agentInits.size() + 16;
			const double y = Max(120.0, Scene::Height() - panelH - 20);
			RectF panel{ x - 6,y - 10,340,panelH };
			panel.draw(ColorF{ 0.1,0.1,0.12,0.75 });
			panel.drawFrame(1, Palette::White);
			font(U"Agents").draw(x, y - 2, Palette::White);

			int pi = 0;
			for (int i = 0; i < (int)sim.cfg.agentInits.size(); ++i)
			{
				const auto& init = sim.cfg.agentInits[i];
				const auto& as = sim.day.agentStates[i];
				ColorF      col = (i == selectedAgent) ? Palette::Yellow : Palette::White;
				double      yy = y + headH + lineH * i;

				if (init.type == AgentType::Patrol)
				{
					const auto& pe = sim.day.patrolExtras[pi++];
					font(U"#{} 巡回 Fuel:{}/{} Udon:{} Cell:{}"_fmt(
						i, as.fuel, init.fuelMax, pe.udon, as.cellId))
						.draw(x, yy, col);
				}
				else
				{
					font(U"#{} 補給 Fuel:∞ Cell:{}"_fmt(i, as.cellId)).draw(x, yy, col);
				}
			}
		}
	}
}
