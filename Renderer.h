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
    static constexpr float WIN_W = 1600.f;
    static constexpr float WIN_H = 900.f;

    // Top 4 panels: each 400 × 450
    static constexpr float TOP_H    = 450.f;
    static constexpr float TOP_W    = 400.f;

    // Bottom 10 trader panels: 5 columns × 2 rows, each 320 × 225
    static constexpr float BOT_H    = 225.f;
    static constexpr float BOT_W    = 320.f;
    static constexpr float BOT_Y    = 450.f;

    // ── Colors ────────────────────────────────────────────────────────────────
    static sf::Color COL_BG()       { return {15,  15,  20}; }
    static sf::Color COL_PANEL()    { return {25,  25,  35}; }
    static sf::Color COL_BORDER()   { return {80,  80, 110}; }
    static sf::Color COL_TEXT()     { return {255, 255, 255}; }
    static sf::Color COL_DIM()      { return {180, 180, 200}; }
    static sf::Color COL_BUY()      { return {80,  180, 255}; }
    static sf::Color COL_SELL()     { return {255, 180,  60}; }
    static sf::Color COL_GREEN()    { return {120, 255, 120}; }
    static sf::Color COL_YELLOW()   { return {255, 240, 100}; }
    static sf::Color COL_HEADER()   { return {220, 220, 255}; }

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
