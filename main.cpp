#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kWindowWidth = 1240;
constexpr int kWindowHeight = 760;
constexpr int kTileSize = 56;
constexpr int kTileStepY = 42;
constexpr int kTileFaceDepth = 10;
constexpr int kGridWidth = 100;
constexpr int kGridHeight = 100;
constexpr int kGridX = 24;
constexpr int kGridY = 96;
constexpr int kViewWidth = 820;
constexpr int kViewHeight = 550;
constexpr int kPanelX = 870;
constexpr int kPanelWidth = 344;
constexpr int kFarmShuttleStopX = 5;
constexpr int kFarmShuttleStopY = 16;
constexpr float kFarmShuttleReturnX = 4.0f;
constexpr float kFarmShuttleReturnY = 16.0f;
constexpr float kCampusSpawnX = static_cast<float>(kGridX + 250);
constexpr float kCampusSpawnY = static_cast<float>(kGridY + 400);
constexpr float kBuilding301SpawnX = static_cast<float>(kGridX + 388);
constexpr float kBuilding301SpawnY = static_cast<float>(kGridY + 380);

SDL_Color color(int r, int g, int b, int a = 255) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &rect);
}

void drawRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

void fillQuad(SDL_Renderer* renderer, std::array<SDL_FPoint, 4> points, SDL_Color c) {
    SDL_Vertex vertices[4]{};
    for (int i = 0; i < 4; ++i) {
        vertices[i].position = points[i];
        vertices[i].color = c;
        vertices[i].tex_coord = SDL_FPoint{0.0f, 0.0f};
    }
    int indices[6]{0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

SDL_Color shade(SDL_Color c, int delta) {
    auto clamp = [](int value) {
        return static_cast<Uint8>(std::clamp(value, 0, 255));
    };
    return SDL_Color{clamp(c.r + delta), clamp(c.g + delta), clamp(c.b + delta), c.a};
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

class Text {
public:
    ~Text() {
        shutdown();
    }

    void shutdown() {
        if (small_) TTF_CloseFont(small_);
        if (medium_) TTF_CloseFont(medium_);
        if (large_) TTF_CloseFont(large_);
        small_ = nullptr;
        medium_ = nullptr;
        large_ = nullptr;
    }

    void init() {
        std::vector<std::string> paths;
        if (const char* envPath = std::getenv("FARM_FONT")) {
            paths.emplace_back(envPath);
        }
        paths.emplace_back("assets/fonts/DejaVuSans.ttf");
        paths.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
        paths.emplace_back("/Library/Fonts/Arial.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");

        std::string chosen;
        for (const auto& path : paths) {
            TTF_Font* probe = TTF_OpenFont(path.c_str(), 16);
            if (probe) {
                TTF_CloseFont(probe);
                chosen = path;
                break;
            }
        }
        if (chosen.empty()) {
            throw std::runtime_error("Could not find a TTF font. Set FARM_FONT to a .ttf file.");
        }

        small_ = TTF_OpenFont(chosen.c_str(), 15);
        medium_ = TTF_OpenFont(chosen.c_str(), 19);
        large_ = TTF_OpenFont(chosen.c_str(), 28);
        if (!small_ || !medium_ || !large_) {
            throw std::runtime_error("Could not open font: " + chosen);
        }
    }

    void draw(SDL_Renderer* renderer, const std::string& message, int x, int y, SDL_Color c, int size = 0) const {
        TTF_Font* font = size == 2 ? large_ : (size == 1 ? medium_ : small_);
        if (!font || message.empty()) return;

        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, message.c_str(), c);
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_FreeSurface(surface);
        if (!texture) return;
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    int lineHeight(int size = 0) const {
        TTF_Font* font = size == 2 ? large_ : (size == 1 ? medium_ : small_);
        return font ? TTF_FontHeight(font) : 18;
    }

private:
    TTF_Font* small_{nullptr};
    TTF_Font* medium_{nullptr};
    TTF_Font* large_{nullptr};
};

enum class Weather { Sunny, Cloudy, Rain, Storm };
enum class Action { Hoe, Seed, WateringCan, AxePick, Hand, Build };
enum class InteractionResult { None, Keep, Remove };
enum class TileKind { Grass, Soil, Path, Pond, BuildingArea, IndoorFloor };
enum class AnimalKind { Chicken, Cow };
enum class Mode { Farm, Fishing, Memory, Shop, Debugging, Help };
enum class Location { Farm, Campus, Building301, BreadboardFarmLab, GradStudentRanchLab, ResearchLab };
enum class Direction { Down, Up, Left, Right };
enum class CropType { Turnip, Potato };
enum class DeviceType { Resistor, Capacitor, Diode, Mosfet, Finfet };
enum class ResearcherType { UndergradIntern, MasterStudent, PhdStudent, Postdoc };
enum class ResearchPanel { Construction, Upgrades };
enum class DecorKind { Tree, Rock, Flower, Fence, Scarecrow, House, Market, MarketKeeper, Bed, Table, SnuBusStop };
enum class NpcKind { MarketKeeper };

struct DeviceInfo {
    DeviceType type;
    std::string name;
    int cost;
    float growSeconds;
    float failureChance;
    int sellValue;
    int researchReward;
    int unlockLevel;
};

struct DeviceSlot {
    bool occupied{false};
    bool ready{false};
    DeviceType type{DeviceType::Resistor};
    float growth{0.0f};
};

struct ResearcherInfo {
    ResearcherType type;
    std::string name;
    int cost;
    int researchPerTick;
    int unlockLevel;
};

struct ResearcherSlot {
    bool occupied{false};
    ResearcherType type{ResearcherType::UndergradIntern};
    float burnoutTimer{0.0f};
};

const std::array<DeviceInfo, 5> kDeviceCatalog{{
    {DeviceType::Resistor, "RESISTOR", 20, 7.0f, 0.04f, 45, 2, 1},
    {DeviceType::Capacitor, "CAPACITOR", 32, 9.5f, 0.06f, 70, 3, 1},
    {DeviceType::Diode, "DIODE", 55, 13.0f, 0.10f, 120, 6, 3},
    {DeviceType::Mosfet, "MOSFET", 95, 18.0f, 0.16f, 210, 12, 5},
    {DeviceType::Finfet, "FINFET", 160, 24.0f, 0.22f, 380, 22, 7}
}};

const std::array<ResearcherInfo, 4> kResearcherCatalog{{
    {ResearcherType::UndergradIntern, "UNDERGRAD INTERN", 45, 1, 1},
    {ResearcherType::MasterStudent, "MASTER STUDENT", 85, 3, 2},
    {ResearcherType::PhdStudent, "PHD STUDENT", 145, 6, 3},
    {ResearcherType::Postdoc, "POSTDOC", 230, 10, 5}
}};

std::string weatherName(Weather weather) {
    switch (weather) {
        case Weather::Sunny: return "SUNNY";
        case Weather::Cloudy: return "CLOUDY";
        case Weather::Rain: return "RAIN";
        case Weather::Storm: return "STORM";
    }
    return "UNKNOWN";
}

std::string locationName(Location location) {
    switch (location) {
        case Location::Farm: return "FARM";
        case Location::Campus: return "CAMPUS";
        case Location::Building301: return "BUILDING 301";
        case Location::BreadboardFarmLab: return "BREADBOARD FARM LAB";
        case Location::GradStudentRanchLab: return "GRAD STUDENT RANCH LAB";
        case Location::ResearchLab: return "RESEARCH LAB";
    }
    return "LOCATION";
}

bool isUseKey(SDL_Keycode key) {
    return key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_e;
}

const DeviceInfo& deviceInfo(DeviceType type) {
    for (const auto& info : kDeviceCatalog) {
        if (info.type == type) return info;
    }
    return kDeviceCatalog.front();
}

const ResearcherInfo& researcherInfo(ResearcherType type) {
    for (const auto& info : kResearcherCatalog) {
        if (info.type == type) return info;
    }
    return kResearcherCatalog.front();
}

std::string actionName(Action action) {
    switch (action) {
        case Action::Hoe: return "HOE";
        case Action::Seed: return "PLANT TOOL";
        case Action::WateringCan: return "WATER";
        case Action::AxePick: return "AXE/PICK";
        case Action::Hand: return "HAND";
        case Action::Build: return "BUILD";
    }
    return "TOOL";
}

std::string animalName(AnimalKind kind) {
    return kind == AnimalKind::Chicken ? "CHICKEN" : "COW";
}

std::string cropName(CropType type) {
    return type == CropType::Turnip ? "TURNIP" : "POTATO";
}

std::string seedName(CropType type) {
    return cropName(type) + " SEED";
}

int cropSellPrice(const std::string& crop) {
    if (crop == "POTATO") return 22;
    return 14;
}

std::string tileKindName(TileKind kind) {
    switch (kind) {
        case TileKind::Grass: return "GRASS";
        case TileKind::Soil: return "TILLED SOIL";
        case TileKind::Path: return "PATH";
        case TileKind::Pond: return "POND";
        case TileKind::BuildingArea: return "BUILDING PAD";
        case TileKind::IndoorFloor: return "INDOOR FLOOR";
    }
    return "TILE";
}

bool baseTileWalkable(TileKind kind) {
    switch (kind) {
        case TileKind::Grass:
        case TileKind::Soil:
        case TileKind::Path:
        case TileKind::IndoorFloor:
            return true;
        case TileKind::Pond:
        case TileKind::BuildingArea:
            return false;
    }
    return false;
}

constexpr int kStaminaHoe = 4;
constexpr int kStaminaPlant = 3;
constexpr int kStaminaWater = 4;
constexpr int kStaminaHarvest = 3;
constexpr int kStaminaClear = 6;
constexpr int kStaminaBuildFence = 8;
constexpr int kStaminaBuildBarn = 14;
constexpr int kStaminaFish = 5;
constexpr int kStaminaPlantDevice = 4;
constexpr int kStaminaHarvestDevice = 4;

int edibleStaminaValue(const std::string& item) {
    if (item == "FISH") return 22;
    if (item == "POTATO") return 16;
    if (item == "TURNIP") return 12;
    return 0;
}

class Money {
public:
    explicit Money(int amount = 0) : amount_(amount) {}

    int value() const { return amount_; }

    bool spend(int cost) {
        if (amount_ < cost) return false;
        amount_ -= cost;
        return true;
    }

    void add(int amount) { amount_ += amount; }

    Money& operator+=(int amount) {
        amount_ += amount;
        return *this;
    }

    Money& operator-=(int amount) {
        amount_ -= amount;
        return *this;
    }

    friend bool operator>=(const Money& money, int amount) {
        return money.amount_ >= amount;
    }

    friend Money operator+(Money money, int amount) {
        money += amount;
        return money;
    }

    friend Money operator-(Money money, int amount) {
        money -= amount;
        return money;
    }

    friend std::ostream& operator<<(std::ostream& os, const Money& money) {
        os << "$" << money.amount_;
        return os;
    }

private:
    int amount_{0};
};

template <typename Key>
class Inventory {
public:
    void add(const Key& key, int amount = 1) {
        if (amount <= 0) return;
        items_[key] += amount;
    }

    bool remove(const Key& key, int amount = 1) {
        auto it = items_.find(key);
        if (it == items_.end() || it->second < amount) return false;
        it->second -= amount;
        if (it->second <= 0) items_.erase(it);
        return true;
    }

    int count(const Key& key) const {
        auto it = items_.find(key);
        return it == items_.end() ? 0 : it->second;
    }

    template <typename Fn>
    void forEach(Fn fn) const {
        for (const auto& [key, amount] : items_) {
            fn(key, amount);
        }
    }

private:
    std::unordered_map<Key, int> items_;
};

class EventLog {
public:
    void push(std::string line) {
        for (char& ch : line) {
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        }
        constexpr std::size_t kMaxEventChars = 58;
        if (line.size() > kMaxEventChars) {
            line = line.substr(0, kMaxEventChars - 3) + "...";
        }
        lines_.push_front(std::move(line));
        while (lines_.size() > 7) lines_.pop_back();
    }

    const std::deque<std::string>& lines() const { return lines_; }

private:
    std::deque<std::string> lines_;
};

class Player {
public:
    Player() {
        inventory_.add("TURNIP SEED", 6);
        inventory_.add("FEED", 3);
    }

    Money& money() { return money_; }
    const Money& money() const { return money_; }
    Inventory<std::string>& inventory() { return inventory_; }
    const Inventory<std::string>& inventory() const { return inventory_; }

    int level() const { return level_; }
    int xp() const { return xp_; }
    int nextLevelXp() const { return level_ * 55; }
    int stamina() const { return stamina_; }
    int maxStamina() const { return maxStamina_; }
    int researchPoints() const { return researchPoints_; }

    void addResearchPoints(int amount) {
        researchPoints_ += std::max(0, amount);
    }

    bool canSpendResearchPoints(int amount) const {
        return amount <= 0 || researchPoints_ >= amount;
    }

    bool spendResearchPoints(int amount) {
        if (!canSpendResearchPoints(amount)) return false;
        researchPoints_ -= amount;
        return true;
    }

    bool canSpendStamina(int amount) const {
        return amount <= 0 || stamina_ >= amount;
    }

    bool spendStamina(int amount) {
        if (!canSpendStamina(amount)) return false;
        stamina_ = std::max(0, stamina_ - amount);
        return true;
    }

    void restoreStamina(int amount) {
        stamina_ = std::min(maxStamina_, stamina_ + std::max(0, amount));
    }

    void restoreStaminaToMax() {
        stamina_ = maxStamina_;
    }

    bool consumeFood(EventLog& log) {
        if (stamina_ >= maxStamina_) {
            log.push("STAMINA ALREADY FULL");
            return false;
        }
        std::array<std::string, 3> foods{"FISH", "POTATO", "TURNIP"};
        for (const auto& food : foods) {
            int restore = edibleStaminaValue(food);
            if (restore <= 0 || inventory_.count(food) <= 0) continue;
            inventory_.remove(food, 1);
            restoreStamina(restore);
            std::ostringstream msg;
            msg << "ATE " << food << " +" << restore << " STAMINA";
            log.push(msg.str());
            return true;
        }
        log.push("NO EDIBLE FOOD");
        return false;
    }

    void addXp(int amount, EventLog& log) {
        xp_ += amount;
        while (xp_ >= nextLevelXp()) {
            xp_ -= nextLevelXp();
            ++level_;
            std::ostringstream msg;
            msg << "LEVEL UP: FARMER LEVEL " << level_;
            log.push(msg.str());
            if (level_ == 2) log.push("POTATO SEEDS UNLOCKED");
            if (level_ == 3) log.push("FISHING ROD UNLOCKED");
            if (level_ == 4) log.push("BARN CONSTRUCTION UNLOCKED");
            if (level_ == 5) log.push("COWS AND MILK UNLOCKED");
        }
    }

    bool potatoUnlocked() const { return level_ >= 2; }
    bool fishingUnlocked() const { return level_ >= 3; }
    bool barnUnlocked() const { return level_ >= 4; }
    bool cowUnlocked() const { return level_ >= 5; }

private:
    Money money_{90};
    int level_{1};
    int xp_{0};
    int stamina_{100};
    int maxStamina_{100};
    int researchPoints_{0};
    Inventory<std::string> inventory_;
};

struct GameContext {
    Player& player;
    EventLog& log;
    std::mt19937& rng;
    Weather weather;
    float dt;
};

class Entity {
public:
    virtual ~Entity() = default;
    virtual void update(GameContext& ctx) = 0;
    virtual void render(SDL_Renderer* renderer, const SDL_Rect& tile) const = 0;
    virtual InteractionResult interact(Action action, GameContext& ctx) = 0;
    virtual void stormHit(GameContext& ctx) = 0;
    virtual std::string name() const = 0;
    virtual std::string status() const = 0;
    virtual bool blocksMovement() const { return true; }
    virtual bool opensMarketplace() const { return false; }
    virtual bool opensCampusShuttle() const { return false; }
};

class Crop final : public Entity {
public:
    explicit Crop(CropType type) : type_(type), produce_(cropName(type)) {
        maxStage_ = type_ == CropType::Potato ? 5 : 4;
    }

    Crop& operator++() {
        stage_ = std::min(stage_ + 1, maxStage_);
        watered_ = false;
        growTimer_ = 0.0f;
        return *this;
    }

    void update(GameContext& ctx) override {
        const bool naturallyWet = ctx.weather == Weather::Rain;
        if (naturallyWet) watered_ = true;
        const bool canGrow = watered_ || naturallyWet;
        if (!canGrow || stage_ >= maxStage_) return;

        float rate = 1.0f;
        if (ctx.weather == Weather::Rain) rate = 2.0f;
        if (ctx.weather == Weather::Storm) rate = 0.4f;
        growTimer_ += ctx.dt * rate;
        if (growTimer_ >= 4.5f) ++(*this);
    }

    void render(SDL_Renderer* renderer, const SDL_Rect& tile) const override {
        SDL_Rect soil{tile.x + 8, tile.y + 18, tile.w - 16, 25};
        fillRect(renderer, soil, watered_ ? color(104, 87, 65) : color(130, 86, 47));
        drawLine(renderer, soil.x + 4, soil.y + 7, soil.x + soil.w - 5, soil.y + 3, color(91, 58, 39));
        drawLine(renderer, soil.x + 3, soil.y + 17, soil.x + soil.w - 6, soil.y + 12, color(91, 58, 39));
        if (watered_) {
            fillRect(renderer, SDL_Rect{soil.x + 7, soil.y + 3, 18, 3}, color(116, 154, 178, 150));
            fillRect(renderer, SDL_Rect{soil.x + 24, soil.y + 15, 15, 3}, color(116, 154, 178, 150));
        }

        if (stage_ == 0) {
            SDL_Rect seed{tile.x + tile.w / 2 - 4, tile.y + 30, 8, 7};
            fillRect(renderer, seed, color(226, 197, 98));
            drawRect(renderer, seed, color(122, 95, 49));
            return;
        }

        SDL_Color leaf = stormScarred_ ? color(118, 130, 77) : color(54, 150, 83);
        int stemHeight = 7 + stage_ * 6;
        SDL_Rect stem{tile.x + tile.w / 2 - 3, tile.y + 41 - stemHeight, 6, stemHeight};
        fillRect(renderer, stem, color(65, 123, 67));
        SDL_Rect leafA{stem.x - 10, stem.y + 9, 14, 8};
        SDL_Rect leafB{stem.x + 3, stem.y + 15, 15, 8};
        fillRect(renderer, leafA, leaf);
        fillRect(renderer, leafB, leaf);
        if (stage_ >= 3) {
            SDL_Rect leafC{stem.x - 13, stem.y + 21, 15, 8};
            fillRect(renderer, leafC, shade(leaf, 12));
        }

        if (stage_ >= maxStage_) {
            SDL_Color cropColor = type_ == CropType::Potato ? color(191, 134, 82) : color(239, 188, 84);
            SDL_Rect bulb{tile.x + tile.w / 2 - 11, stem.y - 8, 22, 16};
            fillRect(renderer, bulb, cropColor);
            drawRect(renderer, bulb, color(112, 81, 39));
            fillRect(renderer, SDL_Rect{bulb.x + 5, bulb.y + 3, 5, 4}, shade(cropColor, 32));
        }
    }

    InteractionResult interact(Action action, GameContext& ctx) override {
        if (action == Action::WateringCan) {
            if (!watered_) {
                watered_ = true;
                ctx.log.push("CROP WATERED");
            } else {
                ctx.log.push("THIS CROP IS ALREADY WATERED");
            }
            return InteractionResult::Keep;
        }

        if (action == Action::Hand) {
            if (stage_ >= maxStage_) {
                ctx.player.inventory().add(produce_, stormScarred_ ? 1 : 2);
                ctx.player.addXp(stormScarred_ ? 8 : 12, ctx.log);
                ctx.log.push("HARVESTED " + produce_);
                return InteractionResult::Remove;
            }
            ctx.log.push("CROP IS STILL GROWING");
            return InteractionResult::Keep;
        }

        return InteractionResult::None;
    }

    void stormHit(GameContext& ctx) override {
        if (stage_ > 0) --stage_;
        watered_ = false;
        stormScarred_ = true;
        ctx.log.push("STORM DAMAGED A CROP");
    }

    std::string name() const override { return produce_; }

    std::string status() const override {
        std::ostringstream out;
        out << produce_ << " STAGE " << stage_ << "/" << maxStage_;
        if (watered_) out << " WET";
        if (stormScarred_) out << " SCARRED";
        return out.str();
    }

    bool blocksMovement() const override { return false; }

private:
    CropType type_;
    std::string produce_;
    int stage_{0};
    int maxStage_{4};
    bool watered_{false};
    bool stormScarred_{false};
    float growTimer_{0.0f};
};

class Animal final : public Entity {
public:
    explicit Animal(AnimalKind kind) : kind_(kind) {}

    void update(GameContext& ctx) override {
        hunger_ = std::min(100.0f, hunger_ + ctx.dt * 0.8f);
        if (ready_) return;

        float rate = hunger_ > 70.0f ? 0.35f : 1.0f;
        if (ctx.weather == Weather::Storm) rate *= 0.4f;
        productionTimer_ += ctx.dt * rate;
        if (productionTimer_ >= productionSeconds()) {
            productionTimer_ = productionSeconds();
            ready_ = true;
        }
    }

    void render(SDL_Renderer* renderer, const SDL_Rect& tile) const override {
        SDL_Rect body{tile.x + 13, tile.y + 18, tile.w - 26, tile.h - 28};
        SDL_Color bodyColor = kind_ == AnimalKind::Chicken ? color(243, 239, 218) : color(141, 96, 62);
        fillRect(renderer, body, bodyColor);
        drawRect(renderer, body, color(58, 52, 46));

        SDL_Rect head{tile.x + 31, tile.y + 10, 16, 16};
        fillRect(renderer, head, bodyColor);
        drawRect(renderer, head, color(58, 52, 46));

        if (kind_ == AnimalKind::Chicken) {
            SDL_Rect comb{head.x + 4, head.y - 5, 8, 6};
            fillRect(renderer, comb, color(207, 77, 64));
            SDL_Rect beak{head.x + 13, head.y + 6, 9, 5};
            fillRect(renderer, beak, color(230, 165, 65));
        } else {
            SDL_Rect patch{body.x + 7, body.y + 5, 13, 12};
            fillRect(renderer, patch, color(238, 230, 204));
            SDL_Rect hornA{head.x - 4, head.y - 3, 6, 6};
            SDL_Rect hornB{head.x + 14, head.y - 3, 6, 6};
            fillRect(renderer, hornA, color(226, 207, 150));
            fillRect(renderer, hornB, color(226, 207, 150));
        }

        if (ready_) {
            SDL_Rect marker{tile.x + tile.w - 15, tile.y + 7, 9, 9};
            fillRect(renderer, marker, color(255, 216, 93));
        }
    }

    InteractionResult interact(Action action, GameContext& ctx) override {
        if (action != Action::Hand) return InteractionResult::None;

        if (ready_) {
            ctx.player.inventory().add(product(), 1);
            productionTimer_ = 0.0f;
            ready_ = false;
            hunger_ = std::min(100.0f, hunger_ + 14.0f);
            ctx.player.addXp(kind_ == AnimalKind::Chicken ? 9 : 15, ctx.log);
            ctx.log.push("COLLECTED " + product());
            return InteractionResult::Keep;
        }

        if (ctx.player.inventory().remove("FEED", 1)) {
            hunger_ = std::max(0.0f, hunger_ - 48.0f);
            ctx.log.push(animalName(kind_) + " FED");
            return InteractionResult::Keep;
        }

        ctx.log.push("NO FEED IN INVENTORY");
        return InteractionResult::Keep;
    }

    void stormHit(GameContext& ctx) override {
        hunger_ = std::min(100.0f, hunger_ + 35.0f);
        productionTimer_ = std::max(0.0f, productionTimer_ - 7.0f);
        ctx.log.push("ANIMAL WAS SPOOKED BY STORM");
    }

    std::string name() const override { return animalName(kind_); }

    std::string status() const override {
        std::ostringstream out;
        out << animalName(kind_) << " ";
        if (ready_) {
            out << product() << " READY";
        } else {
            out << static_cast<int>((productionTimer_ / productionSeconds()) * 100.0f) << "%";
        }
        if (hunger_ > 70.0f) out << " HUNGRY";
        return out.str();
    }

    bool blocksMovement() const override { return false; }

private:
    float productionSeconds() const { return kind_ == AnimalKind::Chicken ? 16.0f : 25.0f; }
    std::string product() const { return kind_ == AnimalKind::Chicken ? "EGG" : "MILK"; }

    AnimalKind kind_;
    float hunger_{15.0f};
    float productionTimer_{0.0f};
    bool ready_{false};
};

class Building final : public Entity {
public:
    explicit Building(std::string kind) : kind_(std::move(kind)) {}

    void update(GameContext&) override {}

    void render(SDL_Renderer* renderer, const SDL_Rect& tile) const override {
        SDL_Rect base{tile.x + 8, tile.y + 21, tile.w - 16, tile.h - 17};
        SDL_Rect roof{tile.x + 13, tile.y + 9, tile.w - 26, 18};
        fillRect(renderer, base, damaged_ ? color(135, 104, 95) : color(181, 74, 63));
        fillRect(renderer, roof, color(103, 67, 57));
        drawRect(renderer, base, color(70, 50, 46));
        drawLine(renderer, roof.x, roof.y + roof.h, roof.x + roof.w / 2, roof.y, color(70, 50, 46));
        drawLine(renderer, roof.x + roof.w, roof.y + roof.h, roof.x + roof.w / 2, roof.y, color(70, 50, 46));
        SDL_Rect door{tile.x + tile.w / 2 - 8, tile.y + 35, 16, 22};
        fillRect(renderer, door, color(84, 55, 48));
        if (damaged_) {
            drawLine(renderer, base.x + 5, base.y + 5, base.x + 18, base.y + 22, color(236, 207, 115));
            drawLine(renderer, base.x + 18, base.y + 22, base.x + 12, base.y + 31, color(236, 207, 115));
        }
    }

    InteractionResult interact(Action action, GameContext& ctx) override {
        if (action != Action::Build && action != Action::Hand) return InteractionResult::None;
        if (!damaged_) {
            ctx.log.push(kind_ + " IS IN GOOD SHAPE");
            return InteractionResult::Keep;
        }
        int cost = ctx.player.level() >= 3 ? 8 : 14;
        if (action == Action::Hand) {
            ctx.log.push("REPAIR COST: $" + std::to_string(cost) + ". USE BUILD TOOL.");
            return InteractionResult::Keep;
        }
        if (!ctx.player.money().spend(cost)) {
            ctx.log.push("NEED $" + std::to_string(cost) + " TO REPAIR " + kind_);
            return InteractionResult::Keep;
        }
        damaged_ = false;
        ctx.player.addXp(6, ctx.log);
        ctx.log.push(kind_ + " REPAIRED");
        return InteractionResult::Keep;
    }

    void stormHit(GameContext& ctx) override {
        damaged_ = true;
        ctx.log.push("STORM DAMAGED A " + kind_);
    }

    std::string name() const override { return kind_; }

    std::string status() const override {
        return damaged_ ? kind_ + " DAMAGED" : kind_ + " READY";
    }

private:
    std::string kind_;
    bool damaged_{false};
};

class Decoration final : public Entity {
public:
    Decoration(DecorKind kind, bool removable = true) : kind_(kind), removable_(removable) {}

    void update(GameContext&) override {}

    void render(SDL_Renderer* renderer, const SDL_Rect& tile) const override {
        switch (kind_) {
            case DecorKind::Tree: {
                fillRect(renderer, SDL_Rect{tile.x + 23, tile.y + 26, 10, 28}, color(105, 70, 43));
                fillRect(renderer, SDL_Rect{tile.x + 10, tile.y + 6, 36, 24}, color(49, 122, 66));
                fillRect(renderer, SDL_Rect{tile.x + 5, tile.y + 20, 44, 20}, color(43, 107, 58));
                fillRect(renderer, SDL_Rect{tile.x + 18, tile.y - 1, 23, 18}, color(60, 143, 75));
                break;
            }
            case DecorKind::Rock: {
                SDL_Rect rock{tile.x + 13, tile.y + 23, 32, 23};
                fillRect(renderer, rock, color(126, 129, 126));
                fillRect(renderer, SDL_Rect{rock.x + 6, rock.y - 5, 22, 10}, color(152, 154, 149));
                drawRect(renderer, rock, color(83, 88, 86));
                break;
            }
            case DecorKind::Flower: {
                fillRect(renderer, SDL_Rect{tile.x + 27, tile.y + 24, 3, 18}, color(52, 133, 67));
                fillRect(renderer, SDL_Rect{tile.x + 18, tile.y + 17, 8, 8}, color(231, 119, 154));
                fillRect(renderer, SDL_Rect{tile.x + 30, tile.y + 15, 9, 9}, color(246, 207, 87));
                fillRect(renderer, SDL_Rect{tile.x + 24, tile.y + 11, 8, 8}, color(238, 132, 83));
                break;
            }
            case DecorKind::Fence: {
                SDL_Color wood = damaged_ ? color(125, 93, 70) : color(152, 102, 61);
                fillRect(renderer, SDL_Rect{tile.x + 9, tile.y + 27, 38, 7}, wood);
                fillRect(renderer, SDL_Rect{tile.x + 9, tile.y + 41, 38, 7}, shade(wood, -10));
                fillRect(renderer, SDL_Rect{tile.x + 13, tile.y + 19, 7, 34}, shade(wood, 12));
                fillRect(renderer, SDL_Rect{tile.x + 37, tile.y + 19, 7, 34}, shade(wood, 12));
                break;
            }
            case DecorKind::Scarecrow: {
                fillRect(renderer, SDL_Rect{tile.x + 27, tile.y + 11, 5, 38}, color(106, 72, 46));
                fillRect(renderer, SDL_Rect{tile.x + 13, tile.y + 23, 33, 5}, color(106, 72, 46));
                fillRect(renderer, SDL_Rect{tile.x + 21, tile.y + 17, 18, 18}, damaged_ ? color(130, 100, 76) : color(221, 159, 78));
                fillRect(renderer, SDL_Rect{tile.x + 18, tile.y + 9, 24, 7}, color(125, 86, 48));
                fillRect(renderer, SDL_Rect{tile.x + 24, tile.y + 3, 13, 8}, color(152, 102, 56));
                break;
            }
            case DecorKind::House:
            case DecorKind::Market: {
                SDL_Color wall = kind_ == DecorKind::House ? color(201, 150, 104) : color(185, 127, 149);
                SDL_Color roof = kind_ == DecorKind::House ? color(121, 60, 48) : color(80, 90, 135);
                fillRect(renderer, SDL_Rect{tile.x + 2, tile.y + 15, 52, 40}, wall);
                fillRect(renderer, SDL_Rect{tile.x - 2, tile.y + 4, 60, 16}, roof);
                drawRect(renderer, SDL_Rect{tile.x + 2, tile.y + 15, 52, 40}, color(77, 57, 50));
                fillRect(renderer, SDL_Rect{tile.x + 9, tile.y + 29, 12, 12}, color(118, 164, 184));
                fillRect(renderer, SDL_Rect{tile.x + 34, tile.y + 33, 13, 22}, color(91, 58, 44));
                if (kind_ == DecorKind::Market) {
                    fillRect(renderer, SDL_Rect{tile.x + 4, tile.y + 16, 49, 7}, color(238, 219, 151));
                }
                break;
            }
            case DecorKind::MarketKeeper: {
                fillRect(renderer, SDL_Rect{tile.x + 17, tile.y + 22, 22, 28}, color(67, 106, 154));
                fillRect(renderer, SDL_Rect{tile.x + 20, tile.y + 6, 16, 16}, color(218, 154, 105));
                fillRect(renderer, SDL_Rect{tile.x + 17, tile.y + 2, 22, 6}, color(82, 54, 38));
                fillRect(renderer, SDL_Rect{tile.x + 22, tile.y + 50, 5, 7}, color(54, 45, 40));
                fillRect(renderer, SDL_Rect{tile.x + 31, tile.y + 50, 5, 7}, color(54, 45, 40));
                fillRect(renderer, SDL_Rect{tile.x + 13, tile.y + 31, 8, 6}, color(218, 154, 105));
                fillRect(renderer, SDL_Rect{tile.x + 36, tile.y + 31, 8, 6}, color(218, 154, 105));
                break;
            }
            case DecorKind::Bed: {
                fillRect(renderer, SDL_Rect{tile.x + 8, tile.y + 19, 40, 31}, color(129, 83, 57));
                fillRect(renderer, SDL_Rect{tile.x + 11, tile.y + 18, 34, 12}, color(238, 224, 194));
                fillRect(renderer, SDL_Rect{tile.x + 11, tile.y + 30, 34, 17}, color(126, 151, 187));
                drawRect(renderer, SDL_Rect{tile.x + 8, tile.y + 19, 40, 31}, color(76, 55, 44));
                break;
            }
            case DecorKind::Table: {
                fillRect(renderer, SDL_Rect{tile.x + 11, tile.y + 23, 35, 19}, color(149, 92, 53));
                fillRect(renderer, SDL_Rect{tile.x + 15, tile.y + 42, 5, 12}, color(101, 66, 43));
                fillRect(renderer, SDL_Rect{tile.x + 38, tile.y + 42, 5, 12}, color(101, 66, 43));
                drawRect(renderer, SDL_Rect{tile.x + 11, tile.y + 23, 35, 19}, color(76, 55, 44));
                break;
            }
            case DecorKind::SnuBusStop: {
                fillRect(renderer, SDL_Rect{tile.x + 26, tile.y + 15, 5, 39}, color(72, 83, 78));
                fillRect(renderer, SDL_Rect{tile.x + 12, tile.y + 5, 33, 22}, color(51, 106, 163));
                drawRect(renderer, SDL_Rect{tile.x + 12, tile.y + 5, 33, 22}, color(232, 238, 224));
                fillRect(renderer, SDL_Rect{tile.x + 17, tile.y + 10, 23, 4}, color(232, 238, 224));
                fillRect(renderer, SDL_Rect{tile.x + 17, tile.y + 17, 16, 4}, color(114, 188, 116));
                fillRect(renderer, SDL_Rect{tile.x + 18, tile.y + 34, 22, 12}, color(77, 151, 186));
                fillRect(renderer, SDL_Rect{tile.x + 20, tile.y + 38, 5, 4}, color(232, 238, 224));
                fillRect(renderer, SDL_Rect{tile.x + 32, tile.y + 38, 5, 4}, color(232, 238, 224));
                fillRect(renderer, SDL_Rect{tile.x + 20, tile.y + 47, 5, 5}, color(42, 48, 48));
                fillRect(renderer, SDL_Rect{tile.x + 32, tile.y + 47, 5, 5}, color(42, 48, 48));
                break;
            }
        }
        if (damaged_) {
            drawLine(renderer, tile.x + 10, tile.y + 11, tile.x + 44, tile.y + 47, color(238, 206, 86));
        }
    }

    InteractionResult interact(Action action, GameContext& ctx) override {
        if (action == Action::AxePick && removable_) {
            ctx.player.addXp(kind_ == DecorKind::Tree ? 5 : 2, ctx.log);
            ctx.log.push(name() + " CLEARED");
            return InteractionResult::Remove;
        }
        if ((action == Action::Build || action == Action::Hand) && damaged_) {
            int cost = kind_ == DecorKind::Fence ? 4 : 8;
            if (action == Action::Hand) {
                ctx.log.push("REPAIR COST: $" + std::to_string(cost) + ". USE BUILD TOOL.");
                return InteractionResult::Keep;
            }
            if (!ctx.player.money().spend(cost)) {
                ctx.log.push("NEED $" + std::to_string(cost) + " TO REPAIR " + name());
                return InteractionResult::Keep;
            }
            damaged_ = false;
            ctx.player.addXp(3, ctx.log);
            ctx.log.push(name() + " REPAIRED");
            return InteractionResult::Keep;
        }
        if (action == Action::Hand) {
            if (kind_ == DecorKind::Market) ctx.log.push("TALK TO THE MARKET KEEPER");
            else if (kind_ == DecorKind::MarketKeeper) ctx.log.push("MARKET KEEPER: TRY TODAY'S SEEDS");
            else if (kind_ == DecorKind::House) ctx.log.push("HOME SWEET HOME");
            else if (kind_ == DecorKind::Bed) {
                ctx.player.restoreStaminaToMax();
                ctx.log.push("SLEPT WELL: STAMINA FULL");
            }
            else if (kind_ == DecorKind::Table) ctx.log.push("A STURDY TABLE");
            else if (kind_ == DecorKind::Scarecrow) ctx.log.push("FENCES MARK PENS; SCARECROWS PROTECT CROPS");
            else if (kind_ == DecorKind::SnuBusStop) ctx.log.push("SNU SHUTTLE STOP");
            else ctx.log.push(name());
            return InteractionResult::Keep;
        }
        return InteractionResult::None;
    }

    void stormHit(GameContext& ctx) override {
        if (kind_ == DecorKind::Fence || kind_ == DecorKind::Scarecrow || kind_ == DecorKind::House || kind_ == DecorKind::Market) {
            damaged_ = true;
            ctx.log.push("STORM DAMAGED " + name());
        }
    }

    std::string name() const override {
        switch (kind_) {
            case DecorKind::Tree: return "TREE";
            case DecorKind::Rock: return "ROCK";
            case DecorKind::Flower: return "FLOWERS";
            case DecorKind::Fence: return "FENCE";
            case DecorKind::Scarecrow: return "SCARECROW";
            case DecorKind::House: return "HOUSE";
            case DecorKind::Market: return "MARKET";
            case DecorKind::MarketKeeper: return "MARKET KEEPER";
            case DecorKind::Bed: return "BED";
            case DecorKind::Table: return "TABLE";
            case DecorKind::SnuBusStop: return "SNU SHUTTLE STOP";
        }
        return "DECOR";
    }

    std::string status() const override {
        return damaged_ ? name() + " DAMAGED" : name();
    }

    bool blocksMovement() const override {
        switch (kind_) {
            case DecorKind::Flower:
                return false;
            case DecorKind::Tree:
            case DecorKind::Rock:
            case DecorKind::Fence:
            case DecorKind::Scarecrow:
            case DecorKind::House:
            case DecorKind::Market:
            case DecorKind::MarketKeeper:
            case DecorKind::Bed:
            case DecorKind::Table:
            case DecorKind::SnuBusStop:
                return true;
        }
        return true;
    }

    bool opensCampusShuttle() const override {
        return kind_ == DecorKind::SnuBusStop;
    }

private:
    DecorKind kind_;
    bool removable_{true};
    bool damaged_{false};
};

class NPC final : public Entity {
public:
    explicit NPC(NpcKind kind) : kind_(kind) {}

    void update(GameContext&) override {}

    void render(SDL_Renderer* renderer, const SDL_Rect& tile) const override {
        SDL_Rect shadow{tile.x + 15, tile.y + 48, 28, 7};
        fillRect(renderer, shadow, color(64, 70, 61, 120));

        if (kind_ == NpcKind::MarketKeeper) {
            SDL_Rect body{tile.x + 17, tile.y + 22, 22, 28};
            fillRect(renderer, body, color(67, 106, 154));
            drawRect(renderer, body, color(45, 65, 93));
            fillRect(renderer, SDL_Rect{tile.x + 20, tile.y + 6, 16, 16}, color(218, 154, 105));
            drawRect(renderer, SDL_Rect{tile.x + 20, tile.y + 6, 16, 16}, color(104, 71, 54));
            fillRect(renderer, SDL_Rect{tile.x + 17, tile.y + 2, 22, 6}, color(82, 54, 38));
            fillRect(renderer, SDL_Rect{tile.x + 22, tile.y + 50, 5, 7}, color(54, 45, 40));
            fillRect(renderer, SDL_Rect{tile.x + 31, tile.y + 50, 5, 7}, color(54, 45, 40));
            fillRect(renderer, SDL_Rect{tile.x + 13, tile.y + 31, 8, 6}, color(218, 154, 105));
            fillRect(renderer, SDL_Rect{tile.x + 36, tile.y + 31, 8, 6}, color(218, 154, 105));
            fillRect(renderer, SDL_Rect{tile.x + 19, tile.y + 18, 18, 5}, color(238, 219, 151));
        }
    }

    InteractionResult interact(Action action, GameContext& ctx) override {
        if (action != Action::Hand) return InteractionResult::None;
        ctx.log.push("TALKED TO MARKET KEEPER");
        return InteractionResult::Keep;
    }

    void stormHit(GameContext&) override {}

    std::string name() const override {
        return kind_ == NpcKind::MarketKeeper ? "MARKET KEEPER" : "NPC";
    }

    std::string status() const override {
        return name();
    }

    bool blocksMovement() const override { return true; }

    bool opensMarketplace() const override {
        return kind_ == NpcKind::MarketKeeper;
    }

private:
    NpcKind kind_;
};

struct Tile {
    TileKind kind{TileKind::Grass};
    std::unique_ptr<Entity> entity;
};

class World {
public:
    explicit World(std::optional<unsigned int> mapSeed = std::nullopt) : tiles_(kGridWidth * kGridHeight) {
        unsigned int seed = mapSeed.value_or(std::random_device{}());
        std::mt19937 mapRng(seed);

        for (int x = 1; x < kGridWidth - 1; ++x) setKind(x, 3, TileKind::Path);
        for (int y = 1; y < kGridHeight - 1; ++y) setKind(4, y, TileKind::Path);
        for (int x = 4; x <= 78; ++x) setKind(x, 15, TileKind::Path);
        for (int y = 12; y <= 74; ++y) setKind(22, y, TileKind::Path);

        for (int x = 1; x <= 3; ++x) setKind(x, 1, TileKind::BuildingArea);
        for (int x = 6; x <= 8; ++x) setKind(x, 2, TileKind::BuildingArea);
        for (int x = 1; x <= 3; ++x) setKind(x, 2, TileKind::IndoorFloor);
        addDecor(2, 1, DecorKind::House, false);
        addDecor(2, 2, DecorKind::Bed, false);
        addDecor(7, 2, DecorKind::Market, false);
        addNpc(8, 4, NpcKind::MarketKeeper);
        addDecor(6, 4, DecorKind::Table, false);

        for (int y = 6; y <= 10; ++y) {
            for (int x = 6; x <= 12; ++x) setKind(x, y, TileKind::Soil);
        }

        generateWaterBodies(14, mapRng);
        clearProtectedWaterAreas();

        addDecor(kFarmShuttleStopX, kFarmShuttleStopY, DecorKind::SnuBusStop, false);
        addDecor(14, 5, DecorKind::Tree);
        addDecor(18, 4, DecorKind::Tree);
        addDecor(24, 6, DecorKind::Tree);
        addDecor(2, 13, DecorKind::Tree);
        addDecor(15, 17, DecorKind::Tree);
        addDecor(33, 28, DecorKind::Tree);
        addDecor(48, 46, DecorKind::Tree);
        addDecor(71, 31, DecorKind::Tree);
        addDecor(19, 19, DecorKind::Rock);
        addDecor(12, 14, DecorKind::Rock);
        addDecor(31, 35, DecorKind::Rock);
        addDecor(58, 20, DecorKind::Rock);
        addDecor(6, 14, DecorKind::Flower);
        addDecor(9, 13, DecorKind::Flower);
        addDecor(36, 16, DecorKind::Flower);
        addDecor(68, 55, DecorKind::Flower);
        addDecor(13, 8, DecorKind::Scarecrow, false);
        for (int x = 5; x <= 13; ++x) {
            if (x != 9) addDecor(x, 11, DecorKind::Fence);
        }
        for (int x = 5; x <= 13; ++x) {
            if (x != 9) addDecor(x, 17, DecorKind::Fence);
        }
        for (int y = 12; y <= 16; ++y) {
            if (y != 15) {
                addDecor(5, y, DecorKind::Fence);
                addDecor(13, y, DecorKind::Fence);
            }
        }

        for (int y = 0; y < kGridHeight; ++y) {
            for (int x = 0; x < kGridWidth; ++x) {
                if (x > 0 && y > 0 && x < kGridWidth - 1 && y < kGridHeight - 1) continue;
                if ((x + y) % 3 == 0) addDecor(x, y, DecorKind::Tree);
            }
        }

        for (int y = 5; y < kGridHeight - 2; y += 4) {
            for (int x = 16; x < kGridWidth - 3; x += 5) {
                if (at(x, y).kind == TileKind::Grass && !at(x, y).entity) {
                    addDecor(x, y, (x + y) % 2 == 0 ? DecorKind::Rock : DecorKind::Flower);
                }
            }
        }
    }

    bool inBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < kGridWidth && y < kGridHeight;
    }

    Tile& at(int x, int y) { return tiles_[index(x, y)]; }
    const Tile& at(int x, int y) const { return tiles_[index(x, y)]; }

    bool isPond(int x, int y) const {
        return inBounds(x, y) && at(x, y).kind == TileKind::Pond;
    }

    int countPondTiles() const {
        int count = 0;
        for (const auto& tile : tiles_) {
            if (tile.kind == TileKind::Pond) ++count;
        }
        return count;
    }

    int countWaterBodies() const {
        std::vector<bool> visited(tiles_.size(), false);
        int components = 0;
        std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (int y = 0; y < kGridHeight; ++y) {
            for (int x = 0; x < kGridWidth; ++x) {
                int start = index(x, y);
                if (visited[start] || at(x, y).kind != TileKind::Pond) continue;
                ++components;
                std::vector<std::pair<int, int>> stack{{x, y}};
                visited[start] = true;
                while (!stack.empty()) {
                    auto [cx, cy] = stack.back();
                    stack.pop_back();
                    for (const auto& [dx, dy] : dirs) {
                        int nx = cx + dx;
                        int ny = cy + dy;
                        if (!inBounds(nx, ny)) continue;
                        int next = index(nx, ny);
                        if (visited[next] || at(nx, ny).kind != TileKind::Pond) continue;
                        visited[next] = true;
                        stack.push_back({nx, ny});
                    }
                }
            }
        }
        return components;
    }

    bool isWalkable(int x, int y) const {
        if (!inBounds(x, y)) return false;
        const Tile& tile = at(x, y);
        if (!baseTileWalkable(tile.kind)) return false;
        if (tile.entity && tile.entity->blocksMovement()) return false;
        return true;
    }

    bool isWalkableAt(float tileX, float tileY, float radiusX = 0.23f, float radiusY = 0.22f) const {
        int minX = static_cast<int>(std::floor(tileX - radiusX + 0.5f));
        int maxX = static_cast<int>(std::floor(tileX + radiusX + 0.5f));
        int minY = static_cast<int>(std::floor(tileY - radiusY + 0.5f));
        int maxY = static_cast<int>(std::floor(tileY + radiusY + 0.5f));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (!isWalkable(x, y)) return false;
            }
        }
        return true;
    }

    bool canPlaceAnimalAt(int x, int y) const {
        if (!inBounds(x, y)) return false;
        const Tile& tile = at(x, y);
        return tile.kind == TileKind::Grass && !tile.entity;
    }

    void update(GameContext& ctx) {
        for (auto& tile : tiles_) {
            if (tile.entity) tile.entity->update(ctx);
        }
    }

    bool hoe(int x, int y, GameContext& ctx) {
        Tile& tile = at(x, y);
        if (tile.entity) {
            ctx.log.push("CLEAR THE TILE FIRST");
            return false;
        }
        if (tile.kind == TileKind::Soil) {
            ctx.log.push("SOIL IS ALREADY TILLED");
            return false;
        }
        if (tile.kind != TileKind::Grass) {
            ctx.log.push("ONLY GRASS CAN BE TILLED");
            return false;
        }
        tile.kind = TileKind::Soil;
        ctx.player.addXp(1, ctx.log);
        ctx.log.push("SOIL TILLED");
        return true;
    }

    bool plant(int x, int y, CropType type, GameContext& ctx) {
        Tile& tile = at(x, y);
        if (tile.kind == TileKind::Pond) {
            ctx.log.push("POND TILES ARE FOR FISHING");
            return false;
        }
        if (tile.kind != TileKind::Soil) {
            ctx.log.push("USE THE HOE BEFORE PLANTING");
            return false;
        }
        if (tile.entity) {
            ctx.log.push("TILE IS ALREADY OCCUPIED");
            return false;
        }
        if (type == CropType::Potato && !ctx.player.potatoUnlocked()) {
            ctx.log.push("POTATO SEEDS UNLOCK AT LEVEL 2");
            return false;
        }
        if (!ctx.player.inventory().remove(seedName(type), 1)) {
            ctx.log.push("BUY " + seedName(type) + " AT THE MARKET");
            return false;
        }
        tile.entity = std::make_unique<Crop>(type);
        ctx.log.push(cropName(type) + " PLANTED");
        return true;
    }

    bool placeAnimal(int x, int y, AnimalKind kind, GameContext& ctx) {
        if (!canPlaceAnimalAt(x, y)) {
            ctx.log.push("ANIMAL NEEDS AN EMPTY FIELD TILE");
            return false;
        }
        if (kind == AnimalKind::Cow && !ctx.player.cowUnlocked()) {
            ctx.log.push("COWS UNLOCK AT LEVEL 5");
            return false;
        }
        int cost = kind == AnimalKind::Chicken ? 30 : 70;
        if (!ctx.player.money().spend(cost)) {
            ctx.log.push("NOT ENOUGH MONEY FOR " + animalName(kind));
            return false;
        }
        Tile& tile = at(x, y);
        tile.entity = std::make_unique<Animal>(kind);
        ctx.player.addXp(5, ctx.log);
        ctx.log.push(animalName(kind) + " ADDED TO FARM");
        return true;
    }

    bool build(int x, int y, bool barn, GameContext& ctx) {
        Tile& tile = at(x, y);
        if (tile.kind == TileKind::Pond || tile.entity) {
            ctx.log.push("BUILDINGS NEED EMPTY FIELD TILES");
            return false;
        }
        if (barn && !ctx.player.barnUnlocked()) {
            ctx.log.push("BARNS UNLOCK AT LEVEL 4");
            return false;
        }
        int cost = barn ? 55 : 10;
        if (!ctx.player.money().spend(cost)) {
            ctx.log.push(barn ? "NOT ENOUGH MONEY FOR BARN" : "NOT ENOUGH MONEY FOR FENCE");
            return false;
        }
        if (barn) {
            tile.kind = TileKind::BuildingArea;
            tile.entity = std::make_unique<Building>("BARN");
            ctx.player.addXp(14, ctx.log);
            ctx.log.push("BARN BUILT");
        } else {
            tile.entity = std::make_unique<Decoration>(DecorKind::Fence);
            ctx.player.addXp(3, ctx.log);
            ctx.log.push("FENCE BUILT");
        }
        return true;
    }

    InteractionResult interact(int x, int y, Action action, GameContext& ctx) {
        Tile& tile = at(x, y);
        if (!tile.entity) {
            ctx.log.push(tileKindName(tile.kind));
            return InteractionResult::None;
        }
        InteractionResult result = tile.entity->interact(action, ctx);
        if (result == InteractionResult::Remove) {
            tile.entity.reset();
            if (tile.kind == TileKind::BuildingArea) tile.kind = TileKind::Grass;
        } else if (result == InteractionResult::None) {
            ctx.log.push("THAT TOOL HAS NO EFFECT");
        }
        return result;
    }

    int stormDamage(GameContext& ctx) {
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        int damaged = 0;
        for (auto& tile : tiles_) {
            if (!tile.entity) continue;
            if (roll(ctx.rng) < 0.28f) {
                tile.entity->stormHit(ctx);
                ++damaged;
            }
        }
        return damaged;
    }

private:
    int index(int x, int y) const { return y * kGridWidth + x; }

    bool protectedFromWater(int x, int y) const {
        if (!inBounds(x, y)) return true;
        if (x <= 15 && y <= 17) return true;
        if (std::abs(x - 6) <= 4 && std::abs(y - 6) <= 4) return true;
        if (std::abs(x - 2) <= 3 && std::abs(y - 3) <= 3) return true;
        if (std::abs(x - 8) <= 4 && std::abs(y - 4) <= 4) return true;
        if (std::abs(x - kFarmShuttleStopX) <= 2 && std::abs(y - kFarmShuttleStopY) <= 2) return true;
        const Tile& tile = at(x, y);
        if (tile.kind == TileKind::Path || tile.kind == TileKind::BuildingArea || tile.kind == TileKind::IndoorFloor || tile.kind == TileKind::Soil) return true;
        return false;
    }

    bool canPlaceWater(int x, int y) const {
        if (!inBounds(x, y)) return false;
        if (x <= 1 || y <= 1 || x >= kGridWidth - 2 || y >= kGridHeight - 2) return false;
        if (protectedFromWater(x, y)) return false;
        return at(x, y).kind == TileKind::Grass && !at(x, y).entity;
    }

    void generateWaterBodies(int count, std::mt19937& rng) {
        std::uniform_int_distribution<int> xRoll(6, kGridWidth - 7);
        std::uniform_int_distribution<int> yRoll(8, kGridHeight - 7);
        std::uniform_int_distribution<int> sizeRoll(18, 54);
        std::uniform_int_distribution<int> dirRoll(0, 3);
        std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

        for (int body = 0; body < count; ++body) {
            int cx = 0;
            int cy = 0;
            bool foundCenter = false;
            for (int attempts = 0; attempts < 160; ++attempts) {
                cx = xRoll(rng);
                cy = yRoll(rng);
                if (canPlaceWater(cx, cy)) {
                    foundCenter = true;
                    break;
                }
            }
            if (!foundCenter) continue;

            int targetSize = sizeRoll(rng);
            std::vector<std::pair<int, int>> bodyTiles{{cx, cy}};
            setPond(cx, cy);

            for (int attempts = 0; attempts < targetSize * 18 && static_cast<int>(bodyTiles.size()) < targetSize; ++attempts) {
                std::uniform_int_distribution<int> tileRoll(0, static_cast<int>(bodyTiles.size()) - 1);
                auto [baseX, baseY] = bodyTiles[tileRoll(rng)];
                auto [dx, dy] = dirs[dirRoll(rng)];
                int nx = baseX + dx;
                int ny = baseY + dy;
                if (!canPlaceWater(nx, ny)) continue;
                setPond(nx, ny);
                bodyTiles.push_back({nx, ny});
            }
        }
    }

    void clearProtectedWaterAreas() {
        for (int y = 0; y < kGridHeight; ++y) {
            for (int x = 0; x < kGridWidth; ++x) {
                if (at(x, y).kind == TileKind::Pond && protectedFromWater(x, y)) {
                    setKind(x, y, TileKind::Grass);
                }
            }
        }
    }

    void setKind(int x, int y, TileKind kind) {
        if (inBounds(x, y)) tiles_[index(x, y)].kind = kind;
    }

    void setPond(int x, int y) {
        setKind(x, y, TileKind::Pond);
    }

    void addDecor(int x, int y, DecorKind kind, bool removable = true) {
        if (!inBounds(x, y)) return;
        Tile& tile = tiles_[index(x, y)];
        if (tile.entity || tile.kind == TileKind::Pond || tile.kind == TileKind::Path) return;
        tiles_[index(x, y)].entity = std::make_unique<Decoration>(kind, removable);
    }

    void addNpc(int x, int y, NpcKind kind) {
        if (!inBounds(x, y)) return;
        Tile& tile = tiles_[index(x, y)];
        if (tile.entity || tile.kind == TileKind::Pond) return;
        tile.entity = std::make_unique<NPC>(kind);
    }

    std::vector<Tile> tiles_;
};

