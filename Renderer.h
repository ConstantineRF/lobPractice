#pragma once
#include <SFML/Graphics.hpp>
#include "Simulation.h"
#include <deque>
#include <string>

class Renderer {
public:
    explicit Renderer(sf::RenderWindow& window);

    bool loadAssets();   // load font; returns false on failure
    void render(const Simulation& sim);

private:
    sf::RenderWindow& window_;
    sf::Font          font_;

    // ── Window / panel dimensions ─────────────────────────────────────────────
    static constexpr float WIN_W = 1920.f;
    static constexpr float WIN_H = 1200.f;

    // Top 4 panels: height 600, widths vary
    static constexpr float TOP_H    = 600.f;
    static constexpr float TOP_W    = 480.f;  // Price chart & Holdings
    static constexpr float OB_W     = 552.f;  // Order book (wider)
    static constexpr float LOG_W    = 408.f;  // Exchange log (narrower)

    // Bottom 10 trader panels: 5 columns × 2 rows, each 384 × 300
    static constexpr float BOT_H    = 300.f;
    static constexpr float BOT_W    = 384.f;
    static constexpr float BOT_Y    = 600.f;

    // ── Colors ────────────────────────────────────────────────────────────────
    static sf::Color COL_BG()       { return {15,  15,  20}; }
    static sf::Color COL_PANEL()    { return {25,  25,  35}; }
    static sf::Color COL_BORDER()   { return {100, 100, 135}; }
    static sf::Color COL_TEXT()     { return {255, 255, 255}; }
    static sf::Color COL_DIM()      { return {210, 210, 228}; }
    static sf::Color COL_BUY()      { return {110, 210, 255}; }
    static sf::Color COL_SELL()     { return {255, 215, 100}; }
    static sf::Color COL_GREEN()    { return {160, 255, 160}; }
    static sf::Color COL_YELLOW()   { return {255, 255, 140}; }
    static sf::Color COL_HEADER()   { return {240, 240, 255}; }

    // ── Price chart state ─────────────────────────────────────────────────────
    struct PricePoint { SimTime time; Price bid; Price ask; };
    std::deque<PricePoint> price_history_;
    SimTime last_price_sample_ = -1.0;
    static constexpr int   PRICE_HIST_CAP = 300;  // 5 min at 1 sample/sec
    static constexpr float PRICE_WINDOW   = 300.f; // seconds shown

    // ── Drawing helpers ───────────────────────────────────────────────────────
    void drawRect(sf::Vector2f pos, sf::Vector2f size,
                  sf::Color fill,
                  sf::Color outline = sf::Color::Transparent,
                  float outline_t   = 0.f);
    void drawText(const std::string& str, sf::Vector2f pos,
                  unsigned int size, sf::Color color);
    void drawPanel(sf::Vector2f origin, sf::Vector2f size, const std::string& title);
    void drawLine(sf::Vector2f a, sf::Vector2f b, sf::Color color);

    // ── Panel drawing ─────────────────────────────────────────────────────────
    void drawPriceChart    (sf::Vector2f origin, const Simulation& sim);
    void drawOrderBook     (sf::Vector2f origin, const Simulation& sim);
    void drawMessageLog    (sf::Vector2f origin, const Simulation& sim);
    void drawHoldingsTable (sf::Vector2f origin, const Simulation& sim);
    void drawTraderPanel   (sf::Vector2f origin, sf::Vector2f size,
                            const Trader& trader, int idx, const Simulation& sim);
};