bool runCollisionTests() {
    World world(1337);
    bool ok = true;
    auto expect = [&](bool actual, bool expected, const std::string& label) {
        if (actual != expected) {
            std::cerr << "Collision test failed: " << label << " expected "
                      << (expected ? "walkable" : "blocked") << " but got "
                      << (actual ? "walkable" : "blocked") << "\n";
            ok = false;
        }
    };

    std::optional<std::pair<int, int>> pondTile;
    for (int y = 0; y < kGridHeight && !pondTile; ++y) {
        for (int x = 0; x < kGridWidth && !pondTile; ++x) {
            if (world.isPond(x, y)) pondTile = std::make_pair(x, y);
        }
    }

    expect(pondTile.has_value(), true, "generated pond exists");
    expect(world.countWaterBodies() >= 6, true, "multiple connected water bodies generated");
    expect(world.countPondTiles() > 120, true, "large water coverage generated");
    if (pondTile) {
        expect(world.isWalkable(pondTile->first, pondTile->second), false, "pond water");
        expect(world.isWalkableAt(static_cast<float>(pondTile->first), static_cast<float>(pondTile->second)), false, "continuous movement into pond");
    }
    expect(world.isWalkable(2, 1), false, "house");
    expect(world.isWalkable(5, 11), false, "fence");
    expect(world.isWalkable(14, 5), false, "tree");
    expect(world.isWalkable(8, 4), false, "market keeper NPC");
    expect(world.at(8, 4).entity && world.at(8, 4).entity->opensMarketplace(), true, "market keeper opens marketplace");
    expect(world.isWalkable(kFarmShuttleStopX, kFarmShuttleStopY), false, "SNU shuttle bus stop");
    expect(world.at(kFarmShuttleStopX, kFarmShuttleStopY).entity && world.at(kFarmShuttleStopX, kFarmShuttleStopY).entity->opensCampusShuttle(), true, "SNU shuttle opens campus travel");
    expect(world.isWalkable(static_cast<int>(kFarmShuttleReturnX), static_cast<int>(kFarmShuttleReturnY)), true, "SNU shuttle approach tile");
    expect(world.isWalkable(6, 4), false, "table furniture");
    expect(world.isWalkable(2, 2), false, "bed furniture");
    expect(world.isWalkable(20, 6), true, "grass");
    expect(world.isWalkable(4, 3), true, "path");
    expect(world.isWalkable(6, 6), true, "farm soil");
    expect(world.isWalkable(2, 3), true, "house door path is clear");
    expect(world.isWalkable(8, 3), true, "market approach path is clear");
    expect(world.isWalkableAt(6.0f, 6.0f), true, "spawn area remains walkable");
    expect(baseTileWalkable(TileKind::IndoorFloor), true, "indoor floor base tile");
    expect(world.isWalkableAt(6.0f, 6.0f), true, "continuous movement on soil");

    if (ok) std::cout << "Collision tests passed\n";
    return ok;
}

bool runStaminaTests() {
    EventLog log;
    Player player;
    bool ok = true;
    auto expect = [&](bool condition, const std::string& label) {
        if (!condition) {
            std::cerr << "Stamina test failed: " << label << "\n";
            ok = false;
        }
    };

    expect(player.stamina() == player.maxStamina(), "starts full");
    expect(player.spendStamina(kStaminaPlant), "can spend planting stamina");
    expect(player.stamina() == player.maxStamina() - kStaminaPlant, "planting reduces stamina");
    expect(!player.spendStamina(player.maxStamina() + 1), "cannot overspend stamina");

    while (player.spendStamina(kStaminaWater)) {}
    expect(!player.canSpendStamina(kStaminaWater), "low stamina blocks stamina action");

    player.inventory().add("FISH", 1);
    int fishBefore = player.inventory().count("FISH");
    expect(player.consumeFood(log), "food can be consumed");
    expect(player.inventory().count("FISH") == fishBefore - 1, "food count decreases");
    expect(player.stamina() > 0, "food restores stamina");

    player.spendStamina(15);
    player.restoreStaminaToMax();
    expect(player.stamina() == player.maxStamina(), "sleep restore fills stamina");

    if (ok) std::cout << "Stamina tests passed\n";
    return ok;
}

class EventManager {
public:
    Weather weather() const { return weather_; }

    void update(float dt, GameContext& ctx, World& world) {
        remaining_ -= dt;
        if (remaining_ > 0.0f) return;

        std::uniform_int_distribution<int> weatherRoll(1, 100);
        int roll = weatherRoll(ctx.rng);
        if (roll <= 14) weather_ = Weather::Storm;
        else if (roll <= 38) weather_ = Weather::Rain;
        else if (roll <= 64) weather_ = Weather::Cloudy;
        else weather_ = Weather::Sunny;

        std::uniform_real_distribution<float> durationRoll(12.0f, 24.0f);
        remaining_ = durationRoll(ctx.rng);

        if (weather_ == Weather::Storm) {
            ctx.log.push("RANDOM EVENT: STORM");
            int damaged = world.stormDamage(ctx);
            if (damaged == 0) ctx.log.push("STORM PASSED WITHOUT DAMAGE");
        } else {
            ctx.log.push("WEATHER SHIFTED TO " + weatherName(weather_));
        }
    }

private:
    Weather weather_{Weather::Sunny};
    float remaining_{10.0f};
};

class FishingGame {
public:
    void start(std::mt19937& rng) {
        attempts_ = 3;
        catches_ = 0;
        marker_ = 0.0f;
        direction_ = 1.0f;
        newRound(rng);
    }

    void update(float dt) {
        marker_ += dt * speed_ * direction_;
        if (marker_ > 1.0f) {
            marker_ = 1.0f;
            direction_ = -1.0f;
        } else if (marker_ < 0.0f) {
            marker_ = 0.0f;
            direction_ = 1.0f;
        }
    }

    bool cast(Player& player, EventLog& log, std::mt19937& rng) {
        const bool caught = marker_ >= zoneStart_ && marker_ <= zoneStart_ + zoneWidth_;
        --attempts_;
        if (caught) {
            ++catches_;
            player.inventory().add("FISH", 1);
            player.addXp(10, log);
            log.push("FISH CAUGHT");
        } else {
            log.push("FISH GOT AWAY");
        }
        if (attempts_ <= 0) {
            if (catches_ > 0) player.money().add(catches_ * 4);
            log.push("FISHING ENDED");
            return true;
        }
        newRound(rng);
        return false;
    }

    void render(SDL_Renderer* renderer, const Text& text) const {
        fillRect(renderer, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(31, 88, 107));
        fillRect(renderer, SDL_Rect{80, 92, 880, 420}, color(224, 233, 214));
        drawRect(renderer, SDL_Rect{80, 92, 880, 420}, color(35, 63, 69));
        text.draw(renderer, "POND FISHING", 110, 122, color(35, 63, 69), 2);
        text.draw(renderer, "LAND THE MARKER INSIDE THE GREEN CATCH WATER", 112, 168, color(61, 77, 79), 1);

        SDL_Rect bar{150, 286, 740, 34};
        fillRect(renderer, bar, color(68, 116, 145));
        drawRect(renderer, bar, color(28, 52, 69));

        SDL_Rect zone{bar.x + static_cast<int>(zoneStart_ * bar.w), bar.y - 8, static_cast<int>(zoneWidth_ * bar.w), bar.h + 16};
        fillRect(renderer, zone, color(92, 176, 112));
        drawRect(renderer, zone, color(35, 92, 57));

        int markerX = bar.x + static_cast<int>(marker_ * bar.w);
        SDL_Rect marker{markerX - 5, bar.y - 16, 10, bar.h + 32};
        fillRect(renderer, marker, color(250, 214, 89));

        std::ostringstream status;
        status << "CASTS " << attempts_ << "   CATCHES " << catches_;
        text.draw(renderer, status.str(), 112, 390, color(35, 63, 69), 1);
        text.draw(renderer, "SPACE CASTS  |  ESC RETURNS", 112, 432, color(99, 82, 73));
    }

private:
    void newRound(std::mt19937& rng) {
        std::uniform_real_distribution<float> zoneRoll(0.08f, 0.72f);
        std::uniform_real_distribution<float> speedRoll(0.65f, 1.35f);
        zoneStart_ = zoneRoll(rng);
        zoneWidth_ = 0.16f;
        speed_ = speedRoll(rng);
        marker_ = 0.0f;
        direction_ = 1.0f;
    }

    int attempts_{3};
    int catches_{0};
    float marker_{0.0f};
    float direction_{1.0f};
    float speed_{1.0f};
    float zoneStart_{0.4f};
    float zoneWidth_{0.16f};
};

class MemoryGame {
public:
    void start(std::mt19937& rng) {
        cards_.clear();
        std::vector<char> symbols{'A', 'A', 'B', 'B', 'C', 'C', 'D', 'D'};
        std::shuffle(symbols.begin(), symbols.end(), rng);
        for (char symbol : symbols) cards_.push_back(Card{symbol});
        cursor_ = 0;
        first_.reset();
        second_.reset();
        revealTimer_ = 0.0f;
        moves_ = 0;
        finished_ = false;
    }

    void update(float dt, Player& player, EventLog& log) {
        if (!first_ || !second_) return;
        revealTimer_ -= dt;
        if (revealTimer_ > 0.0f) return;

        Card& a = cards_[*first_];
        Card& b = cards_[*second_];
        if (a.symbol == b.symbol) {
            a.matched = true;
            b.matched = true;
            log.push("MEMORY PAIR MATCHED");
        } else {
            a.revealed = false;
            b.revealed = false;
        }
        first_.reset();
        second_.reset();

        if (std::all_of(cards_.begin(), cards_.end(), [](const Card& card) { return card.matched; })) {
            finished_ = true;
            player.inventory().add("GEM", 1);
            player.money().add(18);
            player.addXp(18, log);
            log.push("MEMORY FAIR WON");
        }
    }

    void move(int dx, int dy) {
        int x = cursor_ % 4;
        int y = cursor_ / 4;
        x = std::clamp(x + dx, 0, 3);
        y = std::clamp(y + dy, 0, 1);
        cursor_ = y * 4 + x;
    }

    void flip(EventLog& log) {
        if (finished_ || first_ && second_) return;
        Card& card = cards_[cursor_];
        if (card.revealed || card.matched) return;
        card.revealed = true;
        if (!first_) {
            first_ = cursor_;
        } else {
            second_ = cursor_;
            revealTimer_ = 0.75f;
            ++moves_;
        }
        log.push("CARD FLIPPED");
    }

    bool finished() const { return finished_; }

    void render(SDL_Renderer* renderer, const Text& text) const {
        fillRect(renderer, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(73, 67, 88));
        fillRect(renderer, SDL_Rect{78, 72, 884, 500}, color(236, 229, 205));
        drawRect(renderer, SDL_Rect{78, 72, 884, 500}, color(66, 55, 61));
        text.draw(renderer, "VILLAGE MEMORY FAIR", 110, 106, color(66, 55, 61), 2);

        for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
            int x = 152 + (i % 4) * 180;
            int y = 204 + (i / 4) * 134;
            SDL_Rect card{x, y, 124, 92};
            SDL_Color face = cards_[i].matched ? color(116, 176, 132) : color(87, 122, 171);
            SDL_Color back = color(169, 94, 103);
            fillRect(renderer, card, cards_[i].revealed || cards_[i].matched ? face : back);
            drawRect(renderer, card, i == cursor_ ? color(250, 216, 93) : color(61, 52, 59));
            drawRect(renderer, SDL_Rect{card.x + 3, card.y + 3, card.w - 6, card.h - 6}, i == cursor_ ? color(250, 216, 93) : color(61, 52, 59));
            if (cards_[i].revealed || cards_[i].matched) {
                std::string symbol(1, cards_[i].symbol);
                text.draw(renderer, symbol, card.x + 49, card.y + 26, color(244, 239, 221), 2);
            }
        }

        std::ostringstream status;
        status << "MOVES " << moves_;
        if (finished_) status << "   REWARD CLAIMED";
        text.draw(renderer, status.str(), 112, 470, color(66, 55, 61), 1);
        text.draw(renderer, "ARROWS MOVE  |  SPACE FLIPS  |  ESC RETURNS", 112, 516, color(99, 82, 73));
    }

private:
    struct Card {
        char symbol{'A'};
        bool revealed{false};
        bool matched{false};
    };

    std::vector<Card> cards_;
    int cursor_{0};
    std::optional<int> first_;
    std::optional<int> second_;
    float revealTimer_{0.0f};
    int moves_{0};
    bool finished_{false};
};

class DebuggingGame {
public:
    void start(int playerLevel, std::mt19937& rng) {
        if (playerLevel >= 7) {
            gridSize_ = 5;
            reward_ = 35;
        } else if (playerLevel >= 4) {
            gridSize_ = 4;
            reward_ = 20;
        } else {
            gridSize_ = 3;
            reward_ = 10;
        }
        penalty_ = 5;
        cursor_ = 0;
        timeLeft_ = 10.0f;
        active_ = true;
        std::uniform_int_distribution<int> dist(0, gridSize_ * gridSize_ - 1);
        bug_ = dist(rng);
    }

    void stop() { active_ = false; }
    bool active() const { return active_; }
    bool expired() const { return active_ && timeLeft_ <= 0.0f; }
    int reward() const { return reward_; }
    int penalty() const { return penalty_; }
    bool correct() const { return cursor_ == bug_; }

    void update(float dt) {
        if (!active_) return;
        timeLeft_ = std::max(0.0f, timeLeft_ - dt);
    }

    void move(int dx, int dy) {
        int x = cursor_ % gridSize_;
        int y = cursor_ / gridSize_;
        x = std::clamp(x + dx, 0, gridSize_ - 1);
        y = std::clamp(y + dy, 0, gridSize_ - 1);
        cursor_ = y * gridSize_ + x;
    }

    void render(SDL_Renderer* renderer, const Text& text) const {
        fillRect(renderer, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(38, 48, 54));
        fillRect(renderer, SDL_Rect{96, 76, 848, 530}, color(221, 226, 216));
        drawRect(renderer, SDL_Rect{96, 76, 848, 530}, color(55, 67, 70));
        text.draw(renderer, "BUG FINDING / CIRCUIT DEBUGGING", 128, 112, color(51, 62, 65), 2);
        text.draw(renderer, "Find the faulty circuit before time runs out.", 130, 156, color(70, 82, 83));

        int cell = gridSize_ == 5 ? 66 : (gridSize_ == 4 ? 78 : 92);
        int gap = 10;
        int gridW = gridSize_ * cell + (gridSize_ - 1) * gap;
        int startX = 520 - gridW / 2;
        int startY = 220;
        for (int i = 0; i < gridSize_ * gridSize_; ++i) {
            int x = i % gridSize_;
            int y = i / gridSize_;
            SDL_Rect rect{startX + x * (cell + gap), startY + y * (cell + gap), cell, cell};
            fillRect(renderer, rect, color(57, 76, 85));
            drawRect(renderer, rect, i == cursor_ ? color(250, 216, 76) : color(151, 175, 179));
            drawLine(renderer, rect.x + 12, rect.y + rect.h / 2, rect.x + rect.w - 12, rect.y + rect.h / 2, color(132, 190, 174));
            drawLine(renderer, rect.x + rect.w / 2, rect.y + 12, rect.x + rect.w / 2, rect.y + rect.h - 12, color(132, 190, 174));
            fillRect(renderer, SDL_Rect{rect.x + rect.w / 2 - 5, rect.y + rect.h / 2 - 5, 10, 10}, color(216, 224, 206));
        }

        std::ostringstream timer;
        timer << "TIME " << static_cast<int>(std::ceil(timeLeft_)) << "s";
        text.draw(renderer, timer.str(), 130, 510, color(51, 62, 65), 1);
        text.draw(renderer, "WASD/ARROWS MOVE  |  SPACE CHOOSE  |  ESC EXIT", 130, 552, color(51, 62, 65));
    }

private:
    int gridSize_{4};
    int cursor_{0};
    int bug_{0};
    int reward_{20};
    int penalty_{5};
    float timeLeft_{10.0f};
    bool active_{false};
};

struct FloatingText {
    std::string text;
    float x{0.0f};
    float y{0.0f};
    float life{1.0f};
    SDL_Color color{255, 255, 255, 255};
};

struct TileEffect {
    int x{0};
    int y{0};
    float life{0.45f};
    SDL_Color color{255, 255, 255, 180};
};

class Game {
public:
    explicit Game(std::optional<unsigned int> mapSeed = std::nullopt) : world_(mapSeed) {}

    ~Game() {
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        text_.shutdown();
        TTF_Quit();
        SDL_Quit();
    }

    void init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
        if (TTF_Init() != 0) {
            throw std::runtime_error(TTF_GetError());
        }
        text_.init();

        window_ = SDL_CreateWindow(
            "Farm Village Simulator Demo",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            kWindowWidth,
            kWindowHeight,
            SDL_WINDOW_SHOWN
        );
        if (!window_) throw std::runtime_error(SDL_GetError());

        const char* renderDriverEnv = std::getenv("SDL_RENDER_DRIVER");
        bool forceSoftwareRenderer = renderDriverEnv && std::string(renderDriverEnv) == "software";
        if (forceSoftwareRenderer) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        } else {
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer_) {
                renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
            }
        }
        if (!renderer_) throw std::runtime_error(SDL_GetError());
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        log_.push("WELCOME TO FARM VILLAGE");
        log_.push("FARM, SELL, BUILD, AND WATCH THE WEATHER");
    }

    void run(bool smokeTest) {
        Uint64 last = SDL_GetPerformanceCounter();
        if (smokeTest) {
            update(1.0f / 60.0f);
            render();
            return;
        }

        while (running_) {
            Uint64 now = SDL_GetPerformanceCounter();
            float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
            last = now;
            dt = std::min(dt, 0.05f);

            handleEvents();
            update(dt);
            render();
        }
    }

private:
    GameContext makeContext(float dt) {
        return GameContext{player_, log_, rng_, events_.weather(), dt};
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running_ = false;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                if (mode_ == Mode::Help) handleHelpKey(event.key.keysym.sym);
                else if (location_ != Location::Farm) handleLocationKey(event.key.keysym.sym);
                else if (mode_ == Mode::Farm) handleFarmKey(event.key.keysym.sym);
                else if (mode_ == Mode::Fishing) handleFishingKey(event.key.keysym.sym);
                else if (mode_ == Mode::Memory) handleMemoryKey(event.key.keysym.sym);
                else if (mode_ == Mode::Shop) handleShopKey(event.key.keysym.sym);
            }
        }
    }

    void handleLocationKey(SDL_Keycode key) {
        if (mode_ == Mode::Debugging) {
            handleDebuggingKey(key);
            return;
        }
        if (key == SDLK_h) {
            mode_ = Mode::Help;
            return;
        }
        if (location_ == Location::BreadboardFarmLab) {
            handleBreadboardLabKey(key);
            return;
        }
        if (location_ == Location::GradStudentRanchLab) {
            handleRanchLabKey(key);
            return;
        }
        if (location_ == Location::ResearchLab) {
            handleResearchLabKey(key);
            return;
        }
        if (location_ == Location::Campus && isUseKey(key)) {
            if (!tryCampusBuilding301Use() && !tryCampusShuttleUse()) {
                log_.push("FACE BUILDING 301 OR THE SHUTTLE STOP");
            }
            return;
        }
        if (location_ == Location::Building301 && isUseKey(key)) {
            if (!tryBuilding301Use()) {
                log_.push("FACE A DOOR OR STAIRS");
            }
            return;
        }
        if (key == SDLK_ESCAPE && location_ == Location::Building301) {
            returnToCampusFromBuilding301();
        } else if (key == SDLK_ESCAPE && location_ != Location::Campus) {
            returnToFarm();
        }
    }

    void handleFarmKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE || key == SDLK_h) {
            mode_ = Mode::Help;
            return;
        }
        if (key == SDLK_q) {
            running_ = false;
            return;
        }

        if (key == SDLK_1) { currentAction_ = Action::Hoe; return; }
        if (key == SDLK_2) { currentAction_ = Action::Seed; log_.push("PLANT TOOL SELECTED: " + seedName(selectedCrop_)); return; }
        if (key == SDLK_3) { currentAction_ = Action::WateringCan; return; }
        if (key == SDLK_4) { currentAction_ = Action::AxePick; return; }
        if (key == SDLK_5) { currentAction_ = Action::Hand; return; }
        if (key == SDLK_6) { currentAction_ = Action::Build; return; }
        if (key == SDLK_t) {
            currentAction_ = Action::Seed;
            selectedCrop_ = CropType::Turnip;
            log_.push("SEED CHOICE: TURNIP SEED");
        }
        if (key == SDLK_p) {
            currentAction_ = Action::Seed;
            selectedCrop_ = CropType::Potato;
            log_.push("SEED CHOICE: POTATO SEED");
        }
        if (key == SDLK_TAB) {
            if (currentAction_ == Action::Seed) {
                selectedCrop_ = selectedCrop_ == CropType::Turnip ? CropType::Potato : CropType::Turnip;
                log_.push("SEED CHOICE: " + seedName(selectedCrop_));
            } else if (currentAction_ == Action::Build) {
                buildBarn_ = !buildBarn_;
                log_.push(std::string("BUILD CHOICE: ") + (buildBarn_ ? "BARN" : "FENCE"));
            }
        }

        if (isUseKey(key)) applyTool();
        if (key == SDLK_c) consumeFood();
        if (key == SDLK_f) startFishing();
        if (key == SDLK_m) startMemory();
    }

    void handleHelpKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE || key == SDLK_h) {
            mode_ = Mode::Farm;
            return;
        }
        if (key == SDLK_q) running_ = false;
    }

    void handleShopKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) {
            mode_ = Mode::Farm;
            log_.push("LEFT MARKETPLACE");
            return;
        }
        if (key == SDLK_1) buyTurnipSeeds();
        if (key == SDLK_2) buyPotatoSeeds();
        if (key == SDLK_3) buyFeed();
        if (key == SDLK_4) buyAnimalFromShop(AnimalKind::Chicken);
        if (key == SDLK_5) sellGoods();
        if (key == SDLK_6) buyAnimalFromShop(AnimalKind::Cow);
        if (key == SDLK_7) sellSpecificGoods("TURNIP", cropSellPrice("TURNIP"));
        if (key == SDLK_8) sellSpecificGoods("POTATO", cropSellPrice("POTATO"));
        if (key == SDLK_9) sellSpecificGoods("EGG", 10);
        if (key == SDLK_0) sellSpecificGoods("MILK", 24);
        if (key == SDLK_f) sellSpecificGoods("FISH", 16);
    }

    void handleFishingKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) mode_ = Mode::Farm;
        if (isUseKey(key)) {
            performStaminaAction(kStaminaFish, [&] {
                bool done = fishing_.cast(player_, log_, rng_);
                if (done) mode_ = Mode::Farm;
                return true;
            }, playerBasePoint());
        }
    }

    void handleMemoryKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) mode_ = Mode::Farm;
        if (key == SDLK_LEFT || key == SDLK_a) memory_.move(-1, 0);
        if (key == SDLK_RIGHT || key == SDLK_d) memory_.move(1, 0);
        if (key == SDLK_UP || key == SDLK_w) memory_.move(0, -1);
        if (key == SDLK_DOWN || key == SDLK_s) memory_.move(0, 1);
        if (isUseKey(key)) {
            memory_.flip(log_);
            if (memory_.finished()) mode_ = Mode::Farm;
        }
    }

    void handleBreadboardLabKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) {
            returnToBuilding301(2);
            return;
        }
        if (key >= SDLK_1 && key <= SDLK_5) {
            selectedDevice_ = std::clamp(static_cast<int>(key - SDLK_1), 0, static_cast<int>(kDeviceCatalog.size()) - 1);
            logHere("Device selected: " + kDeviceCatalog[selectedDevice_].name + ".");
            return;
        }
        if (key == SDLK_TAB) {
            selectedDeviceSlot_ = (selectedDeviceSlot_ + 1) % activeDeviceSlotCount();
            logHere("Device slot " + std::to_string(selectedDeviceSlot_ + 1) + ".");
            return;
        }
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
            int dx = key == SDLK_LEFT ? -1 : (key == SDLK_RIGHT ? 1 : 0);
            int dy = key == SDLK_UP ? -1 : (key == SDLK_DOWN ? 1 : 0);
            moveDeviceSlotSelection(dx, dy);
            return;
        }
        if (isUseKey(key)) {
            if (tryLabExitUse()) return;
            activateDeviceSlot();
        }
    }

    void handleRanchLabKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) {
            returnToBuilding301(3);
            return;
        }
        if (key >= SDLK_1 && key <= SDLK_4) {
            selectedResearcher_ = std::clamp(static_cast<int>(key - SDLK_1), 0, static_cast<int>(kResearcherCatalog.size()) - 1);
            logHere("Researcher selected: " + kResearcherCatalog[selectedResearcher_].name + ".");
            return;
        }
        if (key == SDLK_TAB) {
            selectedResearcherSlot_ = (selectedResearcherSlot_ + 1) % activeResearcherSlotCount();
            logHere("Researcher slot " + std::to_string(selectedResearcherSlot_ + 1) + ".");
            return;
        }
        if (isUseKey(key)) {
            if (tryLabExitUse()) return;
            activateResearcherSlot();
        }
    }

    void handleResearchLabKey(SDL_Keycode key) {
        if (researchMenuOpen_) {
            if (key == SDLK_ESCAPE) {
                researchMenuOpen_ = false;
                logHere("Closed Research Lab menu.");
                return;
            }
            if (key == SDLK_TAB || key == SDLK_u) {
                researchPanel_ = ResearchPanel::Upgrades;
                return;
            }
            if (key == SDLK_c) {
                researchPanel_ = ResearchPanel::Construction;
                return;
            }
            if (key >= SDLK_1 && key <= SDLK_5) {
                int choice = static_cast<int>(key - SDLK_1);
                if (researchPanel_ == ResearchPanel::Construction) {
                    buyResearchFacility(choice);
                } else {
                    buyResearchUpgrade(choice);
                }
                return;
            }
            return;
        }

        if (key == SDLK_ESCAPE) {
            returnToBuilding301(3);
            return;
        }
        if (isUseKey(key)) {
            if (tryLabExitUse()) return;
            if (tryResearchTerminalUse()) return;
            if (tryDebugComputerUse()) return;
            logHere("Face the exit, research terminal, or debugging computer.");
        }
    }

    void handleDebuggingKey(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) {
            debugging_.stop();
            mode_ = Mode::Farm;
            logHere("Debugging cancelled.");
            return;
        }
        if (key == SDLK_LEFT || key == SDLK_a) debugging_.move(-1, 0);
        if (key == SDLK_RIGHT || key == SDLK_d) debugging_.move(1, 0);
        if (key == SDLK_UP || key == SDLK_w) debugging_.move(0, -1);
        if (key == SDLK_DOWN || key == SDLK_s) debugging_.move(0, 1);
        if (isUseKey(key)) finishDebuggingAttempt();
    }

    std::pair<int, int> standingTile() const {
        int x = std::clamp(static_cast<int>(std::lround(playerTileX_)), 0, kGridWidth - 1);
        int y = std::clamp(static_cast<int>(std::lround(playerTileY_)), 0, kGridHeight - 1);
        return {x, y};
    }

    std::pair<int, int> facingTile() const {
        auto [x, y] = standingTile();
        switch (facing_) {
            case Direction::Down: ++y; break;
            case Direction::Up: --y; break;
            case Direction::Left: --x; break;
            case Direction::Right: ++x; break;
        }
        if (!world_.inBounds(x, y)) return standingTile();
        return {x, y};
    }

    SDL_Point playerWorldBasePoint() const {
        return SDL_Point{
            static_cast<int>(std::lround(playerTileX_ * kTileSize + kTileSize / 2.0f)),
            static_cast<int>(std::lround(playerTileY_ * kTileStepY + 48.0f))
        };
    }

    void updateCamera() {
        SDL_Point player = playerWorldBasePoint();
        float desiredX = static_cast<float>(player.x) - kViewWidth * 0.5f;
        float desiredY = static_cast<float>(player.y) - kViewHeight * 0.56f;
        float maxX = std::max(0.0f, static_cast<float>(kGridWidth * kTileSize - kViewWidth));
        float maxY = std::max(0.0f, static_cast<float>(kGridHeight * kTileStepY + kTileSize - kViewHeight));
        cameraX_ = std::clamp(desiredX, 0.0f, maxX);
        cameraY_ = std::clamp(desiredY, 0.0f, maxY);
    }

    SDL_Rect tileScreenRect(int x, int y) const {
        return SDL_Rect{
            kGridX + static_cast<int>(std::lround(x * kTileSize - cameraX_)),
            kGridY + static_cast<int>(std::lround(y * kTileStepY - cameraY_)),
            kTileSize,
            kTileSize
        };
    }

    SDL_Point tileCenterPoint(int x, int y) const {
        SDL_Rect tile = tileScreenRect(x, y);
        return SDL_Point{tile.x + tile.w / 2, tile.y + 28};
    }

    SDL_Point playerBasePoint() const {
        SDL_Point world = playerWorldBasePoint();
        return SDL_Point{
            kGridX + static_cast<int>(std::lround(world.x - cameraX_)),
            kGridY + static_cast<int>(std::lround(world.y - cameraY_))
        };
    }

    std::array<int, 4> visibleTileRange(int margin = 3) const {
        int minX = std::max(0, static_cast<int>(std::floor(cameraX_ / kTileSize)) - margin);
        int maxX = std::min(kGridWidth - 1, static_cast<int>(std::ceil((cameraX_ + kViewWidth) / kTileSize)) + margin);
        int minY = std::max(0, static_cast<int>(std::floor(cameraY_ / kTileStepY)) - margin);
        int maxY = std::min(kGridHeight - 1, static_cast<int>(std::ceil((cameraY_ + kViewHeight) / kTileStepY)) + margin);
        return {minX, maxX, minY, maxY};
    }

    void addFloatingText(const std::string& text, SDL_Point point, SDL_Color c = color(247, 238, 190)) {
        floatingTexts_.push_back(FloatingText{text, static_cast<float>(point.x), static_cast<float>(point.y), 1.15f, c});
    }

    void addTileEffect(int x, int y, SDL_Color c) {
        tileEffects_.push_back(TileEffect{x, y, 0.45f, c});
    }

    std::string eventTag() const {
        switch (location_) {
            case Location::Farm: return "Farm";
            case Location::Campus: return "Campus";
            case Location::Building301: return "Building 301";
            case Location::BreadboardFarmLab: return "Breadboard Lab";
            case Location::GradStudentRanchLab: return "Ranch Lab";
            case Location::ResearchLab: return "Research Lab";
        }
        return "Game";
    }

    void logHere(const std::string& line) {
        log_.push("[" + eventTag() + "] " + line);
    }

    int activeDeviceSlotCount() const {
        int count = 6;
        if (extraBreadboardStationBuilt_) count += 2;
        if (deviceSlotExpansionBuilt_) count += 1;
        return std::clamp(count, 1, static_cast<int>(deviceSlots_.size()));
    }

    void moveDeviceSlotSelection(int dx, int dy) {
        constexpr int columns = 3;
        int active = activeDeviceSlotCount();
        int row = selectedDeviceSlot_ / columns;
        int col = selectedDeviceSlot_ % columns;
        int next = std::clamp(row + dy, 0, (active - 1) / columns) * columns + std::clamp(col + dx, 0, columns - 1);
        selectedDeviceSlot_ = std::clamp(next, 0, active - 1);
        logHere("Device slot " + std::to_string(selectedDeviceSlot_ + 1) + ".");
    }

    int activeResearcherSlotCount() const {
        int count = 4;
        if (researcherDeskBuilt_) count += 1;
        if (researcherSlotExpansionBuilt_) count += 1;
        return std::clamp(count, 1, static_cast<int>(researcherSlots_.size()));
    }

    bool deviceUnlocked(const DeviceInfo& info) const {
        if (info.type == DeviceType::Mosfet || info.type == DeviceType::Finfet) {
            return advancedSemiconductorLicenseBuilt_ || player_.level() >= info.unlockLevel;
        }
        return player_.level() >= info.unlockLevel;
    }

    std::string deviceUnlockReason(const DeviceInfo& info) const {
        if (deviceUnlocked(info)) return "";
        if (info.type == DeviceType::Mosfet || info.type == DeviceType::Finfet) {
            return "NEEDS LV" + std::to_string(info.unlockLevel) + " OR LICENSE";
        }
        return "NEEDS LV" + std::to_string(info.unlockLevel);
    }

    float effectiveDeviceFailureChance(const DeviceInfo& info) const {
        float chance = info.failureChance;
        if (stabilizedCircuitDesignBuilt_) chance -= 0.05f;
        if (debugFailureShield_ > 0) chance -= 0.05f;
        return std::clamp(chance, 0.01f, 0.35f);
    }

    int researcherProductivity(const ResearcherSlot& slot) const {
        if (!slot.occupied) return 0;
        int value = researcherInfo(slot.type).researchPerTick;
        if (researcherProductivityBuilt_) value = static_cast<int>(std::ceil(value * 1.5f));
        if (slot.burnoutTimer > 0.0f) value = std::max(1, value / 2);
        return value;
    }

    bool payLabCost(int moneyCost, int researchCost) {
        if (player_.money().value() < moneyCost) {
            logHere("Not enough money.");
            return false;
        }
        if (!player_.canSpendResearchPoints(researchCost)) {
            logHere("Not enough research points.");
            return false;
        }
        player_.money().spend(moneyCost);
        player_.spendResearchPoints(researchCost);
        return true;
    }

    bool performStaminaAction(int cost, const std::function<bool()>& action, SDL_Point feedbackPoint) {
        if (!player_.canSpendStamina(cost)) {
            log_.push("TOO TIRED TO DO THAT");
            addFloatingText("TIRED", feedbackPoint, color(238, 118, 92));
            return false;
        }
        if (!action()) return false;
        player_.spendStamina(cost);
        return true;
    }

    void consumeFood() {
        int before = player_.stamina();
        if (player_.consumeFood(log_)) {
            int gained = player_.stamina() - before;
            std::string text = gained > 0 ? "+" + std::to_string(gained) + " STA" : "FULL";
            addFloatingText(text, playerBasePoint(), color(126, 221, 126));
        }
    }

    SDL_Rect campusBoundsRect() const {
        return SDL_Rect{kGridX + 18, kGridY + 18, kViewWidth - 36, kViewHeight - 36};
    }

    SDL_Rect campusPlayerRect() const {
        return SDL_Rect{static_cast<int>(std::lround(campusPlayerX_)), static_cast<int>(std::lround(campusPlayerY_)), 30, 42};
    }

    SDL_Rect campusReturnStopRect() const {
        return SDL_Rect{kGridX + 70, kGridY + 360, 76, 92};
    }

    SDL_Rect campusBuilding301Rect() const {
        return SDL_Rect{kGridX + 430, kGridY + 84, 300, 220};
    }

    SDL_Rect campusBuilding301DoorRect() const {
        SDL_Rect building = campusBuilding301Rect();
        return SDL_Rect{building.x + 118, building.y + building.h - 62, 64, 62};
    }

    SDL_Rect campusFacingRect() const {
        SDL_Rect rect = campusPlayerRect();
        constexpr int reach = 20;
        switch (facing_) {
            case Direction::Down: rect.y += reach; break;
            case Direction::Up: rect.y -= reach; break;
            case Direction::Left: rect.x -= reach; break;
            case Direction::Right: rect.x += reach; break;
        }
        return rect;
    }

    bool isCampusWalkable(const SDL_Rect& rect) const {
        SDL_Rect bounds = campusBoundsRect();
        if (rect.x < bounds.x || rect.y < bounds.y || rect.x + rect.w > bounds.x + bounds.w || rect.y + rect.h > bounds.y + bounds.h) {
            return false;
        }
        SDL_Rect stop = campusReturnStopRect();
        SDL_Rect building = campusBuilding301Rect();
        return !SDL_HasIntersection(&rect, &stop) && !SDL_HasIntersection(&rect, &building);
    }

    SDL_Rect building301BoundsRect() const {
        return SDL_Rect{kGridX + 58, kGridY + 62, kViewWidth - 116, kViewHeight - 118};
    }

    SDL_Rect building301PlayerRect() const {
        return SDL_Rect{static_cast<int>(std::lround(building301PlayerX_)), static_cast<int>(std::lround(building301PlayerY_)), 30, 42};
    }

    SDL_Rect building301ExitDoorRect() const {
        return SDL_Rect{kGridX + 368, kGridY + 454, 98, 58};
    }

    SDL_Rect building301DeskRect() const {
        return SDL_Rect{kGridX + 318, kGridY + 188, 198, 62};
    }

    SDL_Rect building301StairsUpRect() const {
        return SDL_Rect{kGridX + 628, kGridY + 168, 96, 86};
    }

    SDL_Rect building301StairsDownRect() const {
        return SDL_Rect{kGridX + 106, kGridY + 168, 96, 86};
    }

    SDL_Rect breadboardLabDoorRect() const {
        return SDL_Rect{kGridX + 318, kGridY + 138, 198, 72};
    }

    SDL_Rect ranchLabDoorRect() const {
        return SDL_Rect{kGridX + 146, kGridY + 142, 224, 72};
    }

    SDL_Rect researchLabDoorRect() const {
        return SDL_Rect{kGridX + 468, kGridY + 142, 224, 72};
    }

    SDL_Rect building301FacingRect() const {
        SDL_Rect rect = building301PlayerRect();
        constexpr int reach = 20;
        switch (facing_) {
            case Direction::Down: rect.y += reach; break;
            case Direction::Up: rect.y -= reach; break;
            case Direction::Left: rect.x -= reach; break;
            case Direction::Right: rect.x += reach; break;
        }
        return rect;
    }

    bool isBuilding301Walkable(const SDL_Rect& rect) const {
        SDL_Rect bounds = building301BoundsRect();
        if (rect.x < bounds.x || rect.y < bounds.y || rect.x + rect.w > bounds.x + bounds.w || rect.y + rect.h > bounds.y + bounds.h) {
            return false;
        }
        SDL_Rect desk = building301DeskRect();
        if (building301Floor_ == 1 && SDL_HasIntersection(&rect, &desk)) return false;
        return true;
    }

    SDL_Rect labBoundsRect() const {
        return SDL_Rect{kGridX + 52, kGridY + 52, kViewWidth - 104, kViewHeight - 106};
    }

    SDL_Rect labPlayerRect() const {
        return SDL_Rect{static_cast<int>(std::lround(labPlayerX_)), static_cast<int>(std::lround(labPlayerY_)), 30, 42};
    }

    SDL_Rect labExitDoorRect() const {
        return SDL_Rect{kGridX + 360, kGridY + 462, 118, 54};
    }

    SDL_Rect researchTerminalRect() const {
        return SDL_Rect{kGridX + 112, kGridY + 176, 252, 116};
    }

    SDL_Rect debugComputerRect() const {
        return SDL_Rect{kGridX + 464, kGridY + 176, 252, 116};
    }

    SDL_Rect labFacingRect() const {
        SDL_Rect rect = labPlayerRect();
        constexpr int reach = 20;
        switch (facing_) {
            case Direction::Down: rect.y += reach; break;
            case Direction::Up: rect.y -= reach; break;
            case Direction::Left: rect.x -= reach; break;
            case Direction::Right: rect.x += reach; break;
        }
        return rect;
    }

    bool isLabWalkable(const SDL_Rect& rect) const {
        SDL_Rect bounds = labBoundsRect();
        if (rect.x < bounds.x || rect.y < bounds.y || rect.x + rect.w > bounds.x + bounds.w || rect.y + rect.h > bounds.y + bounds.h) {
            return false;
        }
        if (location_ == Location::ResearchLab) {
            SDL_Rect terminal = researchTerminalRect();
            SDL_Rect debug = debugComputerRect();
            return !SDL_HasIntersection(&rect, &terminal) && !SDL_HasIntersection(&rect, &debug);
        }
        return true;
    }

    void updateCampusPlayer(float dt) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;

        if (dx == 0.0f && dy == 0.0f) {
            walking_ = false;
            return;
        }

        if (std::abs(dx) > std::abs(dy)) {
            facing_ = dx < 0.0f ? Direction::Left : Direction::Right;
        } else {
            facing_ = dy < 0.0f ? Direction::Up : Direction::Down;
        }

        float length = std::sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;

        constexpr float speed = 220.0f;
        bool moved = false;
        SDL_Rect nextXRect = campusPlayerRect();
        nextXRect.x = static_cast<int>(std::lround(campusPlayerX_ + dx * speed * dt));
        if (isCampusWalkable(nextXRect)) {
            campusPlayerX_ += dx * speed * dt;
            moved = true;
        }

        SDL_Rect nextYRect = campusPlayerRect();
        nextYRect.y = static_cast<int>(std::lround(campusPlayerY_ + dy * speed * dt));
        if (isCampusWalkable(nextYRect)) {
            campusPlayerY_ += dy * speed * dt;
            moved = true;
        }

        walking_ = moved;
        if (moved) walkTime_ += dt;
    }

    void updateBuilding301Player(float dt) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;

        if (dx == 0.0f && dy == 0.0f) {
            walking_ = false;
            return;
        }

        if (std::abs(dx) > std::abs(dy)) {
            facing_ = dx < 0.0f ? Direction::Left : Direction::Right;
        } else {
            facing_ = dy < 0.0f ? Direction::Up : Direction::Down;
        }

        float length = std::sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;

        constexpr float speed = 205.0f;
        bool moved = false;
        SDL_Rect nextXRect = building301PlayerRect();
        nextXRect.x = static_cast<int>(std::lround(building301PlayerX_ + dx * speed * dt));
        if (isBuilding301Walkable(nextXRect)) {
            building301PlayerX_ += dx * speed * dt;
            moved = true;
        }

        SDL_Rect nextYRect = building301PlayerRect();
        nextYRect.y = static_cast<int>(std::lround(building301PlayerY_ + dy * speed * dt));
        if (isBuilding301Walkable(nextYRect)) {
            building301PlayerY_ += dy * speed * dt;
            moved = true;
        }

        walking_ = moved;
        if (moved) walkTime_ += dt;
    }

    void updateLabPlayer(float dt) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;

        if (dx == 0.0f && dy == 0.0f) {
            walking_ = false;
            return;
        }

        if (std::abs(dx) > std::abs(dy)) {
            facing_ = dx < 0.0f ? Direction::Left : Direction::Right;
        } else {
            facing_ = dy < 0.0f ? Direction::Up : Direction::Down;
        }

        float length = std::sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;

        constexpr float speed = 205.0f;
        bool moved = false;
        SDL_Rect nextXRect = labPlayerRect();
        nextXRect.x = static_cast<int>(std::lround(labPlayerX_ + dx * speed * dt));
        if (isLabWalkable(nextXRect)) {
            labPlayerX_ += dx * speed * dt;
            moved = true;
        }

        SDL_Rect nextYRect = labPlayerRect();
        nextYRect.y = static_cast<int>(std::lround(labPlayerY_ + dy * speed * dt));
        if (isLabWalkable(nextYRect)) {
            labPlayerY_ += dy * speed * dt;
            moved = true;
        }

        walking_ = moved;
        if (moved) walkTime_ += dt;
    }

    void travelToCampus() {
        location_ = Location::Campus;
        mode_ = Mode::Farm;
        campusPlayerX_ = kCampusSpawnX;
        campusPlayerY_ = kCampusSpawnY;
        facing_ = Direction::Left;
        walking_ = false;
        log_.push("You boarded the SNU shuttle bus.");
        log_.push("Arrived at campus.");
    }

    void enterBuilding301() {
        location_ = Location::Building301;
        mode_ = Mode::Farm;
        building301Floor_ = 1;
        building301PlayerX_ = kBuilding301SpawnX;
        building301PlayerY_ = kBuilding301SpawnY;
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Entered Building 301.");
        log_.push("Welcome to Building 301.");
    }

    void moveBuilding301Floor(int floor) {
        building301Floor_ = std::clamp(floor, 1, 3);
        building301PlayerX_ = kBuilding301SpawnX;
        building301PlayerY_ = static_cast<float>(kGridY + 330);
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Building 301 " + std::to_string(building301Floor_) + "F");
    }

    void enterBreadboardLab() {
        location_ = Location::BreadboardFarmLab;
        mode_ = Mode::Farm;
        labReturnFloor_ = 2;
        labPlayerX_ = static_cast<float>(kGridX + 384);
        labPlayerY_ = static_cast<float>(kGridY + 392);
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Entered Breadboard Farm Lab.");
    }

    void enterRanchLab() {
        location_ = Location::GradStudentRanchLab;
        mode_ = Mode::Farm;
        labReturnFloor_ = 3;
        labPlayerX_ = static_cast<float>(kGridX + 384);
        labPlayerY_ = static_cast<float>(kGridY + 392);
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Entered Grad Student Ranch Lab.");
    }

    void enterResearchLab() {
        location_ = Location::ResearchLab;
        mode_ = Mode::Farm;
        researchMenuOpen_ = false;
        labReturnFloor_ = 3;
        labPlayerX_ = static_cast<float>(kGridX + 384);
        labPlayerY_ = static_cast<float>(kGridY + 392);
        facing_ = Direction::Down;
        walking_ = false;
        logHere("Entered Research Lab.");
    }

    void returnToBuilding301(int floor) {
        location_ = Location::Building301;
        mode_ = Mode::Farm;
        building301Floor_ = std::clamp(floor, 1, 3);
        building301PlayerX_ = kBuilding301SpawnX;
        building301PlayerY_ = static_cast<float>(kGridY + 382);
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Returned to Building 301 " + std::to_string(building301Floor_) + "F.");
    }

    void returnToCampusFromBuilding301() {
        location_ = Location::Campus;
        mode_ = Mode::Farm;
        SDL_Rect door = campusBuilding301DoorRect();
        campusPlayerX_ = static_cast<float>(door.x + door.w / 2 - 15);
        campusPlayerY_ = static_cast<float>(door.y + door.h + 2);
        facing_ = Direction::Down;
        walking_ = false;
        log_.push("Returned to campus.");
    }

    void returnToFarm() {
        location_ = Location::Farm;
        mode_ = Mode::Farm;
        playerTileX_ = kFarmShuttleReturnX;
        playerTileY_ = kFarmShuttleReturnY;
        facing_ = Direction::Right;
        walking_ = false;
        updateCamera();
        log_.push("Returned to the farm.");
    }

    bool tryCampusShuttleUse() {
        SDL_Rect facing = campusFacingRect();
        SDL_Rect stop = campusReturnStopRect();
        if (SDL_HasIntersection(&facing, &stop)) {
            returnToFarm();
            return true;
        }
        return false;
    }

    bool tryCampusBuilding301Use() {
        SDL_Rect facing = campusFacingRect();
        SDL_Rect door = campusBuilding301DoorRect();
        if (SDL_HasIntersection(&facing, &door)) {
            enterBuilding301();
            return true;
        }
        return false;
    }

    bool tryBuilding301Use() {
        SDL_Rect facing = building301FacingRect();
        SDL_Rect exit = building301ExitDoorRect();
        SDL_Rect up = building301StairsUpRect();
        SDL_Rect down = building301StairsDownRect();
        SDL_Rect breadboard = breadboardLabDoorRect();
        SDL_Rect ranch = ranchLabDoorRect();
        SDL_Rect research = researchLabDoorRect();
        if (building301Floor_ == 1 && SDL_HasIntersection(&facing, &exit)) {
            returnToCampusFromBuilding301();
            return true;
        }
        if (building301Floor_ < 3 && SDL_HasIntersection(&facing, &up)) {
            moveBuilding301Floor(building301Floor_ + 1);
            return true;
        }
        if (building301Floor_ > 1 && SDL_HasIntersection(&facing, &down)) {
            moveBuilding301Floor(building301Floor_ - 1);
            return true;
        }
        if (building301Floor_ == 2 && SDL_HasIntersection(&facing, &breadboard)) {
            enterBreadboardLab();
            return true;
        }
        if (building301Floor_ == 3 && SDL_HasIntersection(&facing, &ranch)) {
            enterRanchLab();
            return true;
        }
        if (building301Floor_ == 3 && SDL_HasIntersection(&facing, &research)) {
            enterResearchLab();
            return true;
        }
        return false;
    }

    bool tryLabExitUse() {
        SDL_Rect facing = labFacingRect();
        SDL_Rect exit = labExitDoorRect();
        if (SDL_HasIntersection(&facing, &exit)) {
            returnToBuilding301(labReturnFloor_);
            return true;
        }
        return false;
    }

    bool tryResearchTerminalUse() {
        SDL_Rect facing = labFacingRect();
        SDL_Rect terminal = researchTerminalRect();
        if (!SDL_HasIntersection(&facing, &terminal)) return false;
        researchMenuOpen_ = true;
        researchPanel_ = ResearchPanel::Construction;
        logHere("Opened facility expansion menu.");
        return true;
    }

    bool tryDebugComputerUse() {
        SDL_Rect facing = labFacingRect();
        SDL_Rect computer = debugComputerRect();
        if (!SDL_HasIntersection(&facing, &computer)) return false;
        startDebuggingGame();
        return true;
    }

    std::string buildingFloorName() const {
        if (building301Floor_ == 1) return "LOBBY";
        if (building301Floor_ == 2) return "BREADBOARD FARM";
        return "RANCH / RESEARCH";
    }

    void buyResearchFacility(int choice) {
        switch (choice) {
            case 0:
                if (extraBreadboardStationBuilt_) {
                    logHere("Extra Breadboard Station is already built.");
                    return;
                }
                if (!payLabCost(140, 20)) return;
                extraBreadboardStationBuilt_ = true;
                selectedDeviceSlot_ = std::min(selectedDeviceSlot_, activeDeviceSlotCount() - 1);
                logHere("Built Extra Breadboard Station. Device slots increased.");
                return;
            case 1:
                if (researcherDeskBuilt_) {
                    logHere("Researcher Desk is already built.");
                    return;
                }
                if (!payLabCost(120, 18)) return;
                researcherDeskBuilt_ = true;
                selectedResearcherSlot_ = std::min(selectedResearcherSlot_, activeResearcherSlotCount() - 1);
                logHere("Built Researcher Desk. Researcher slots increased.");
                return;
            case 2:
                if (autoExperimentLabBuilt_) {
                    logHere("Auto Experiment Lab is already built.");
                    return;
                }
                if (!payLabCost(180, 35)) return;
                autoExperimentLabBuilt_ = true;
                logHere("Built Auto Experiment Lab. Passive research started.");
                return;
            case 3:
                if (loungeBuilt_) {
                    logHere("Lounge is already built.");
                    return;
                }
                if (!payLabCost(150, 30)) return;
                loungeBuilt_ = true;
                logHere("Built Lounge. Researcher burnout chance reduced.");
                return;
            default:
                logHere("No construction option on that key.");
                return;
        }
    }

    void buyResearchUpgrade(int choice) {
        switch (choice) {
            case 0:
                if (stabilizedCircuitDesignBuilt_) {
                    logHere("Stabilized Circuit Design is already active.");
                    return;
                }
                if (!payLabCost(160, 45)) return;
                stabilizedCircuitDesignBuilt_ = true;
                logHere("Stabilized Circuit Design purchased. Device failures reduced.");
                return;
            case 1:
                if (researcherProductivityBuilt_) {
                    logHere("Researcher Productivity Program is already active.");
                    return;
                }
                if (!payLabCost(170, 50)) return;
                researcherProductivityBuilt_ = true;
                logHere("Researcher Productivity Program purchased. RP generation increased.");
                return;
            case 2:
                if (advancedSemiconductorLicenseBuilt_) {
                    logHere("Advanced Semiconductor License is already active.");
                    return;
                }
                if (!payLabCost(260, 80)) return;
                advancedSemiconductorLicenseBuilt_ = true;
                logHere("Advanced Semiconductor License purchased. MOSFET and FinFET unlocked.");
                return;
            case 3:
                if (deviceSlotExpansionBuilt_) {
                    logHere("Device Slot Expansion is already built.");
                    return;
                }
                if (!payLabCost(170, 45)) return;
                deviceSlotExpansionBuilt_ = true;
                selectedDeviceSlot_ = std::min(selectedDeviceSlot_, activeDeviceSlotCount() - 1);
                logHere("Device Slot Expansion built. Breadboard capacity increased.");
                return;
            case 4:
                if (researcherSlotExpansionBuilt_) {
                    logHere("Researcher Slot Expansion is already built.");
                    return;
                }
                if (!payLabCost(150, 45)) return;
                researcherSlotExpansionBuilt_ = true;
                selectedResearcherSlot_ = std::min(selectedResearcherSlot_, activeResearcherSlotCount() - 1);
                logHere("Researcher Slot Expansion built. Ranch capacity increased.");
                return;
            default:
                logHere("No upgrade option on that key.");
                return;
        }
    }

    void startDebuggingGame() {
        debugging_.start(player_.level(), rng_);
        mode_ = Mode::Debugging;
        researchMenuOpen_ = false;
        logHere("Debugging computer started.");
    }

    void finishDebuggingAttempt() {
        if (!debugging_.active()) return;
        if (debugging_.correct()) {
            int reward = debugging_.reward();
            player_.addResearchPoints(reward);
            debugFailureShield_ = 1;
            logHere("Debug successful. +" + std::to_string(reward) + " research points.");
            logHere("Critical bug fixed. Device failure risk reduced.");
        } else {
            int penalty = debugging_.penalty();
            player_.spendResearchPoints(penalty);
            logHere("Debug failed. -" + std::to_string(penalty) + " research points.");
        }
        debugging_.stop();
        mode_ = Mode::Farm;
    }

    void failDebuggingTimeout() {
        if (!debugging_.active()) return;
        debugging_.stop();
        mode_ = Mode::Farm;
        logHere("Debug failed. Time ran out.");
    }

    void activateDeviceSlot() {
        selectedDeviceSlot_ = std::clamp(selectedDeviceSlot_, 0, activeDeviceSlotCount() - 1);
        DeviceSlot& slot = deviceSlots_[selectedDeviceSlot_];
        if (!slot.occupied) {
            const DeviceInfo& info = kDeviceCatalog[selectedDevice_];
            if (!deviceUnlocked(info)) {
                logHere(info.name + " locked: " + deviceUnlockReason(info));
                return;
            }
            if (!player_.canSpendStamina(kStaminaPlantDevice)) {
                logHere("Too tired to plant device crops.");
                return;
            }
            if (!player_.money().spend(info.cost)) {
                logHere("Not enough money for " + info.name + ".");
                return;
            }
            player_.spendStamina(kStaminaPlantDevice);
            slot.occupied = true;
            slot.ready = false;
            slot.type = info.type;
            slot.growth = 0.0f;
            logHere(info.name + " planted on breadboard.");
            return;
        }

        const DeviceInfo& info = deviceInfo(slot.type);
        if (!slot.ready) {
            int percent = static_cast<int>(std::clamp(slot.growth / info.growSeconds, 0.0f, 1.0f) * 100.0f);
            logHere(info.name + " growing " + std::to_string(percent) + "%.");
            return;
        }
        if (!player_.canSpendStamina(kStaminaHarvestDevice)) {
            logHere("Too tired to harvest device crops.");
            return;
        }
        player_.spendStamina(kStaminaHarvestDevice);
        player_.money().add(info.sellValue);
        player_.addResearchPoints(info.researchReward);
        int xpGain = std::max(6, info.sellValue / 12);
        player_.addXp(xpGain, log_);
        logHere("Harvested " + info.name + " +$" + std::to_string(info.sellValue) + " +" + std::to_string(info.researchReward) + " RP +" + std::to_string(xpGain) + " XP.");
        slot = DeviceSlot{};
    }

    void activateResearcherSlot() {
        selectedResearcherSlot_ = std::clamp(selectedResearcherSlot_, 0, activeResearcherSlotCount() - 1);
        ResearcherSlot& slot = researcherSlots_[selectedResearcherSlot_];
        if (slot.occupied) {
            const ResearcherInfo& info = researcherInfo(slot.type);
            logHere(info.name + " is already working.");
            return;
        }

        const ResearcherInfo& info = kResearcherCatalog[selectedResearcher_];
        if (player_.level() < info.unlockLevel) {
            logHere(info.name + " unlocks at level " + std::to_string(info.unlockLevel) + ".");
            return;
        }
        if (!player_.money().spend(info.cost)) {
            logHere("Not enough money for " + info.name + ".");
            return;
        }
        slot.occupied = true;
        slot.type = info.type;
        slot.burnoutTimer = 0.0f;
        player_.addXp(8, log_);
        logHere("Hired " + info.name + ".");
    }

    std::vector<int> occupiedResearcherIndices() const {
        std::vector<int> indices;
        int active = activeResearcherSlotCount();
        for (int i = 0; i < active; ++i) {
            if (researcherSlots_[i].occupied) indices.push_back(i);
        }
        return indices;
    }

    void runResearcherRandomEvent() {
        std::vector<int> occupied = occupiedResearcherIndices();
        if (occupied.empty()) return;

        std::uniform_int_distribution<int> rollDist(0, 99);
        int roll = rollDist(rng_);
        int negativeRelief = loungeBuilt_ ? 10 : 0;
        if (roll >= 45 - negativeRelief) return;

        if (roll < 18) {
            int gain = 18 + static_cast<int>(occupied.size()) * 4;
            player_.addResearchPoints(gain);
            log_.push("[Ranch Lab] Paper accepted. +" + std::to_string(gain) + " research points.");
            return;
        }
        if (roll < 23) {
            int gain = 35 + static_cast<int>(occupied.size()) * 6;
            player_.addResearchPoints(gain);
            log_.push("[Ranch Lab] Conference award. +" + std::to_string(gain) + " research points.");
            return;
        }

        std::uniform_int_distribution<int> pickDist(0, static_cast<int>(occupied.size()) - 1);
        ResearcherSlot& slot = researcherSlots_[occupied[pickDist(rng_)]];
        const ResearcherInfo& info = researcherInfo(slot.type);

        if (roll < 33) {
            player_.spendResearchPoints(8);
            slot.burnoutTimer = std::max(slot.burnoutTimer, loungeBuilt_ ? 5.0f : 9.0f);
            log_.push("[Ranch Lab] Paper rejected. " + info.name + " productivity paused.");
            return;
        }
        if (roll < 42) {
            slot.burnoutTimer = std::max(slot.burnoutTimer, loungeBuilt_ ? 8.0f : 16.0f);
            log_.push("[Ranch Lab] Burnout. " + info.name + " productivity reduced.");
            return;
        }
        if (loungeBuilt_ && roll % 2 == 0) {
            log_.push("[Ranch Lab] Lounge prevented a researcher from leaving.");
            return;
        }
        slot = ResearcherSlot{};
        log_.push("[Ranch Lab] Researcher left after a difficult semester.");
    }

    void updateLabSystems(float dt) {
        int activeDevices = activeDeviceSlotCount();
        for (int i = 0; i < activeDevices; ++i) {
            DeviceSlot& slot = deviceSlots_[i];
            if (!slot.occupied || slot.ready) continue;
            const DeviceInfo& info = deviceInfo(slot.type);
            slot.growth += dt;
            if (slot.growth >= info.growSeconds) {
                slot.growth = info.growSeconds;
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                float failureChance = effectiveDeviceFailureChance(info);
                if (debugFailureShield_ > 0) --debugFailureShield_;
                if (dist(rng_) < failureChance) {
                    slot = DeviceSlot{};
                    int pct = static_cast<int>(std::round(failureChance * 100.0f));
                    log_.push("[Breadboard Lab] " + info.name + " failed during fabrication. Risk was " + std::to_string(pct) + "%.");
                    continue;
                }
                slot.ready = true;
                log_.push("[Breadboard Lab] " + info.name + " ready in Breadboard Lab.");
            }
        }

        for (auto& slot : researcherSlots_) {
            if (slot.burnoutTimer > 0.0f) slot.burnoutTimer = std::max(0.0f, slot.burnoutTimer - dt);
        }

        if (autoExperimentLabBuilt_) {
            autoExperimentTick_ += dt;
            if (autoExperimentTick_ >= 18.0f) {
                autoExperimentTick_ = 0.0f;
                player_.addResearchPoints(4);
                log_.push("[Research Lab] Auto Experiment Lab generated +4 RP.");
            }
        }

        researcherTick_ += dt;
        if (researcherTick_ >= 15.0f) {
            researcherTick_ = 0.0f;
            int gain = 0;
            int activeResearchers = activeResearcherSlotCount();
            for (int i = 0; i < activeResearchers; ++i) {
                gain += researcherProductivity(researcherSlots_[i]);
            }
            if (gain > 0) {
                player_.addResearchPoints(gain);
                log_.push("[Ranch Lab] Research team generated +" + std::to_string(gain) + " RP.");
            }
        }

        researcherEventTick_ += dt;
        if (researcherEventTick_ >= 38.0f) {
            researcherEventTick_ = 0.0f;
            runResearcherRandomEvent();
        }
    }

    void updateFarmer(float dt) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;

        if (dx == 0.0f && dy == 0.0f) {
            walking_ = false;
            return;
        }

        walking_ = true;
        if (std::abs(dx) > std::abs(dy)) {
            facing_ = dx < 0.0f ? Direction::Left : Direction::Right;
        } else {
            facing_ = dy < 0.0f ? Direction::Up : Direction::Down;
        }

        float length = std::sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;

        constexpr float speed = 2.85f;
        float nextX = std::clamp(playerTileX_ + dx * speed * dt, 0.0f, static_cast<float>(kGridWidth - 1));
        float nextY = std::clamp(playerTileY_ + dy * speed * dt, 0.0f, static_cast<float>(kGridHeight - 1));
        bool moved = false;

        if (world_.isWalkableAt(nextX, playerTileY_)) {
            playerTileX_ = nextX;
            moved = true;
        }
        if (world_.isWalkableAt(playerTileX_, nextY)) {
            playerTileY_ = nextY;
            moved = true;
        }

        walking_ = moved;
        if (moved) walkTime_ += dt;
        updateCamera();
    }

    void applyTool() {
        GameContext ctx = makeContext(0.0f);
        auto target = facingTile();
        int targetX = target.first;
        int targetY = target.second;
        SDL_Point targetPoint = tileCenterPoint(targetX, targetY);
        Tile& targetTile = world_.at(targetX, targetY);
        if (targetTile.entity && targetTile.entity->opensCampusShuttle()) {
            world_.interact(targetX, targetY, Action::Hand, ctx);
            travelToCampus();
            addFloatingText("SNU", targetPoint, color(128, 206, 247));
            return;
        }
        if (targetTile.entity && targetTile.entity->opensMarketplace()) {
            world_.interact(targetX, targetY, Action::Hand, ctx);
            openShop("MARKET OPENED");
            addFloatingText("SHOP", targetPoint, color(250, 227, 130));
            return;
        }
        switch (currentAction_) {
            case Action::Hoe:
                if (performStaminaAction(kStaminaHoe, [&] {
                    return world_.hoe(targetX, targetY, ctx);
                }, targetPoint)) {
                    addTileEffect(targetX, targetY, color(160, 104, 62, 180));
                    addFloatingText("+SOIL", targetPoint);
                }
                break;
            case Action::Seed:
                if (performStaminaAction(kStaminaPlant, [&] {
                    return world_.plant(targetX, targetY, selectedCrop_, ctx);
                }, targetPoint)) {
                    addTileEffect(targetX, targetY, color(111, 170, 88, 170));
                    addFloatingText("+" + cropName(selectedCrop_), targetPoint);
                }
                break;
            case Action::WateringCan:
                if (performStaminaAction(kStaminaWater, [&] {
                    return world_.interact(targetX, targetY, Action::WateringCan, ctx) != InteractionResult::None;
                }, targetPoint)) {
                    addTileEffect(targetX, targetY, color(95, 164, 214, 190));
                    addFloatingText("SPLASH", targetPoint, color(168, 220, 249));
                }
                break;
            case Action::AxePick: {
                const Tile& before = world_.at(targetX, targetY);
                std::string name = before.entity ? before.entity->name() : tileKindName(before.kind);
                if (performStaminaAction(kStaminaClear, [&] {
                    return world_.interact(targetX, targetY, Action::AxePick, ctx) == InteractionResult::Remove;
                }, targetPoint)) {
                    addTileEffect(targetX, targetY, color(205, 183, 132, 190));
                    addFloatingText("-" + name, targetPoint);
                }
                break;
            }
            case Action::Hand: {
                const Tile& tile = world_.at(targetX, targetY);
                std::string name = tile.entity ? tile.entity->name() : tileKindName(tile.kind);
                bool isCrop = name == "TURNIP" || name == "POTATO";
                InteractionResult result = InteractionResult::None;
                bool ok = isCrop
                    ? performStaminaAction(kStaminaHarvest, [&] {
                        result = world_.interact(targetX, targetY, Action::Hand, ctx);
                        return result == InteractionResult::Remove;
                    }, targetPoint)
                    : ([&] {
                        result = world_.interact(targetX, targetY, Action::Hand, ctx);
                        return result != InteractionResult::None;
                    })();
                if (result == InteractionResult::Remove) {
                    addFloatingText("+" + name, targetPoint, color(250, 227, 130));
                    addTileEffect(targetX, targetY, color(250, 227, 130, 180));
                } else if (ok && name == "BED") {
                    addFloatingText("RESTED", targetPoint, color(126, 221, 126));
                }
                break;
            }
            case Action::Build: {
                const Tile& tile = world_.at(targetX, targetY);
                if (tile.entity) {
                    world_.interact(targetX, targetY, Action::Build, ctx);
                } else if (performStaminaAction(buildBarn_ ? kStaminaBuildBarn : kStaminaBuildFence, [&] {
                    auto [standX, standY] = standingTile();
                    int remainingExits = 0;
                    const std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                    for (const auto& [dx, dy] : dirs) {
                        int nx = standX + dx;
                        int ny = standY + dy;
                        if (nx == targetX && ny == targetY) continue;
                        if (world_.isWalkable(nx, ny)) ++remainingExits;
                    }
                    if (remainingExits == 0) {
                        ctx.log.push("[Action] That placement would trap you.");
                        return false;
                    }
                    return world_.build(targetX, targetY, buildBarn_, ctx);
                }, targetPoint)) {
                    addTileEffect(targetX, targetY, color(232, 196, 127, 190));
                    addFloatingText(buildBarn_ ? "+BARN" : "+FENCE", targetPoint);
                }
                break;
            }
        }
    }

    void openShop(const std::string& message = "MARKETPLACE OPEN") {
        mode_ = Mode::Shop;
        log_.push(message);
    }

    void buyTurnipSeeds() {
        if (player_.money().spend(15)) {
            player_.inventory().add("TURNIP SEED", 3);
            addFloatingText("-$15", playerBasePoint(), color(246, 207, 94));
            log_.push("BOUGHT 3 TURNIP SEEDS");
        } else {
            log_.push("NOT ENOUGH MONEY FOR TURNIP SEEDS");
        }
    }

    void buyPotatoSeeds() {
        if (!player_.potatoUnlocked()) {
            log_.push("POTATO SEEDS UNLOCK AT LEVEL 2");
            return;
        }
        if (player_.money().spend(24)) {
            player_.inventory().add("POTATO SEED", 3);
            addFloatingText("-$24", playerBasePoint(), color(246, 207, 94));
            log_.push("BOUGHT 3 POTATO SEEDS");
        } else {
            log_.push("NOT ENOUGH MONEY FOR POTATO SEEDS");
        }
    }

    void buyFeed() {
        if (player_.money().spend(9)) {
            player_.inventory().add("FEED", 3);
            addFloatingText("-$9", playerBasePoint(), color(246, 207, 94));
            log_.push("BOUGHT 3 FEED");
        } else {
            log_.push("NOT ENOUGH MONEY FOR FEED");
        }
    }

    std::optional<std::pair<int, int>> findAnimalPlacementTile() const {
        auto origin = standingTile();
        int originX = origin.first;
        int originY = origin.second;
        auto safeForDelivery = [&](int x, int y) {
            if (!world_.canPlaceAnimalAt(x, y)) return false;
            if (std::abs(x - 8) <= 6 && std::abs(y - 4) <= 6) return false;
            if (std::abs(x - originX) <= 1 && std::abs(y - originY) <= 1) return false;
            return true;
        };

        for (int y = 12; y <= 17; ++y) {
            for (int x = 6; x <= 12; ++x) {
                if (safeForDelivery(x, y)) return std::make_pair(x, y);
            }
        }

        for (int radius = 6; radius <= 22; ++radius) {
            for (int y = originY - radius; y <= originY + radius; ++y) {
                for (int x = originX - radius; x <= originX + radius; ++x) {
                    if (std::max(std::abs(x - originX), std::abs(y - originY)) != radius) continue;
                    if (safeForDelivery(x, y)) return std::make_pair(x, y);
                }
            }
        }
        return std::nullopt;
    }

    void buyAnimalFromShop(AnimalKind kind) {
        auto placement = findAnimalPlacementTile();
        if (!placement) {
            log_.push("NO SAFE PEN TILE FOR " + animalName(kind));
            return;
        }
        auto [targetX, targetY] = *placement;
        GameContext ctx = makeContext(0.0f);
        if (world_.placeAnimal(targetX, targetY, kind, ctx)) {
            addFloatingText("+" + animalName(kind), tileCenterPoint(targetX, targetY));
            addTileEffect(targetX, targetY, color(232, 209, 137, 190));
            log_.push(animalName(kind) + " DELIVERED TO THE ANIMAL PEN");
        }
    }

    void sellGoods() {
        std::map<std::string, int> prices{
            {"TURNIP", cropSellPrice("TURNIP")},
            {"POTATO", cropSellPrice("POTATO")},
            {"EGG", 10},
            {"MILK", 24},
            {"FISH", 16},
            {"GEM", 30}
        };

        int total = 0;
        for (const auto& [item, price] : prices) {
            int count = player_.inventory().count(item);
            if (count <= 0) continue;
            player_.inventory().remove(item, count);
            total += count * price;
        }

        if (total == 0) {
            log_.push("NO SELLABLE GOODS");
            return;
        }
        player_.money().add(total);
        player_.addXp(std::max(4, total / 8), log_);
        addFloatingText("+$" + std::to_string(total), playerBasePoint(), color(246, 207, 94));
        std::ostringstream msg;
        msg << "SOLD GOODS FOR $" << total;
        log_.push(msg.str());
    }

    void sellSpecificGoods(const std::string& item, int price) {
        int count = player_.inventory().count(item);
        if (count <= 0) {
            log_.push("NO " + item + " TO SELL");
            return;
        }
        player_.inventory().remove(item, count);
        int total = count * price;
        player_.money().add(total);
        player_.addXp(std::max(2, total / 9), log_);
        addFloatingText("+$" + std::to_string(total), playerBasePoint(), color(246, 207, 94));
        log_.push("SOLD " + item + " x" + std::to_string(count) + " FOR $" + std::to_string(total));
    }

    void startFishing() {
        if (!player_.fishingUnlocked()) {
            log_.push("FISHING ROD UNLOCKS AT LEVEL 3");
            return;
        }
        auto [targetX, targetY] = facingTile();
        auto [standX, standY] = standingTile();
        const Tile& target = world_.at(targetX, targetY);
        const Tile& standing = world_.at(standX, standY);
        if (target.kind != TileKind::Pond && standing.kind != TileKind::Pond) {
            log_.push("FACE THE POND TO FISH");
            return;
        }
        fishing_.start(rng_);
        mode_ = Mode::Fishing;
        log_.push("FISHING STARTED");
    }

    void startMemory() {
        memory_.start(rng_);
        mode_ = Mode::Memory;
        log_.push("MEMORY FAIR STARTED");
    }

    void updateEffects(float dt) {
        for (auto& text : floatingTexts_) {
            text.life -= dt;
            text.y -= dt * 28.0f;
            text.color.a = static_cast<Uint8>(std::clamp(text.life / 1.15f, 0.0f, 1.0f) * 255.0f);
        }
        floatingTexts_.erase(
            std::remove_if(floatingTexts_.begin(), floatingTexts_.end(), [](const FloatingText& text) {
                return text.life <= 0.0f;
            }),
            floatingTexts_.end()
        );

        for (auto& effect : tileEffects_) {
            effect.life -= dt;
            effect.color.a = static_cast<Uint8>(std::clamp(effect.life / 0.45f, 0.0f, 1.0f) * 190.0f);
        }
        tileEffects_.erase(
            std::remove_if(tileEffects_.begin(), tileEffects_.end(), [](const TileEffect& effect) {
                return effect.life <= 0.0f;
            }),
            tileEffects_.end()
        );
    }

    void update(float dt) {
        updateLabSystems(dt);
        if (location_ != Location::Farm) {
            updateLocation(dt);
            return;
        }
        if (mode_ == Mode::Fishing) {
            fishing_.update(dt);
            return;
        }
        if (mode_ == Mode::Memory) {
            memory_.update(dt, player_, log_);
            return;
        }
        if (mode_ == Mode::Shop) {
            updateEffects(dt);
            return;
        }
        if (mode_ == Mode::Help) {
            updateEffects(dt);
            return;
        }

        clock_ += dt * 0.42f;
        if (clock_ >= 24.0f) {
            clock_ -= 24.0f;
            ++day_;
            log_.push("A NEW FARM DAY BEGINS");
        }

        updateFarmer(dt);
        updateEffects(dt);

        GameContext ctx = makeContext(dt);
        events_.update(dt, ctx, world_);
        ctx.weather = events_.weather();
        if (ctx.weather == Weather::Rain) {
            if (!rainNoticeShown_ || rainNoticeDay_ != day_) {
                rainNoticeShown_ = true;
                rainNoticeDay_ = day_;
                log_.push("[Weather] Rain watered outdoor crops.");
            }
        } else {
            rainNoticeShown_ = false;
        }
        world_.update(ctx);
    }

    void updateLocation(float dt) {
        if (mode_ == Mode::Help) {
            walking_ = false;
            updateEffects(dt);
            return;
        }
        if (mode_ == Mode::Debugging) {
            debugging_.update(dt);
            if (debugging_.expired()) failDebuggingTimeout();
            updateEffects(dt);
            return;
        }
        if (location_ == Location::ResearchLab && researchMenuOpen_) {
            walking_ = false;
            updateEffects(dt);
            return;
        }
        if (location_ == Location::Campus) {
            updateCampusPlayer(dt);
        } else if (location_ == Location::Building301) {
            updateBuilding301Player(dt);
        } else if (location_ == Location::BreadboardFarmLab || location_ == Location::GradStudentRanchLab || location_ == Location::ResearchLab) {
            updateLabPlayer(dt);
        }
        updateEffects(dt);
    }

    void render() {
        if (location_ != Location::Farm) {
            renderLocationPlaceholder();
            if (mode_ == Mode::Help) renderHelpOverlay();
            SDL_RenderPresent(renderer_);
            return;
        }
        if (mode_ == Mode::Fishing) {
            fishing_.render(renderer_, text_);
            SDL_RenderPresent(renderer_);
            return;
        }
        if (mode_ == Mode::Memory) {
            memory_.render(renderer_, text_);
            SDL_RenderPresent(renderer_);
            return;
        }

        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(203, 217, 194));
        updateCamera();
        renderHeader();
        renderWorld();
        renderWeatherOverlay();
        renderFloatingTexts();
        renderHotbar();
        renderPanel();
        if (mode_ == Mode::Shop) renderShop();
        if (mode_ == Mode::Help) renderHelpOverlay();
        SDL_RenderPresent(renderer_);
    }

    void renderCampusShuttleStop(const SDL_Rect& stop) {
        fillRect(renderer_, SDL_Rect{stop.x + 34, stop.y + 18, 7, stop.h - 18}, color(58, 67, 67));
        fillRect(renderer_, SDL_Rect{stop.x + 10, stop.y + 6, stop.w - 20, 28}, color(47, 102, 165));
        drawRect(renderer_, SDL_Rect{stop.x + 10, stop.y + 6, stop.w - 20, 28}, color(230, 238, 225));
        text_.draw(renderer_, "SNU", stop.x + 21, stop.y + 10, color(244, 247, 232), 1);
        fillRect(renderer_, SDL_Rect{stop.x + 11, stop.y + 49, stop.w - 22, 25}, color(78, 151, 187));
        drawRect(renderer_, SDL_Rect{stop.x + 11, stop.y + 49, stop.w - 22, 25}, color(38, 76, 105));
        fillRect(renderer_, SDL_Rect{stop.x + 18, stop.y + 56, 10, 8}, color(233, 240, 222));
        fillRect(renderer_, SDL_Rect{stop.x + stop.w - 29, stop.y + 56, 10, 8}, color(233, 240, 222));
        fillRect(renderer_, SDL_Rect{stop.x + 20, stop.y + 76, 10, 10}, color(38, 43, 43));
        fillRect(renderer_, SDL_Rect{stop.x + stop.w - 30, stop.y + 76, 10, 10}, color(38, 43, 43));
    }

    void renderScreenPlayer(const SDL_Rect& player) {
        int bob = walking_ ? static_cast<int>(std::sin(walkTime_ * 13.0f) * 2.0f) : 0;
        fillRect(renderer_, SDL_Rect{player.x - 3, player.y + player.h - 4, player.w + 6, 6}, color(51, 61, 55, 120));
        fillRect(renderer_, SDL_Rect{player.x + 5, player.y + 21 + bob, 8, 17 - bob}, color(48, 89, 130));
        fillRect(renderer_, SDL_Rect{player.x + 17, player.y + 21 - bob, 8, 17 + bob}, color(48, 89, 130));
        fillRect(renderer_, SDL_Rect{player.x + 3, player.y + 8, 24, 20}, color(188, 81, 73));
        fillRect(renderer_, SDL_Rect{player.x + 8, player.y + 11, 14, 21}, color(62, 117, 156));
        drawRect(renderer_, SDL_Rect{player.x + 3, player.y + 8, 24, 20}, color(85, 56, 52));
        fillRect(renderer_, SDL_Rect{player.x + 7, player.y - 8, 16, 16}, color(226, 169, 118));
        drawRect(renderer_, SDL_Rect{player.x + 7, player.y - 8, 16, 16}, color(95, 64, 49));
        fillRect(renderer_, SDL_Rect{player.x + 4, player.y - 12, 22, 6}, color(116, 83, 47));
        if (facing_ == Direction::Left) {
            fillRect(renderer_, SDL_Rect{player.x + 10, player.y - 2, 3, 3}, color(49, 44, 42));
        } else if (facing_ == Direction::Right) {
            fillRect(renderer_, SDL_Rect{player.x + 18, player.y - 2, 3, 3}, color(49, 44, 42));
        } else if (facing_ == Direction::Down) {
            fillRect(renderer_, SDL_Rect{player.x + 11, player.y - 2, 3, 3}, color(49, 44, 42));
            fillRect(renderer_, SDL_Rect{player.x + 17, player.y - 2, 3, 3}, color(49, 44, 42));
        }
    }

    void renderCampusPlayer() {
        renderScreenPlayer(campusPlayerRect());
    }

    void renderBuilding301Player() {
        renderScreenPlayer(building301PlayerRect());
    }

    void renderLabPlayer() {
        renderScreenPlayer(labPlayerRect());
    }

    void renderCampusPanel() {
        fillRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(225, 232, 220));
        drawRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(73, 93, 91));
        text_.draw(renderer_, "LOCATION", kPanelX + 22, 138, color(65, 79, 75), 1);
        text_.draw(renderer_, locationName(location_), kPanelX + 22, 166, color(37, 56, 55), 1);

        text_.draw(renderer_, "CAMPUS INFO", kPanelX + 22, 222, color(65, 79, 75));
        text_.draw(renderer_, "BUILDING 301 OPEN", kPanelX + 22, 248, color(37, 56, 55));
        text_.draw(renderer_, "RETURN SHUTTLE READY", kPanelX + 22, 272, color(37, 56, 55));

        text_.draw(renderer_, "KEYS", kPanelX + 194, 302, color(65, 79, 75));
        std::array<std::string, 5> controls{
            "WASD WALK",
            "USE: Space/Enter",
            "FACE 301 DOOR",
            "FACE BUS STOP",
            "E ALT USE"
        };
        int controlY = 326;
        for (const auto& line : controls) {
            text_.draw(renderer_, line, kPanelX + 194, controlY, color(37, 56, 55));
            controlY += 22;
        }

        text_.draw(renderer_, "EVENTS", kPanelX + 22, 498, color(65, 79, 75));
        int logY = 522;
        int count = 0;
        for (const auto& line : log_.lines()) {
            if (count++ >= 6) break;
            text_.draw(renderer_, line, kPanelX + 22, logY, color(75, 58, 48));
            logY += 18;
        }
    }

    void renderCampus() {
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(198, 214, 203));
        renderHeader();
        SDL_Rect viewport{kGridX, kGridY, kViewWidth, kViewHeight};
        fillRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(69, 91, 82));
        drawRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(42, 62, 57));
        fillRect(renderer_, viewport, color(117, 158, 111));

        fillRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, 178}, color(106, 165, 201));
        fillRect(renderer_, SDL_Rect{kGridX, kGridY + 320, kViewWidth, 74}, color(180, 158, 108));
        fillRect(renderer_, SDL_Rect{kGridX + 124, kGridY + 98, 86, 420}, color(188, 168, 116));
        fillRect(renderer_, SDL_Rect{kGridX + 124, kGridY + 318, 640, 76}, color(188, 168, 116));
        fillRect(renderer_, SDL_Rect{kGridX + 536, kGridY + 278, 72, 170}, color(188, 168, 116));
        text_.draw(renderer_, "SNU ENGINEERING CAMPUS", kGridX + 42, kGridY + 36, color(248, 245, 225), 2);

        std::array<SDL_Point, 7> trees{{
            {kGridX + 58, kGridY + 224},
            {kGridX + 246, kGridY + 230},
            {kGridX + 684, kGridY + 226},
            {kGridX + 738, kGridY + 448},
            {kGridX + 270, kGridY + 456},
            {kGridX + 72, kGridY + 482},
            {kGridX + 358, kGridY + 116}
        }};
        for (const SDL_Point& tree : trees) {
            fillRect(renderer_, SDL_Rect{tree.x + 10, tree.y + 30, 10, 24}, color(104, 72, 44));
            fillRect(renderer_, SDL_Rect{tree.x - 2, tree.y + 9, 34, 31}, color(47, 113, 63));
            fillRect(renderer_, SDL_Rect{tree.x + 4, tree.y - 5, 24, 25}, color(61, 140, 75));
        }

        std::array<SDL_Rect, 3> benches{{
            SDL_Rect{kGridX + 226, kGridY + 350, 62, 18},
            SDL_Rect{kGridX + 646, kGridY + 350, 62, 18},
            SDL_Rect{kGridX + 222, kGridY + 146, 62, 18}
        }};
        for (const SDL_Rect& bench : benches) {
            fillRect(renderer_, bench, color(126, 82, 52));
            fillRect(renderer_, SDL_Rect{bench.x + 6, bench.y + 18, 6, 12}, color(76, 62, 50));
            fillRect(renderer_, SDL_Rect{bench.x + bench.w - 12, bench.y + 18, 6, 12}, color(76, 62, 50));
            drawRect(renderer_, bench, color(72, 53, 42));
        }

        SDL_Rect campusSign{kGridX + 36, kGridY + 182, 152, 38};
        fillRect(renderer_, campusSign, color(45, 93, 76));
        drawRect(renderer_, campusSign, color(229, 237, 218));
        text_.draw(renderer_, "SNU", campusSign.x + 12, campusSign.y + 7, color(241, 247, 230), 1);
        text_.draw(renderer_, "ENGINEERING", campusSign.x + 52, campusSign.y + 8, color(241, 247, 230));

        SDL_Rect building = campusBuilding301Rect();
        SDL_Rect door = campusBuilding301DoorRect();
        fillRect(renderer_, SDL_Rect{building.x - 8, building.y - 18, building.w + 16, 32}, color(78, 82, 96));
        fillRect(renderer_, building, color(128, 132, 143));
        drawRect(renderer_, building, color(226, 230, 232));
        for (int y = building.y + 28; y < building.y + 150; y += 42) {
            for (int x = building.x + 24; x < building.x + building.w - 34; x += 54) {
                fillRect(renderer_, SDL_Rect{x, y, 28, 18}, color(165, 185, 208));
                drawRect(renderer_, SDL_Rect{x, y, 28, 18}, color(217, 228, 236));
            }
        }
        fillRect(renderer_, door, color(72, 73, 82));
        drawRect(renderer_, door, color(226, 230, 232));
        text_.draw(renderer_, "301", building.x + 124, building.y + 76, color(244, 242, 222), 2);
        text_.draw(renderer_, "Building 301", building.x + 82, building.y + building.h + 10, color(42, 57, 51), 1);

        SDL_Rect stop = campusReturnStopRect();
        renderCampusShuttleStop(stop);
        text_.draw(renderer_, "RETURN TO FARM", stop.x - 14, stop.y + stop.h + 10, color(41, 63, 58));

        SDL_Rect facing = campusFacingRect();
        if (SDL_HasIntersection(&facing, &door)) {
            drawRect(renderer_, door, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: enter Building 301", kGridX + 260, kGridY + 505, color(42, 57, 51), 1);
        } else if (SDL_HasIntersection(&facing, &stop)) {
            drawRect(renderer_, stop, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: board shuttle", kGridX + 270, kGridY + 505, color(42, 57, 51), 1);
        } else {
            text_.draw(renderer_, "Use Building 301 or the SNU return shuttle.", kGridX + 230, kGridY + 505, color(42, 57, 51), 1);
        }

        renderCampusPlayer();
        renderFloatingTexts();
        renderCampusPanel();
    }

    void renderBuilding301Panel() {
        fillRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(229, 228, 218));
        drawRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(82, 82, 90));
        text_.draw(renderer_, "LOCATION", kPanelX + 22, 138, color(70, 70, 78), 1);
        text_.draw(renderer_, locationName(location_), kPanelX + 22, 166, color(42, 46, 50), 1);

        text_.draw(renderer_, "BUILDING INFO", kPanelX + 22, 222, color(70, 70, 78));
        text_.draw(renderer_, std::to_string(building301Floor_) + "F " + buildingFloorName(), kPanelX + 22, 248, color(42, 46, 50));
        text_.draw(renderer_, building301Floor_ == 2 ? "BREADBOARD LAB" : (building301Floor_ == 3 ? "RANCH / RESEARCH" : "LOBBY / EXIT"), kPanelX + 22, 272, color(42, 46, 50));

        text_.draw(renderer_, "KEYS", kPanelX + 194, 302, color(70, 70, 78));
        std::array<std::string, 5> controls{
            "WASD WALK",
            "USE: Space/Enter",
            "FACE DOOR",
            "FACE STAIRS",
            "E ALT USE"
        };
        int controlY = 326;
        for (const auto& line : controls) {
            text_.draw(renderer_, line, kPanelX + 194, controlY, color(42, 46, 50));
            controlY += 22;
        }

        text_.draw(renderer_, "EVENTS", kPanelX + 22, 498, color(70, 70, 78));
        int logY = 522;
        int count = 0;
        for (const auto& line : log_.lines()) {
            if (count++ >= 6) break;
            text_.draw(renderer_, line, kPanelX + 22, logY, color(75, 58, 48));
            logY += 18;
        }
    }

    void renderBuilding301() {
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(192, 197, 192));
        renderHeader();
        SDL_Rect viewport{kGridX, kGridY, kViewWidth, kViewHeight};
        fillRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(67, 68, 75));
        drawRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(42, 43, 48));
        fillRect(renderer_, viewport, color(92, 96, 107));

        SDL_Rect room = building301BoundsRect();
        fillRect(renderer_, room, color(142, 136, 123));
        drawRect(renderer_, room, color(226, 221, 206));
        fillRect(renderer_, SDL_Rect{room.x, room.y, room.w, 30}, color(78, 80, 88));
        fillRect(renderer_, SDL_Rect{room.x, room.y + room.h - 30, room.w, 30}, color(78, 80, 88));
        fillRect(renderer_, SDL_Rect{room.x, room.y, 30, room.h}, color(78, 80, 88));
        fillRect(renderer_, SDL_Rect{room.x + room.w - 30, room.y, 30, room.h}, color(78, 80, 88));

        for (int x = room.x + 70; x < room.x + room.w - 70; x += 96) {
            drawLine(renderer_, x, room.y + 54, x, room.y + room.h - 58, color(122, 116, 102));
        }
        for (int y = room.y + 74; y < room.y + room.h - 70; y += 72) {
            drawLine(renderer_, room.x + 54, y, room.x + room.w - 54, y, color(122, 116, 102));
        }

        text_.draw(renderer_, "Building 301 " + std::to_string(building301Floor_) + "F", room.x + 38, room.y + 46, color(246, 241, 221), 2);
        text_.draw(renderer_, buildingFloorName(), room.x + 38, room.y + 88, color(238, 232, 212));

        auto drawDoor = [&](const SDL_Rect& rect, const std::string& label, SDL_Color fill) {
            fillRect(renderer_, rect, fill);
            drawRect(renderer_, rect, color(236, 231, 214));
            text_.draw(renderer_, label, rect.x + 14, rect.y + 19, color(244, 240, 220), 1);
        };

        if (building301Floor_ == 1) {
            SDL_Rect desk = building301DeskRect();
            fillRect(renderer_, desk, color(110, 86, 67));
            drawRect(renderer_, desk, color(70, 54, 43));
            text_.draw(renderer_, "LOBBY DESK", desk.x + 50, desk.y + 20, color(238, 231, 207));
            drawDoor(building301ExitDoorRect(), "EXIT", color(75, 72, 78));
            drawDoor(building301StairsUpRect(), "UP 2F", color(88, 83, 118));
        } else if (building301Floor_ == 2) {
            drawDoor(building301StairsDownRect(), "DOWN", color(88, 83, 118));
            drawDoor(building301StairsUpRect(), "UP 3F", color(88, 83, 118));
            drawDoor(breadboardLabDoorRect(), "BREADBOARD LAB", color(72, 125, 98));
        } else {
            drawDoor(building301StairsDownRect(), "DOWN", color(88, 83, 118));
            drawDoor(ranchLabDoorRect(), "RANCH LAB", color(112, 88, 135));
            drawDoor(researchLabDoorRect(), "RESEARCH LAB", color(91, 111, 144));
        }

        std::string prompt = "Face stairs or a door.";
        SDL_Rect facing = building301FacingRect();
        SDL_Rect exit = building301ExitDoorRect();
        SDL_Rect up = building301StairsUpRect();
        SDL_Rect down = building301StairsDownRect();
        SDL_Rect breadboard = breadboardLabDoorRect();
        SDL_Rect ranch = ranchLabDoorRect();
        SDL_Rect research = researchLabDoorRect();
        if (building301Floor_ == 1 && SDL_HasIntersection(&facing, &exit)) {
            drawRect(renderer_, exit, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: exit to campus", kGridX + 300, kGridY + 505, color(238, 232, 212), 1);
        } else if (building301Floor_ < 3 && SDL_HasIntersection(&facing, &up)) {
            drawRect(renderer_, up, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: go upstairs", kGridX + 318, kGridY + 505, color(238, 232, 212), 1);
        } else if (building301Floor_ > 1 && SDL_HasIntersection(&facing, &down)) {
            drawRect(renderer_, down, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: go downstairs", kGridX + 305, kGridY + 505, color(238, 232, 212), 1);
        } else if (building301Floor_ == 2 && SDL_HasIntersection(&facing, &breadboard)) {
            drawRect(renderer_, breadboard, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: enter Breadboard Farm Lab", kGridX + 230, kGridY + 505, color(238, 232, 212), 1);
        } else if (building301Floor_ == 3 && SDL_HasIntersection(&facing, &ranch)) {
            drawRect(renderer_, ranch, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: enter Grad Student Ranch", kGridX + 238, kGridY + 505, color(238, 232, 212), 1);
        } else if (building301Floor_ == 3 && SDL_HasIntersection(&facing, &research)) {
            drawRect(renderer_, research, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: check Research Lab", kGridX + 278, kGridY + 505, color(238, 232, 212), 1);
        } else {
            text_.draw(renderer_, prompt, kGridX + 330, kGridY + 505, color(238, 232, 212), 1);
        }

        renderBuilding301Player();
        renderFloatingTexts();
        renderBuilding301Panel();
    }

    SDL_Rect deviceSlotRect(int index) const {
        int row = index / 3;
        int col = index % 3;
        return SDL_Rect{kGridX + 86 + col * 112, kGridY + 164 + row * 86, 92, 62};
    }

    SDL_Rect researcherSlotRect(int index) const {
        int row = index / 3;
        int col = index % 3;
        return SDL_Rect{kGridX + 86 + col * 142, kGridY + 176 + row * 104, 118, 72};
    }

    void renderLabShell(const std::string& title, const std::string& subtitle) {
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(190, 197, 196));
        renderHeader();
        SDL_Rect viewport{kGridX, kGridY, kViewWidth, kViewHeight};
        fillRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(62, 72, 76));
        drawRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(42, 48, 51));
        fillRect(renderer_, viewport, color(71, 82, 87));
        SDL_Rect room = labBoundsRect();
        fillRect(renderer_, room, color(102, 113, 119));
        drawRect(renderer_, room, color(220, 225, 218));
        text_.draw(renderer_, title, room.x + 28, room.y + 28, color(245, 241, 220), 2);
        text_.draw(renderer_, subtitle, room.x + 28, room.y + 70, color(226, 232, 218));
        SDL_Rect exit = labExitDoorRect();
        fillRect(renderer_, exit, color(78, 72, 76));
        drawRect(renderer_, exit, color(236, 231, 214));
        text_.draw(renderer_, "EXIT", exit.x + 38, exit.y + 16, color(244, 240, 220), 1);
        SDL_Rect facing = labFacingRect();
        if (SDL_HasIntersection(&facing, &exit)) {
            drawRect(renderer_, exit, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: return to Building 301", kGridX + 252, kGridY + 505, color(238, 232, 212), 1);
        }
    }

    void renderLabSidePanel(const std::string& title, const std::vector<std::string>& lines) {
        fillRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(224, 229, 221));
        drawRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(78, 86, 86));
        text_.draw(renderer_, title, kPanelX + 22, 138, color(54, 66, 64), 1);
        text_.draw(renderer_, "MONEY $" + std::to_string(player_.money().value()), kPanelX + 22, 172, color(39, 53, 51));
        text_.draw(renderer_, "RESEARCH " + std::to_string(player_.researchPoints()) + " RP", kPanelX + 22, 196, color(39, 53, 51));
        int y = 238;
        for (const auto& line : lines) {
            text_.draw(renderer_, line, kPanelX + 22, y, color(39, 53, 51));
            y += 24;
        }
        text_.draw(renderer_, "EVENTS", kPanelX + 22, 498, color(54, 66, 64));
        int logY = 522;
        int count = 0;
        for (const auto& line : log_.lines()) {
            if (count++ >= 6) break;
            text_.draw(renderer_, line, kPanelX + 22, logY, color(75, 58, 48));
            logY += 18;
        }
    }

    void renderBreadboardLab() {
        renderLabShell("Breadboard Farm Lab", "Plant electronic device crops on the lab grid.");
        int activeDevices = activeDeviceSlotCount();
        for (int i = 0; i < activeDevices; ++i) {
            SDL_Rect slotRect = deviceSlotRect(i);
            const DeviceSlot& slot = deviceSlots_[i];
            SDL_Color fill = color(48, 59, 65);
            if (slot.occupied) fill = slot.ready ? color(67, 156, 94) : color(65, 107, 160);
            fillRect(renderer_, slotRect, fill);
            drawRect(renderer_, slotRect, i == selectedDeviceSlot_ ? color(250, 216, 76) : color(205, 214, 206));
            if (slot.occupied) {
                const DeviceInfo& info = deviceInfo(slot.type);
                text_.draw(renderer_, info.name, slotRect.x + 6, slotRect.y + 14, color(244, 241, 221));
                if (slot.ready) {
                    text_.draw(renderer_, "READY", slotRect.x + 22, slotRect.y + 38, color(250, 230, 120));
                } else {
                    int pct = static_cast<int>((slot.growth / info.growSeconds) * 100.0f);
                    text_.draw(renderer_, std::to_string(std::clamp(pct, 0, 99)) + "%", slotRect.x + 34, slotRect.y + 38, color(232, 236, 224));
                }
            } else {
                text_.draw(renderer_, "EMPTY", slotRect.x + 25, slotRect.y + 24, color(205, 214, 206));
            }
        }

        int catalogY = kGridY + 150;
        for (int i = 0; i < static_cast<int>(kDeviceCatalog.size()); ++i) {
            const DeviceInfo& info = kDeviceCatalog[i];
            SDL_Rect row{kGridX + 470, catalogY + i * 58, 276, 46};
            bool unlocked = deviceUnlocked(info);
            fillRect(renderer_, row, i == selectedDevice_ ? color(88, 125, 157) : (unlocked ? color(52, 66, 74) : color(66, 66, 68)));
            drawRect(renderer_, row, color(188, 204, 206));
            text_.draw(renderer_, std::to_string(i + 1) + " " + info.name + " $" + std::to_string(info.cost), row.x + 10, row.y + 7, color(240, 241, 222));
            if (unlocked) {
                int fail = static_cast<int>(std::round(effectiveDeviceFailureChance(info) * 100.0f));
                text_.draw(renderer_, "+$" + std::to_string(info.sellValue) + " +" + std::to_string(info.researchReward) + "RP  FAIL " + std::to_string(fail) + "%", row.x + 10, row.y + 26, color(205, 216, 205));
            } else {
                text_.draw(renderer_, deviceUnlockReason(info), row.x + 10, row.y + 26, color(228, 176, 154));
            }
        }

        renderLabPlayer();
        renderFloatingTexts();
        renderLabSidePanel("BREADBOARD LAB", {"1-5 SELECT DEVICE", "TAB/ARROWS SLOT", "USE PLANT/HARVEST", "ACTIVE SLOTS " + std::to_string(activeDevices), "FACE EXIT TO LEAVE"});
    }

    void renderRanchLab() {
        renderLabShell("Grad Student Ranch Lab", "Hire researchers to generate research points.");
        int activeResearchers = activeResearcherSlotCount();
        for (int i = 0; i < activeResearchers; ++i) {
            SDL_Rect slotRect = researcherSlotRect(i);
            const ResearcherSlot& slot = researcherSlots_[i];
            fillRect(renderer_, slotRect, slot.occupied ? color(117, 88, 150) : color(55, 60, 70));
            drawRect(renderer_, slotRect, i == selectedResearcherSlot_ ? color(250, 216, 76) : color(205, 214, 206));
            if (slot.occupied) {
                const ResearcherInfo& info = researcherInfo(slot.type);
                text_.draw(renderer_, info.name, slotRect.x + 6, slotRect.y + 18, color(244, 241, 221));
                if (slot.burnoutTimer > 0.0f) {
                    text_.draw(renderer_, "BURNOUT " + std::to_string(static_cast<int>(std::ceil(slot.burnoutTimer))) + "s", slotRect.x + 12, slotRect.y + 42, color(246, 202, 119));
                } else {
                    text_.draw(renderer_, "+" + std::to_string(researcherProductivity(slot)) + " RP/TICK", slotRect.x + 12, slotRect.y + 42, color(220, 229, 214));
                }
            } else {
                text_.draw(renderer_, "EMPTY DESK", slotRect.x + 20, slotRect.y + 28, color(205, 214, 206));
            }
        }

        int catalogY = kGridY + 150;
        for (int i = 0; i < static_cast<int>(kResearcherCatalog.size()); ++i) {
            const ResearcherInfo& info = kResearcherCatalog[i];
            SDL_Rect row{kGridX + 532, catalogY + i * 70, 258, 54};
            fillRect(renderer_, row, i == selectedResearcher_ ? color(126, 100, 160) : color(58, 60, 82));
            drawRect(renderer_, row, color(211, 205, 224));
            text_.draw(renderer_, std::to_string(i + 1) + " " + info.name, row.x + 10, row.y + 8, color(240, 241, 222));
            text_.draw(renderer_, "$" + std::to_string(info.cost) + "  LV" + std::to_string(info.unlockLevel) + "  +" + std::to_string(info.researchPerTick) + "RP", row.x + 10, row.y + 30, color(216, 220, 211));
        }

        renderLabPlayer();
        renderFloatingTexts();
        renderLabSidePanel("RANCH LAB", {"1-4 SELECT HIRE", "TAB SELECT DESK", "USE HIRE", "ACTIVE DESKS " + std::to_string(activeResearchers), "RANDOM EVENTS ENABLED"});
    }

    void renderResearchMenuOverlay() {
        SDL_Rect modal{kGridX + 76, kGridY + 82, 668, 370};
        fillRect(renderer_, modal, color(232, 235, 220, 244));
        drawRect(renderer_, modal, color(53, 66, 68));
        std::string title = researchPanel_ == ResearchPanel::Construction ? "FACILITY EXPANSION" : "RESEARCH UPGRADES";
        text_.draw(renderer_, title, modal.x + 26, modal.y + 24, color(45, 56, 58), 2);
        text_.draw(renderer_, "C construction  |  U/TAB upgrades  |  ESC close", modal.x + 28, modal.y + 66, color(72, 83, 82));

        struct Row {
            std::string title;
            std::string detail;
            bool bought;
        };

        std::vector<Row> rows;
        if (researchPanel_ == ResearchPanel::Construction) {
            rows = {
                {"1 Extra Breadboard Station  $140  20RP", "+2 device crop slots", extraBreadboardStationBuilt_},
                {"2 Researcher Desk           $120  18RP", "+1 researcher slot", researcherDeskBuilt_},
                {"3 Auto Experiment Lab       $180  35RP", "+4 RP every 18 seconds", autoExperimentLabBuilt_},
                {"4 Lounge                    $150  30RP", "reduces burnout/leaving risk", loungeBuilt_}
            };
        } else {
            rows = {
                {"1 Stabilized Circuit Design $160  45RP", "lower device failure chance", stabilizedCircuitDesignBuilt_},
                {"2 Productivity Program      $170  50RP", "researchers generate more RP", researcherProductivityBuilt_},
                {"3 Semiconductor License     $260  80RP", "unlocks MOSFET and FinFET", advancedSemiconductorLicenseBuilt_},
                {"4 Device Slot Expansion     $170  45RP", "+1 device crop slot", deviceSlotExpansionBuilt_},
                {"5 Researcher Slot Expansion $150  45RP", "+1 researcher slot", researcherSlotExpansionBuilt_}
            };
        }

        int y = modal.y + 112;
        for (const Row& row : rows) {
            SDL_Rect line{modal.x + 26, y - 6, modal.w - 52, 46};
            fillRect(renderer_, line, row.bought ? color(183, 207, 178) : color(209, 219, 205));
            drawRect(renderer_, line, color(91, 105, 98));
            text_.draw(renderer_, row.title, line.x + 12, line.y + 7, color(45, 56, 58));
            text_.draw(renderer_, row.bought ? "BUILT" : row.detail, line.x + 374, line.y + 7, row.bought ? color(42, 98, 55) : color(66, 76, 74));
            y += 52;
        }
    }

    void renderResearchLab() {
        renderLabShell("Research Lab", "Build facilities, buy upgrades, or debug faulty circuits.");
        SDL_Rect terminal = researchTerminalRect();
        SDL_Rect debug = debugComputerRect();

        fillRect(renderer_, terminal, color(46, 58, 70));
        drawRect(renderer_, terminal, color(156, 188, 196));
        text_.draw(renderer_, "RESEARCH TERMINAL", terminal.x + 32, terminal.y + 22, color(235, 242, 224), 1);
        text_.draw(renderer_, "BUILD / UPGRADE", terminal.x + 48, terminal.y + 58, color(180, 226, 190), 1);
        text_.draw(renderer_, "$" + std::to_string(player_.money().value()) + "  RP " + std::to_string(player_.researchPoints()), terminal.x + 58, terminal.y + 88, color(215, 222, 210));

        fillRect(renderer_, debug, color(52, 66, 84));
        drawRect(renderer_, debug, color(166, 194, 212));
        text_.draw(renderer_, "DEBUGGING COMPUTER", debug.x + 28, debug.y + 22, color(235, 242, 224), 1);
        text_.draw(renderer_, "BUG FINDING", debug.x + 72, debug.y + 58, color(184, 221, 226), 1);
        text_.draw(renderer_, "+RP if correct", debug.x + 72, debug.y + 88, color(215, 222, 210));

        SDL_Rect facing = labFacingRect();
        if (SDL_HasIntersection(&facing, &terminal)) {
            drawRect(renderer_, terminal, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: open construction menu", kGridX + 250, kGridY + 505, color(238, 232, 212), 1);
        } else if (SDL_HasIntersection(&facing, &debug)) {
            drawRect(renderer_, debug, color(250, 216, 76));
            text_.draw(renderer_, "Space/Enter: start Bug Finding", kGridX + 282, kGridY + 505, color(238, 232, 212), 1);
        }

        renderLabPlayer();
        renderFloatingTexts();
        if (researchMenuOpen_) renderResearchMenuOverlay();
        renderLabSidePanel("RESEARCH LAB", {"RP BUYS UPGRADES", "FACE TERMINAL: MENU", "FACE COMPUTER: DEBUG", "C/U SWITCH MENU", "1-5 BUY OPTION", "ESC CLOSE / EXIT"});
    }

    void renderLocationPlaceholder() {
        if (mode_ == Mode::Debugging) {
            debugging_.render(renderer_, text_);
            return;
        }
        if (location_ == Location::Campus) {
            renderCampus();
            return;
        }
        if (location_ == Location::Building301) {
            renderBuilding301();
            return;
        }
        if (location_ == Location::BreadboardFarmLab) {
            renderBreadboardLab();
            return;
        }
        if (location_ == Location::GradStudentRanchLab) {
            renderRanchLab();
            return;
        }
        if (location_ == Location::ResearchLab) {
            renderResearchLab();
            return;
        }
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(42, 55, 61));
        fillRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, kViewHeight}, color(77, 93, 101));
        drawRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, kViewHeight}, color(201, 216, 212));
        renderHeader();
        text_.draw(renderer_, locationName(location_), kGridX + 44, kGridY + 48, color(246, 241, 221), 2);
        text_.draw(renderer_, "Campus expansion placeholder", kGridX + 44, kGridY + 94, color(226, 233, 221));
        text_.draw(renderer_, "ESC returns to Farm", kGridX + 44, kGridY + 124, color(226, 233, 221));
        renderFloatingTexts();
    }

    void renderHeader() {
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, 88}, color(48, 77, 75));
        text_.draw(renderer_, "FARM VILLAGE SIMULATOR", 30, 22, color(246, 241, 221), 2);

        std::ostringstream stats;
        stats << "DAY " << day_ << "  " << hourLabel() << "  " << weatherName(events_.weather())
              << "  $" << player_.money().value()
              << "  RP " << player_.researchPoints()
              << "  LV " << player_.level() << " " << player_.xp() << "/" << player_.nextLevelXp();
        text_.draw(renderer_, stats.str(), 420, 28, color(246, 241, 221), 1);

        SDL_Rect staminaBack{420, 58, 310, 15};
        fillRect(renderer_, staminaBack, color(40, 48, 46));
        float ratio = static_cast<float>(player_.stamina()) / static_cast<float>(std::max(1, player_.maxStamina()));
        SDL_Rect staminaFill{staminaBack.x + 2, staminaBack.y + 2, static_cast<int>((staminaBack.w - 4) * ratio), staminaBack.h - 4};
        SDL_Color staminaColor = ratio < 0.25f ? color(218, 91, 75) : (ratio < 0.55f ? color(233, 177, 72) : color(99, 190, 103));
        fillRect(renderer_, staminaFill, staminaColor);
        drawRect(renderer_, staminaBack, color(238, 226, 187));
        std::ostringstream staminaLabel;
        staminaLabel << "STA " << player_.stamina() << "/" << player_.maxStamina();
        text_.draw(renderer_, staminaLabel.str(), staminaBack.x + staminaBack.w + 12, staminaBack.y - 4, color(246, 241, 221));
    }

    std::string hourLabel() const {
        int hour = static_cast<int>(clock_);
        int minute = static_cast<int>((clock_ - hour) * 60.0f);
        std::ostringstream out;
        out << (hour < 10 ? "0" : "") << hour << ":" << (minute < 10 ? "0" : "") << minute;
        return out.str();
    }

    void renderTileBase(int x, int y) {
        SDL_Rect tileRect = tileScreenRect(x, y);
        const Tile& tile = world_.at(x, y);
        SDL_Color base = color(121, 162, 91);
        switch (tile.kind) {
            case TileKind::Grass: base = color(121, 162, 91); break;
            case TileKind::Soil: base = color(128, 86, 47); break;
            case TileKind::Path: base = color(181, 151, 97); break;
            case TileKind::Pond: base = color(73, 145, 176); break;
            case TileKind::BuildingArea: base = color(151, 130, 102); break;
            case TileKind::IndoorFloor: base = color(171, 126, 82); break;
        }
        float left = static_cast<float>(tileRect.x);
        float top = static_cast<float>(tileRect.y + 7);
        float right = static_cast<float>(tileRect.x + tileRect.w);
        float bottom = static_cast<float>(tileRect.y + 43);

        std::array<SDL_FPoint, 4> topFace{
            SDL_FPoint{left + 5.0f, top + 6.0f},
            SDL_FPoint{right - 5.0f, top},
            SDL_FPoint{right - 1.0f, bottom - 5.0f},
            SDL_FPoint{left + 1.0f, bottom}
        };
        std::array<SDL_FPoint, 4> frontFace{
            topFace[3],
            topFace[2],
            SDL_FPoint{topFace[2].x, topFace[2].y + kTileFaceDepth},
            SDL_FPoint{topFace[3].x, topFace[3].y + kTileFaceDepth}
        };

        fillQuad(renderer_, frontFace, shade(base, -42));
        fillQuad(renderer_, topFace, base);

        drawLine(renderer_, static_cast<int>(topFace[0].x), static_cast<int>(topFace[0].y), static_cast<int>(topFace[1].x), static_cast<int>(topFace[1].y), shade(base, -56));
        drawLine(renderer_, static_cast<int>(topFace[1].x), static_cast<int>(topFace[1].y), static_cast<int>(topFace[2].x), static_cast<int>(topFace[2].y), shade(base, -56));
        drawLine(renderer_, static_cast<int>(topFace[2].x), static_cast<int>(topFace[2].y), static_cast<int>(topFace[3].x), static_cast<int>(topFace[3].y), shade(base, -70));
        drawLine(renderer_, static_cast<int>(topFace[3].x), static_cast<int>(topFace[3].y), static_cast<int>(topFace[0].x), static_cast<int>(topFace[0].y), shade(base, -56));

        int seed = (x * 73 + y * 41) % 19;
        if (tile.kind == TileKind::Grass) {
            drawLine(renderer_, tileRect.x + 12 + seed % 8, tileRect.y + 23, tileRect.x + 11 + seed % 8, tileRect.y + 17, color(72, 131, 71));
            drawLine(renderer_, tileRect.x + 34 + seed % 7, tileRect.y + 38, tileRect.x + 35 + seed % 7, tileRect.y + 31, color(91, 145, 76));
            if ((x + y) % 5 == 0) {
                fillRect(renderer_, SDL_Rect{tileRect.x + 40, tileRect.y + 25, 4, 4}, color(235, 213, 103));
            }
        } else if (tile.kind == TileKind::Soil) {
            drawLine(renderer_, tileRect.x + 8, tileRect.y + 25, tileRect.x + 46, tileRect.y + 20, color(94, 59, 37));
            drawLine(renderer_, tileRect.x + 9, tileRect.y + 38, tileRect.x + 47, tileRect.y + 33, color(94, 59, 37));
        } else if (tile.kind == TileKind::Path) {
            fillRect(renderer_, SDL_Rect{tileRect.x + 15 + seed % 18, tileRect.y + 26, 5, 3}, color(137, 112, 76));
            fillRect(renderer_, SDL_Rect{tileRect.x + 30, tileRect.y + 38, 4, 3}, color(214, 183, 126));
        }

        if (tile.kind == TileKind::Pond) {
            drawLine(renderer_, tileRect.x + 9, tileRect.y + 27, tileRect.x + 45, tileRect.y + 20, color(151, 205, 219));
            drawLine(renderer_, tileRect.x + 11, tileRect.y + 39, tileRect.x + 42, tileRect.y + 33, color(151, 205, 219));
            SDL_Color bank = color(107, 142, 81);
            if (!world_.isPond(x, y - 1)) drawLine(renderer_, tileRect.x + 6, tileRect.y + 14, tileRect.x + 49, tileRect.y + 9, bank);
            if (!world_.isPond(x, y + 1)) drawLine(renderer_, tileRect.x + 4, tileRect.y + 45, tileRect.x + 52, tileRect.y + 39, bank);
            if (!world_.isPond(x - 1, y)) drawLine(renderer_, tileRect.x + 4, tileRect.y + 15, tileRect.x + 2, tileRect.y + 43, bank);
            if (!world_.isPond(x + 1, y)) drawLine(renderer_, tileRect.x + 52, tileRect.y + 9, tileRect.x + 55, tileRect.y + 38, bank);
        }
    }

    void renderTargetMarker() {
        auto [x, y] = facingTile();
        SDL_Rect tileRect = tileScreenRect(x, y);
        SDL_Color c = color(250, 216, 76);
        SDL_Point p0{tileRect.x + 5, tileRect.y + 13};
        SDL_Point p1{tileRect.x + tileRect.w - 5, tileRect.y + 7};
        SDL_Point p2{tileRect.x + tileRect.w - 1, tileRect.y + 38};
        SDL_Point p3{tileRect.x + 1, tileRect.y + 43};
        std::array<SDL_FPoint, 4> face{
            SDL_FPoint{static_cast<float>(p0.x), static_cast<float>(p0.y)},
            SDL_FPoint{static_cast<float>(p1.x), static_cast<float>(p1.y)},
            SDL_FPoint{static_cast<float>(p2.x), static_cast<float>(p2.y)},
            SDL_FPoint{static_cast<float>(p3.x), static_cast<float>(p3.y)}
        };
        fillQuad(renderer_, face, color(250, 216, 76, 42));
        for (int offset = -1; offset <= 1; ++offset) {
            drawLine(renderer_, p0.x, p0.y + offset, p1.x, p1.y + offset, c);
            drawLine(renderer_, p1.x + offset, p1.y, p2.x + offset, p2.y, c);
            drawLine(renderer_, p2.x, p2.y + offset, p3.x, p3.y + offset, c);
            drawLine(renderer_, p3.x + offset, p3.y, p0.x + offset, p0.y, c);
        }
        drawLine(renderer_, p0.x + 2, p0.y + 2, p1.x - 2, p1.y + 2, c);
        drawLine(renderer_, p2.x - 2, p2.y - 2, p3.x + 2, p3.y - 2, c);
    }

    void renderEquippedTool(SDL_Point base) {
        int side = facing_ == Direction::Left ? -1 : 1;
        int handX = base.x + side * 14;
        int handY = base.y - 34;
        switch (currentAction_) {
            case Action::Hoe:
                drawLine(renderer_, handX, handY, handX + side * 24, handY - 26, color(108, 75, 45));
                drawLine(renderer_, handX + side, handY, handX + side * 25, handY - 26, color(108, 75, 45));
                fillRect(renderer_, SDL_Rect{handX + side * 20 - (side < 0 ? 20 : 0), handY - 30, 22, 5}, color(165, 172, 169));
                break;
            case Action::Seed:
                fillRect(renderer_, SDL_Rect{handX + side * 2 - (side < 0 ? 12 : 0), handY - 6, 12, 14}, color(186, 128, 75));
                fillRect(renderer_, SDL_Rect{handX + side * 6 - (side < 0 ? 3 : 0), handY + 2, 4, 4}, color(236, 207, 91));
                break;
            case Action::WateringCan:
                fillRect(renderer_, SDL_Rect{handX + side * 2 - (side < 0 ? 18 : 0), handY - 8, 18, 15}, color(88, 142, 178));
                drawLine(renderer_, handX + side * 18, handY - 7, handX + side * 30, handY - 13, color(47, 80, 104));
                fillRect(renderer_, SDL_Rect{handX + side * 31 - (side < 0 ? 3 : 0), handY - 8, 3, 4}, color(117, 183, 221));
                break;
            case Action::AxePick:
                drawLine(renderer_, handX, handY, handX + side * 24, handY - 28, color(116, 74, 43));
                drawLine(renderer_, handX + side, handY, handX + side * 25, handY - 28, color(116, 74, 43));
                fillRect(renderer_, SDL_Rect{handX + side * 19 - (side < 0 ? 20 : 0), handY - 34, 20, 10}, color(161, 169, 169));
                break;
            case Action::Hand:
                fillRect(renderer_, SDL_Rect{handX + side * 1 - (side < 0 ? 7 : 0), handY - 3, 7, 8}, color(232, 174, 122));
                break;
            case Action::Build:
                drawLine(renderer_, handX, handY, handX + side * 18, handY - 18, color(116, 74, 43));
                fillRect(renderer_, SDL_Rect{handX + side * 15 - (side < 0 ? 18 : 0), handY - 25, 18, 7}, color(153, 159, 157));
                break;
        }
    }

    void renderFarmer() {
        SDL_Point base = playerBasePoint();
        int bob = walking_ ? static_cast<int>(std::sin(walkTime_ * 13.0f) * 2.0f) : 0;

        fillRect(renderer_, SDL_Rect{base.x - 16, base.y - 6, 32, 7}, color(72, 78, 65, 120));

        SDL_Rect leftLeg{base.x - 10, base.y - 20 + bob, 8, 18 - bob};
        SDL_Rect rightLeg{base.x + 2, base.y - 20 - bob, 8, 18 + bob};
        fillRect(renderer_, leftLeg, color(48, 89, 130));
        fillRect(renderer_, rightLeg, color(48, 89, 130));
        fillRect(renderer_, SDL_Rect{leftLeg.x - 2, base.y - 4, 12, 5}, color(73, 48, 38));
        fillRect(renderer_, SDL_Rect{rightLeg.x - 1, base.y - 4, 12, 5}, color(73, 48, 38));

        SDL_Rect shirt{base.x - 14, base.y - 43, 28, 25};
        fillRect(renderer_, shirt, color(188, 81, 73));
        fillRect(renderer_, SDL_Rect{shirt.x + 5, shirt.y + 4, 18, 24}, color(62, 117, 156));
        drawRect(renderer_, shirt, color(85, 56, 52));
        renderEquippedTool(base);

        SDL_Rect head{base.x - 10, base.y - 61, 20, 19};
        fillRect(renderer_, head, color(226, 169, 118));
        drawRect(renderer_, head, color(95, 64, 49));
        fillRect(renderer_, SDL_Rect{base.x - 14, base.y - 65, 28, 6}, color(116, 83, 47));
        fillRect(renderer_, SDL_Rect{base.x - 10, base.y - 73, 20, 10}, color(157, 103, 55));
        drawRect(renderer_, SDL_Rect{base.x - 10, base.y - 73, 20, 10}, color(90, 62, 39));

        if (facing_ == Direction::Left) {
            fillRect(renderer_, SDL_Rect{head.x + 3, head.y + 8, 3, 3}, color(49, 44, 42));
        } else if (facing_ == Direction::Right) {
            fillRect(renderer_, SDL_Rect{head.x + head.w - 6, head.y + 8, 3, 3}, color(49, 44, 42));
        } else if (facing_ == Direction::Down) {
            fillRect(renderer_, SDL_Rect{head.x + 5, head.y + 8, 3, 3}, color(49, 44, 42));
            fillRect(renderer_, SDL_Rect{head.x + 12, head.y + 8, 3, 3}, color(49, 44, 42));
        } else {
            fillRect(renderer_, SDL_Rect{head.x, head.y, head.w, 6}, color(92, 63, 42));
        }
    }

    void renderWorld() {
        SDL_Rect viewport{kGridX, kGridY, kViewWidth, kViewHeight};
        fillRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(82, 103, 75));
        drawRect(renderer_, SDL_Rect{kGridX - 6, kGridY - 6, kViewWidth + 12, kViewHeight + 12}, color(48, 69, 52));
        SDL_RenderSetClipRect(renderer_, &viewport);
        fillRect(renderer_, viewport, color(111, 143, 88));

        auto [minX, maxX, minY, maxY] = visibleTileRange();
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                renderTileBase(x, y);
            }
        }

        for (const TileEffect& effect : tileEffects_) {
            SDL_Rect tile = tileScreenRect(effect.x, effect.y);
            SDL_Rect pulse{tile.x + 9, tile.y + 17, tile.w - 18, 24};
            fillRect(renderer_, pulse, effect.color);
        }

        renderTargetMarker();

        struct DrawItem {
            float depth{0.0f};
            bool player{false};
            int x{0};
            int y{0};
        };
        std::vector<DrawItem> drawItems;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (!world_.at(x, y).entity) continue;
                SDL_Rect tileRect = tileScreenRect(x, y);
                drawItems.push_back(DrawItem{static_cast<float>(tileRect.y + 52), false, x, y});
            }
        }
        SDL_Point base = playerBasePoint();
        drawItems.push_back(DrawItem{static_cast<float>(base.y), true, 0, 0});

        std::stable_sort(drawItems.begin(), drawItems.end(), [](const DrawItem& a, const DrawItem& b) {
            return a.depth < b.depth;
        });

        for (const DrawItem& item : drawItems) {
            if (item.player) {
                renderFarmer();
            } else {
                SDL_Rect entityRect = tileScreenRect(item.x, item.y);
                entityRect.y += 4;
                world_.at(item.x, item.y).entity->render(renderer_, entityRect);
            }
        }
        renderTargetMarker();
        SDL_RenderSetClipRect(renderer_, nullptr);
    }

    void renderWeatherOverlay() {
        Weather weather = events_.weather();
        if (weather == Weather::Rain || weather == Weather::Storm) {
            SDL_Color tint = weather == Weather::Storm ? color(22, 27, 38, 104) : color(45, 72, 94, 50);
            fillRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, kViewHeight}, tint);
            int offset = static_cast<int>((SDL_GetTicks() / 18) % 34);
            SDL_Color rain = weather == Weather::Storm ? color(182, 205, 232, 190) : color(153, 191, 218, 145);
            for (int i = -20; i < kViewWidth + 80; i += 34) {
                int x = kGridX + 20 + ((i * 17) % std::max(1, kViewWidth - 20));
                int y = kGridY + ((i + offset) % std::max(1, kViewHeight));
                drawLine(renderer_, x, y, x - 9, y + 23, rain);
            }
            if (weather == Weather::Storm && (SDL_GetTicks() / 380) % 9 == 0) {
                fillRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, kViewHeight}, color(238, 239, 210, 42));
            }
        } else if (weather == Weather::Cloudy) {
            fillRect(renderer_, SDL_Rect{kGridX, kGridY, kViewWidth, kViewHeight}, color(80, 88, 94, 28));
        }
    }

    void renderFloatingTexts() {
        for (const FloatingText& item : floatingTexts_) {
            text_.draw(renderer_, item.text, static_cast<int>(item.x), static_cast<int>(item.y), item.color, 0);
        }
    }

    void renderToolIcon(Action action, const SDL_Rect& slot) {
        int cx = slot.x + slot.w / 2;
        int cy = slot.y + 26;
        switch (action) {
            case Action::Hoe:
                for (int i = 0; i < 3; ++i) drawLine(renderer_, cx - 12 + i, cy + 16, cx + 12 + i, cy - 12, color(108, 75, 45));
                fillRect(renderer_, SDL_Rect{cx + 8, cy - 17, 25, 7}, color(165, 172, 169));
                fillRect(renderer_, SDL_Rect{cx + 27, cy - 17, 5, 18}, color(132, 139, 138));
                break;
            case Action::Seed:
                fillRect(renderer_, SDL_Rect{cx - 15, cy - 12, 30, 27}, color(186, 128, 75));
                fillRect(renderer_, SDL_Rect{cx - 12, cy - 17, 24, 9}, color(235, 214, 145));
                drawRect(renderer_, SDL_Rect{cx - 15, cy - 12, 30, 27}, color(91, 62, 42));
                fillRect(renderer_, SDL_Rect{cx - 3, cy - 1, 6, 6}, color(236, 207, 91));
                fillRect(renderer_, SDL_Rect{cx + 9, cy + 4, 5, 5}, color(236, 207, 91));
                break;
            case Action::WateringCan:
                fillRect(renderer_, SDL_Rect{cx - 18, cy - 8, 31, 24}, color(88, 142, 178));
                drawRect(renderer_, SDL_Rect{cx - 18, cy - 8, 31, 24}, color(47, 80, 104));
                drawLine(renderer_, cx + 12, cy - 5, cx + 28, cy - 13, color(47, 80, 104));
                drawLine(renderer_, cx + 13, cy - 2, cx + 30, cy - 8, color(47, 80, 104));
                drawRect(renderer_, SDL_Rect{cx - 24, cy - 5, 10, 16}, color(47, 80, 104));
                fillRect(renderer_, SDL_Rect{cx + 29, cy - 6, 3, 5}, color(117, 183, 221));
                fillRect(renderer_, SDL_Rect{cx + 34, cy - 2, 3, 5}, color(117, 183, 221));
                fillRect(renderer_, SDL_Rect{cx + 27, cy + 3, 3, 5}, color(117, 183, 221));
                break;
            case Action::AxePick:
                for (int i = 0; i < 3; ++i) drawLine(renderer_, cx - 14 + i, cy + 18, cx + 11 + i, cy - 16, color(116, 74, 43));
                fillRect(renderer_, SDL_Rect{cx + 5, cy - 20, 23, 12}, color(161, 169, 169));
                drawLine(renderer_, cx + 6, cy - 8, cx + 30, cy - 1, color(119, 128, 129));
                break;
            case Action::Hand:
                fillRect(renderer_, SDL_Rect{cx - 9, cy - 3, 18, 19}, color(224, 161, 109));
                for (int i = 0; i < 4; ++i) {
                    fillRect(renderer_, SDL_Rect{cx - 14 + i * 7, cy - 17, 5, 18}, color(232, 174, 122));
                }
                fillRect(renderer_, SDL_Rect{cx + 8, cy + 1, 10, 6}, color(232, 174, 122));
                drawRect(renderer_, SDL_Rect{cx - 9, cy - 3, 18, 19}, color(112, 75, 55));
                break;
            case Action::Build:
                if (buildBarn_) {
                    fillRect(renderer_, SDL_Rect{cx - 18, cy - 1, 36, 23}, color(181, 74, 63));
                    fillRect(renderer_, SDL_Rect{cx - 21, cy - 14, 42, 14}, color(103, 67, 57));
                    fillRect(renderer_, SDL_Rect{cx - 6, cy + 8, 12, 14}, color(84, 55, 48));
                } else {
                    fillRect(renderer_, SDL_Rect{cx - 21, cy - 1, 42, 6}, color(152, 102, 61));
                    fillRect(renderer_, SDL_Rect{cx - 21, cy + 12, 42, 6}, color(134, 89, 54));
                    fillRect(renderer_, SDL_Rect{cx - 14, cy - 10, 7, 33}, color(167, 116, 70));
                    fillRect(renderer_, SDL_Rect{cx + 9, cy - 10, 7, 33}, color(167, 116, 70));
                }
                break;
        }
    }

    void renderItemIcon(const std::string& item, const SDL_Rect& slot) {
        int cx = slot.x + slot.w / 2;
        int cy = slot.y + 26;
        if (item == "TURNIP" || item == "T.SEED") {
            fillRect(renderer_, SDL_Rect{cx - 11, cy - 1, 22, 18}, color(239, 188, 84));
            fillRect(renderer_, SDL_Rect{cx - 3, cy - 13, 6, 15}, color(64, 136, 73));
            fillRect(renderer_, SDL_Rect{cx - 15, cy - 10, 13, 8}, color(62, 151, 82));
            fillRect(renderer_, SDL_Rect{cx + 4, cy - 10, 13, 8}, color(62, 151, 82));
            drawRect(renderer_, SDL_Rect{cx - 11, cy - 1, 22, 18}, color(112, 81, 39));
        } else if (item == "POTATO" || item == "P.SEED") {
            fillRect(renderer_, SDL_Rect{cx - 13, cy - 3, 26, 19}, color(191, 134, 82));
            fillRect(renderer_, SDL_Rect{cx - 5, cy + 1, 4, 3}, color(126, 84, 56));
            fillRect(renderer_, SDL_Rect{cx + 7, cy + 8, 4, 3}, color(126, 84, 56));
            drawRect(renderer_, SDL_Rect{cx - 13, cy - 3, 26, 19}, color(112, 81, 39));
        }
    }

    void renderHotbar() {
        SDL_Rect bar{kGridX, kWindowHeight - 86, kViewWidth, 74};
        fillRect(renderer_, bar, color(75, 71, 63, 220));
        drawRect(renderer_, bar, color(42, 38, 34));

        struct Slot {
            std::string key;
            std::string label;
            int count;
        };
        std::array<Slot, 8> slots{{
            {"1", "HOE", 1},
            {"2", "PLANT", player_.inventory().count(seedName(selectedCrop_))},
            {"3", "WATER", 1},
            {"4", "AXE", 1},
            {"5", "HAND", 1},
            {"6", buildBarn_ ? "BARN" : "FENCE", 1},
            {"T", "T.SEED", player_.inventory().count("TURNIP SEED")},
            {"P", "P.SEED", player_.inventory().count("POTATO SEED")}
        }};

        for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
            SDL_Rect slot{bar.x + 12 + i * 98, bar.y + 9, 82, 56};
            bool selected = (i == 0 && currentAction_ == Action::Hoe)
                || (i == 1 && currentAction_ == Action::Seed)
                || (i == 2 && currentAction_ == Action::WateringCan)
                || (i == 3 && currentAction_ == Action::AxePick)
                || (i == 4 && currentAction_ == Action::Hand)
                || (i == 5 && currentAction_ == Action::Build)
                || (i == 6 && currentAction_ == Action::Seed && selectedCrop_ == CropType::Turnip)
                || (i == 7 && currentAction_ == Action::Seed && selectedCrop_ == CropType::Potato);
            fillRect(renderer_, slot, selected ? color(238, 207, 115) : color(230, 217, 184));
            drawRect(renderer_, slot, selected ? color(99, 74, 43) : color(93, 80, 64));
            text_.draw(renderer_, slots[i].key, slot.x + 5, slot.y + 3, color(68, 55, 45));
            if (i <= 5) {
                std::array<Action, 6> actions{Action::Hoe, Action::Seed, Action::WateringCan, Action::AxePick, Action::Hand, Action::Build};
                renderToolIcon(actions[i], slot);
            } else {
                renderItemIcon(slots[i].label, slot);
            }
            text_.draw(renderer_, slots[i].label, slot.x + 26, slot.y + 37, color(42, 51, 47));
            if (i == 1 || i >= 6) {
                text_.draw(renderer_, "x" + std::to_string(slots[i].count), slot.x + 54, slot.y + 3, color(68, 55, 45));
            }
        }
    }

    void renderShop() {
        SDL_Rect shadeRect{0, 0, kWindowWidth, kWindowHeight};
        fillRect(renderer_, shadeRect, color(23, 29, 31, 126));
        SDL_Rect modal{212, 86, 620, 556};
        fillRect(renderer_, modal, color(238, 226, 195));
        drawRect(renderer_, modal, color(72, 58, 48));
        text_.draw(renderer_, "MARKETPLACE", modal.x + 32, modal.y + 24, color(67, 54, 47), 2);

        std::array<std::string, 11> lines{{
            "1  BUY 3 TURNIP SEEDS  - $15",
            player_.potatoUnlocked() ? "2  BUY 3 POTATO SEEDS  - $24" : "2  POTATO SEEDS LOCKED UNTIL LEVEL 2",
            "3  BUY 3 FEED          - $9",
            "4  BUY CHICKEN         - $30",
            "5  SELL ALL GOODS",
            player_.cowUnlocked() ? "6  BUY COW             - $70" : "6  COWS LOCKED UNTIL LEVEL 5",
            "7  SELL TURNIPS ONLY",
            "8  SELL POTATOES ONLY",
            "9  SELL EGGS ONLY",
            "0  SELL MILK ONLY",
            "F  SELL FISH ONLY"
        }};

        int y = modal.y + 92;
        for (const auto& line : lines) {
            text_.draw(renderer_, line, modal.x + 46, y, color(48, 55, 51), 1);
            y += 34;
        }

        text_.draw(renderer_, "Animals are delivered to the fenced pen, away from the market.", modal.x + 46, modal.y + 494, color(97, 75, 62));
        text_.draw(renderer_, "ESC closes", modal.x + 46, modal.y + 522, color(97, 75, 62));
    }

    void renderHelpOverlay() {
        fillRect(renderer_, SDL_Rect{0, 0, kWindowWidth, kWindowHeight}, color(21, 27, 29, 138));
        SDL_Rect modal{154, 72, 736, 594};
        fillRect(renderer_, modal, color(238, 229, 202));
        drawRect(renderer_, modal, color(71, 62, 53));
        text_.draw(renderer_, "HELP / OBJECTIVE", modal.x + 34, modal.y + 26, color(61, 53, 47), 2);

        std::array<std::string, 14> lines{{
            "Goal: grow crops, raise animals, earn money, visit SNU, earn RP.",
            "Farm loop: 1 Hoe grass, 2 Plant Tool, T/P choose seed, 3 Water, 5 Harvest.",
            "Seed type and tool are separate: press 2 any time to return to Plant Tool.",
            "Market: face the keeper and Space/Enter. Buy supplies or sell by category.",
            "Animals: bought animals are delivered to the fenced pen, away from paths.",
            "Fences mark animal pens and help separate livestock from walking routes.",
            "Fishing: unlock at level 3, face water, press F, then Space/Enter on target.",
            "Memory: press M for the fair minigame, match pairs for gems.",
            "SNU: face the shuttle stop, Space/Enter, then enter Building 301.",
            "Building 301 connects farming to lab farming, researchers, and upgrades.",
            "Breadboard Lab: plant device crops; higher devices cost more and may fail.",
            "Ranch Lab: hire researchers to generate RP over time.",
            "Research Lab: RP buys lab upgrades, construction, and device unlocks.",
            "ESC closes menus/help. Q quits during normal gameplay."
        }};

        int y = modal.y + 92;
        for (const auto& line : lines) {
            text_.draw(renderer_, line, modal.x + 34, y, color(42, 54, 50));
            y += 32;
        }
        text_.draw(renderer_, "H or ESC closes", modal.x + 34, modal.y + modal.h - 44, color(92, 73, 60), 1);
    }

    void renderPanel() {
        fillRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(237, 230, 204));
        drawRect(renderer_, SDL_Rect{kPanelX, 116, kPanelWidth, 548}, color(92, 84, 74));

        text_.draw(renderer_, "TOOL: " + actionName(currentAction_), kPanelX + 22, 138, color(62, 58, 52), 1);
        text_.draw(renderer_, "SEED: " + seedName(selectedCrop_), kPanelX + 22, 166, color(62, 58, 52));
        text_.draw(renderer_, std::string("BUILD: ") + (buildBarn_ ? "BARN" : "FENCE"), kPanelX + 22, 190, color(62, 58, 52));

        auto [targetX, targetY] = facingTile();
        const Tile& tile = world_.at(targetX, targetY);
        std::string selected = tile.entity ? tile.entity->status() : tileKindName(tile.kind);
        text_.draw(renderer_, "FACING TILE", kPanelX + 22, 232, color(90, 80, 70));
        text_.draw(renderer_, selected, kPanelX + 22, 255, color(42, 57, 54), 1);

        text_.draw(renderer_, "SUPPLIES", kPanelX + 22, 302, color(90, 80, 70));
        int invY = 326;
        std::vector<std::string> items{"TURNIP SEED", "POTATO SEED", "TURNIP", "POTATO", "FEED", "EGG", "MILK", "FISH", "GEM"};
        for (const auto& item : items) {
            std::ostringstream line;
            line << item << " x" << player_.inventory().count(item);
            text_.draw(renderer_, line.str(), kPanelX + 22, invY, color(42, 57, 54));
            invY += 18;
        }

        text_.draw(renderer_, "KEYS", kPanelX + 194, 302, color(90, 80, 70));
        std::array<std::string, 9> controls{
            "WASD WALK",
            "1-6 TOOLS",
            "USE: Space/Enter",
            "2 PLANT, T/P SEED",
            "FACE NPC SHOP",
            "H HELP / ESC HELP",
            "C EAT",
            "F FISH",
            "M MEMORY"
        };
        int controlY = 326;
        for (const auto& line : controls) {
            text_.draw(renderer_, line, kPanelX + 194, controlY, color(42, 57, 54));
            controlY += 19;
        }

        text_.draw(renderer_, "EVENTS", kPanelX + 22, 498, color(90, 80, 70));
        int logY = 522;
        int count = 0;
        for (const auto& line : log_.lines()) {
            if (count++ >= 6) break;
            text_.draw(renderer_, line, kPanelX + 22, logY, color(80, 58, 51));
            logY += 18;
        }
    }

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    Text text_;
    bool running_{true};

    Player player_;
    World world_;
    EventLog log_;
    EventManager events_;
    std::mt19937 rng_{std::random_device{}()};

    Location location_{Location::Farm};
    Mode mode_{Mode::Farm};
    Action currentAction_{Action::Hoe};
    CropType selectedCrop_{CropType::Turnip};
    bool buildBarn_{false};
    Direction facing_{Direction::Down};
    float playerTileX_{6.0f};
    float playerTileY_{6.0f};
    float campusPlayerX_{kCampusSpawnX};
    float campusPlayerY_{kCampusSpawnY};
    float building301PlayerX_{kBuilding301SpawnX};
    float building301PlayerY_{kBuilding301SpawnY};
    float labPlayerX_{static_cast<float>(kGridX + 384)};
    float labPlayerY_{static_cast<float>(kGridY + 392)};
    float cameraX_{0.0f};
    float cameraY_{0.0f};
    float walkTime_{0.0f};
    bool walking_{false};
    int day_{1};
    float clock_{6.0f};
    bool rainNoticeShown_{false};
    int rainNoticeDay_{0};
    int building301Floor_{1};
    int labReturnFloor_{1};
    int selectedDevice_{0};
    int selectedDeviceSlot_{0};
    int selectedResearcher_{0};
    int selectedResearcherSlot_{0};
    float researcherTick_{0.0f};
    float researcherEventTick_{0.0f};
    float autoExperimentTick_{0.0f};
    bool researchMenuOpen_{false};
    ResearchPanel researchPanel_{ResearchPanel::Construction};
    bool extraBreadboardStationBuilt_{false};
    bool researcherDeskBuilt_{false};
    bool autoExperimentLabBuilt_{false};
    bool loungeBuilt_{false};
    bool stabilizedCircuitDesignBuilt_{false};
    bool researcherProductivityBuilt_{false};
    bool advancedSemiconductorLicenseBuilt_{false};
    bool deviceSlotExpansionBuilt_{false};
    bool researcherSlotExpansionBuilt_{false};
    int debugFailureShield_{0};

    FishingGame fishing_;
    MemoryGame memory_;
    DebuggingGame debugging_;
    std::array<DeviceSlot, 9> deviceSlots_{};
    std::array<ResearcherSlot, 6> researcherSlots_{};
    std::vector<FloatingText> floatingTexts_;
    std::vector<TileEffect> tileEffects_;
};

}  // namespace

int main(int argc, char** argv) {
    bool smokeTest = false;
    bool collisionTest = false;
    bool staminaTest = false;
    std::optional<unsigned int> mapSeed;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--smoke-test") smokeTest = true;
        else if (arg == "--collision-test") collisionTest = true;
        else if (arg == "--stamina-test") staminaTest = true;
        else if (arg.rfind("--map-seed=", 0) == 0) {
            mapSeed = static_cast<unsigned int>(std::stoul(arg.substr(11)));
        } else if (arg == "--map-seed" && i + 1 < argc) {
            mapSeed = static_cast<unsigned int>(std::stoul(argv[++i]));
        }
    }

    if (collisionTest) {
        return runCollisionTests() ? 0 : 1;
    }
    if (staminaTest) {
        return runStaminaTests() ? 0 : 1;
    }

    try {
        Game game(mapSeed);
        game.init();
        game.run(smokeTest);
    } catch (const std::exception& ex) {
        std::cerr << "Farm Village Demo failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
