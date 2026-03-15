#include "Renderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

Renderer::Renderer(sf::RenderWindow& window) : window_(window) {}

bool Renderer::loadAssets() {
    // Try bundled font first, then system Courier New
    if (font_.openFromFile("cour.ttf")) return true;
    if (font_.openFromFile("C:/Windows/Fonts/cour.ttf")) return true;
    return false;
}

// ── Drawing primitives ────────────────────────────────────────────────────────

void Renderer::drawRect(sf::Vector2f pos, sf::Vector2f size,
                        sf::Color fill, sf::Color outline, float outline_t) {
    sf::RectangleShape r(size);
    r.setPosition(pos);
    r.setFillColor(fill);
    if (outline_t > 0.f) {
        r.setOutlineColor(outline);
        r.setOutlineThickness(-outline_t);
    }
    window_.draw(r);
}

void Renderer::drawText(const std::string& str, sf::Vector2f pos,
                        unsigned int size, sf::Color color) {
    sf::Text t(font_, str, size);
    t.setPosition(pos);
    t.setFillColor(color);
    window_.draw(t);
}

void Renderer::drawPanel(sf::Vector2f origin, sf::Vector2f size, const std::string& title) {
    drawRect(origin, size, COL_PANEL(), COL_BORDER(), 1.f);
    drawText(title, {origin.x + 5.f, origin.y + 2.f}, 13, COL_HEADER());
}

void Renderer::drawLine(sf::Vector2f a, sf::Vector2f b, sf::Color color) {
    // SFML 3.0: sf::Vertex is a plain aggregate {position, color, texCoords}
    sf::Vertex line[2] = {
        sf::Vertex{a, color},
        sf::Vertex{b, color}
    };
    window_.draw(line, 2, sf::PrimitiveType::Lines);
}

// ── Main render entry ─────────────────────────────────────────────────────────

void Renderer::render(const Simulation& sim) {
    window_.clear(COL_BG());

    // ── Top 4 panels ─────────────────────────────────────────────────────────
    drawPriceChart   ({0.f,    0.f}, sim);
    drawOrderBook    ({480.f,  0.f}, sim);
    drawMessageLog   ({1032.f, 0.f}, sim);
    drawHoldingsTable({1440.f, 0.f}, sim);

    // ── Bottom trader panels (5 MM + 5 Investors, 2 rows × 5 cols) ───────────
    for (int i = 0; i < NUM_TRADERS; ++i) {
        int col  = i % 5;
        int row  = i / 5;
        float x  = col * BOT_W;
        float y  = BOT_Y + row * BOT_H;
        drawTraderPanel({x, y}, {BOT_W, BOT_H}, sim.getTrader(i), i, sim);
    }

    window_.display();
}

// ── Panel 1: 5-minute price chart ─────────────────────────────────────────────

void Renderer::drawPriceChart(sf::Vector2f origin, const Simulation& sim) {
    const float W = TOP_W, H = TOP_H;
    drawPanel(origin, {W, H}, "PRICE CHART (5 min)");

    SimTime now = sim.getSimTime();

    // Sample once per second
    const auto& book = sim.getExchange().getBook();
    if (book.hasBid() && book.hasAsk() && now - last_price_sample_ >= 1.0) {
        price_history_.push_back({now, book.bestBid(), book.bestAsk()});
        last_price_sample_ = now;
        while ((int)price_history_.size() > PRICE_HIST_CAP)
            price_history_.pop_front();
    }

    if (price_history_.size() < 2) {
        drawText("Waiting for data...", {origin.x + 10.f, origin.y + H / 2.f}, 15, COL_DIM());
        return;
    }

    // Compute price range for y-axis
    Price min_p = price_history_.front().bid;
    Price max_p = price_history_.front().ask;
    for (auto& pp : price_history_) {
        min_p = std::min(min_p, pp.bid);
        max_p = std::max(max_p, pp.ask);
    }
    if (min_p == max_p) { min_p -= 10; max_p += 10; }
    float price_range = (float)(max_p - min_p);

    // Chart area inside panel
    const float PAD_L = 60.f, PAD_R = 12.f, PAD_T = 26.f, PAD_B = 36.f;
    float cx = origin.x + PAD_L;
    float cy = origin.y + PAD_T;
    float cw = W - PAD_L - PAD_R;
    float ch = H - PAD_T - PAD_B;

    // Axis labels
    auto priceLabel = [&](Price p, float y) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", centsToDouble(p));
        drawText(buf, {origin.x + 2.f, y - 7.f}, 12, COL_DIM());
    };
    priceLabel(max_p, cy);
    priceLabel(min_p, cy + ch);
    priceLabel((max_p + min_p) / 2, cy + ch / 2.f);

    // Time axis: show last PRICE_WINDOW seconds
    SimTime t_end   = now;
    SimTime t_start = t_end - PRICE_WINDOW;

    auto toScreenX = [&](SimTime t) -> float {
        return cx + (float)((t - t_start) / PRICE_WINDOW) * cw;
    };
    auto toScreenY = [&](Price p) -> float {
        return cy + ch - (float)(p - min_p) / price_range * ch;
    };

    // Time axis: vertical gridlines every 10s, labels every 60s
    {
        const SimTime GRID_STEP = 10.0;
        SimTime first_grid = std::ceil(t_start / GRID_STEP) * GRID_STEP;
        // horizontal axis baseline
        drawLine({cx, cy + ch}, {cx + cw, cy + ch}, COL_BORDER());
        for (SimTime gt = first_grid; gt <= t_end + 0.1; gt += GRID_STEP) {
            float gx = toScreenX(gt);
            if (gx < cx || gx > cx + cw) continue;
            drawLine({gx, cy}, {gx, cy + ch}, sf::Color(45, 45, 62));
            int ti = (int)std::round(gt);
            if (ti % 60 == 0) {
                char tbuf[10];
                std::snprintf(tbuf, sizeof(tbuf), "%d:%02d", ti / 60, ti % 60);
                drawText(tbuf, {gx - 16.f, cy + ch + 7.f}, 11, COL_DIM());
            }
        }
    }

    // Draw bid line (blue) and ask line (orange)
    sf::VertexArray bid_line(sf::PrimitiveType::LineStrip);
    sf::VertexArray ask_line(sf::PrimitiveType::LineStrip);

    for (auto& pp : price_history_) {
        float x = toScreenX(pp.time);
        if (x < cx || x > cx + cw) continue;
        bid_line.append(sf::Vertex{sf::Vector2f{x, toScreenY(pp.bid)},  COL_BUY()});
        ask_line.append(sf::Vertex{sf::Vector2f{x, toScreenY(pp.ask)}, COL_SELL()});
    }
    if (bid_line.getVertexCount() > 1) window_.draw(bid_line);
    if (ask_line.getVertexCount() > 1) window_.draw(ask_line);

    // Current bid/ask labels
    if (book.hasBid())
        drawText("Bid " + [&]{ char b[12]; std::snprintf(b, 12, "%.2f", centsToDouble(book.bestBid())); return std::string(b); }(),
                 {cx + cw - 84.f, toScreenY(book.bestBid()) - 17.f}, 12, COL_BUY());
    if (book.hasAsk())
        drawText("Ask " + [&]{ char b[12]; std::snprintf(b, 12, "%.2f", centsToDouble(book.bestAsk())); return std::string(b); }(),
                 {cx + cw - 84.f, toScreenY(book.bestAsk()) + 2.f}, 12, COL_SELL());
}

// ── Panel 2: Order Book Visualization ─────────────────────────────────────────

void Renderer::drawOrderBook(sf::Vector2f origin, const Simulation& sim) {
    const float W = OB_W, H = TOP_H;
    drawPanel(origin, {W, H}, "ORDER BOOK — KostyaEx: XYZ");

    const auto& book = sim.getExchange().getBook();
    auto bid_levels = book.getBidLevels(12);
    auto ask_levels = book.getAskLevels(12);

    // Reverse ask levels so best ask is at center, others go upward
    // Layout: asks above center line, bids below
    const float PAD   = 22.f;   // top offset for title
    const float ROW_H = 21.f;
    const float MID_Y = origin.y + H / 2.f;
    const float CENTER_X = origin.x + W / 2.f;
    const float BAR_MAX_W = 190.f;  // max bar width per side (fits bid bar + qty label within panel)

    // Find max total qty for scaling
    int max_qty = 1;
    for (auto& lvl : bid_levels) for (auto& [tid, qty] : lvl.orders) max_qty = std::max(max_qty, qty);
    for (auto& lvl : ask_levels) for (auto& [tid, qty] : lvl.orders) max_qty = std::max(max_qty, qty);
    // Use level total
    int max_lvl_qty = 1;
    for (auto& lvl : bid_levels) {
        int t = 0; for (auto& [tid, qty] : lvl.orders) t += qty;
        max_lvl_qty = std::max(max_lvl_qty, t);
    }
    for (auto& lvl : ask_levels) {
        int t = 0; for (auto& [tid, qty] : lvl.orders) t += qty;
        max_lvl_qty = std::max(max_lvl_qty, t);
    }

    // Trader ID colors (10 traders, distinct hues)
    static const sf::Color TRADER_COLORS[10] = {
        {0,   180, 255},  // 1 bright blue
        {0,   255, 180},  // 2 teal
        {180, 255,   0},  // 3 lime
        {255, 220,   0},  // 4 gold
        {255, 100, 100},  // 5 salmon
        {200,   0, 255},  // 6 purple
        {255, 150,   0},  // 7 orange
        {0,   200, 100},  // 8 green
        {100, 150, 255},  // 9 periwinkle
        {255,  50, 150},  // 10 pink
    };

    auto drawLevel = [&](const OrderBook::Level& lvl, float y, bool is_bid) {
        int total_qty = 0;
        for (auto& [tid, qty] : lvl.orders) total_qty += qty;
        float bar_w = (float)total_qty / max_lvl_qty * BAR_MAX_W;

        // Price label
        char price_buf[16];
        std::snprintf(price_buf, sizeof(price_buf), "%.2f", centsToDouble(lvl.price));
        drawText(price_buf, {CENTER_X - 36.f, y + 1.f}, 12,
                 is_bid ? sf::Color(150, 200, 255) : sf::Color(255, 190, 100));

        // Draw subdivided bar
        float x_start = is_bid ? CENTER_X - 42.f - bar_w : CENTER_X + 42.f;
        float bx = x_start;
        for (auto& [tid, qty] : lvl.orders) {
            float seg_w = (float)qty / max_lvl_qty * BAR_MAX_W;
            sf::Color base_color = TRADER_COLORS[(tid - 1) % 10];
            // Blend toward buy/sell color
            sf::Color bar_color = is_bid
                ? sf::Color((base_color.r + COL_BUY().r) / 2,
                            (base_color.g + COL_BUY().g) / 2,
                            (base_color.b + COL_BUY().b) / 2)
                : sf::Color((base_color.r + COL_SELL().r) / 2,
                            (base_color.g + COL_SELL().g) / 2,
                            (base_color.b + COL_SELL().b) / 2);

            drawRect({bx, y}, {seg_w - 1.f, ROW_H - 2.f}, bar_color);
            // Trader ID label inside bar if wide enough
            if (seg_w > 17.f) {
                drawText(std::to_string(tid), {bx + 2.f, y + 2.f}, 11, sf::Color::White);
            }
            bx += seg_w;
        }
        // Total qty label
        char qty_buf[16];
        std::snprintf(qty_buf, sizeof(qty_buf), "%d", total_qty);
        if (is_bid)
            drawText(qty_buf, {CENTER_X - 42.f - bar_w - 34.f, y + 1.f}, 12, COL_DIM());
        else
            drawText(qty_buf, {CENTER_X + 42.f + bar_w + 2.f, y + 1.f}, 12, COL_DIM());
    };

    // Draw "SELLS" header above center, "BUYS" below
    drawText("ASKS", {CENTER_X - 15.f, origin.y + PAD}, 10, COL_SELL());
    drawText("BIDS", {CENTER_X - 15.f, MID_Y + 2.f}, 10, COL_BUY());

    // Ask levels: draw from center upward (best ask nearest center)
    for (int i = 0; i < (int)ask_levels.size() && i < 12; ++i) {
        float y = MID_Y - (i + 1) * ROW_H;
        if (y < origin.y + PAD + 12.f) break;
        drawLevel(ask_levels[i], y, false);
    }
    // Bid levels: draw from center downward (best bid nearest center)
    for (int i = 0; i < (int)bid_levels.size() && i < 12; ++i) {
        float y = MID_Y + i * ROW_H + 14.f;
        if (y + ROW_H > origin.y + H - 4.f) break;
        drawLevel(bid_levels[i], y, true);
    }

    // Center divider line
    drawLine({origin.x + 2.f, MID_Y + 12.f}, {origin.x + W - 2.f, MID_Y + 12.f},
             COL_BORDER());
}

// ── Panel 3: Exchange Message Log ─────────────────────────────────────────────

void Renderer::drawMessageLog(sf::Vector2f origin, const Simulation& sim) {
    const float W = LOG_W, H = TOP_H;
    drawPanel(origin, {W, H}, "EXCHANGE LOG (last 25)");

    const auto& log = sim.getGlobalLog();
    const float ROW_H = 22.f;
    const float PAD   = 16.f;
    float y = origin.y + PAD;

    for (auto it = log.begin(); it != log.end(); ++it) {
        // Truncate text to fit
        std::string display = it->text;
        if (display.size() > 46) display = display.substr(0, 45) + "~";

        sf::Color col = COL_TEXT();
        // Color by message type prefix
        if (display.substr(0,4) == "FILL") col = COL_GREEN();
        else if (display.substr(0,6) == "CANCEL") col = COL_YELLOW();
        else if (display.substr(0,3) == "NEW") col = sf::Color(180, 220, 255);

        drawText(display, {origin.x + 4.f, y}, 12, col);
        y += ROW_H;
        if (y + ROW_H > origin.y + H) break;
    }
}

// ── Panel 4: Holdings Table ────────────────────────────────────────────────────

void Renderer::drawHoldingsTable(sf::Vector2f origin, const Simulation& sim) {
    const float W = TOP_W, H = TOP_H;
    drawPanel(origin, {W, H}, "HOLDINGS — KostyaEx: XYZ");

    const auto& book = sim.getExchange().getBook();

    // Header
    const float PAD  = 19.f;
    const float ROW_H = 21.f;
    float y = origin.y + PAD;

    // Column positions (scaled for 480px panel width)
    float col_id   = origin.x + 5.f;
    float col_nlv  = origin.x + 30.f;
    float col_cash = origin.x + 126.f;
    float col_sh   = origin.x + 230.f;
    float col_bq   = origin.x + 310.f;
    float col_sq   = origin.x + 390.f;

    drawText("ID",   {col_id,   y}, 12, COL_HEADER());
    drawText("NLV",  {col_nlv,  y}, 12, COL_HEADER());
    drawText("Cash", {col_cash, y}, 12, COL_HEADER());
    drawText("Shrs", {col_sh,   y}, 12, COL_HEADER());
    drawText("BuyQ", {col_bq,   y}, 12, COL_BUY());
    drawText("SelQ", {col_sq,   y}, 12, COL_SELL());
    y += ROW_H;

    // Separator
    drawLine({origin.x + 2.f, y}, {origin.x + W - 2.f, y}, COL_BORDER());
    y += 2.f;

    int total_buy_qty = 0, total_sell_qty = 0;

    for (int i = 0; i < NUM_TRADERS; ++i) {
        const Trader& tr = sim.getTrader(i);
        auto acct = sim.getExchange().getAccount(tr.id());

        int buy_qty  = book.totalQtyForTrader(tr.id(), Side::BUY);
        int sell_qty = book.totalQtyForTrader(tr.id(), Side::SELL);
        total_buy_qty  += buy_qty;
        total_sell_qty += sell_qty;

        sf::Color row_col = (i < NUM_MARKET_MAKERS) ? COL_GREEN() : COL_YELLOW();

        // NLV = cash + shares * (ask if shares>=0 else bid)
        double nlv = acct.cash;
        if (acct.shares >= 0 && book.hasAsk())
            nlv += acct.shares * centsToDouble(book.bestAsk());
        else if (acct.shares < 0 && book.hasBid())
            nlv += acct.shares * centsToDouble(book.bestBid());
        else if (acct.shares != 0) {
            if (book.hasBid()) nlv += acct.shares * centsToDouble(book.bestBid());
            else if (book.hasAsk()) nlv += acct.shares * centsToDouble(book.bestAsk());
        }

        auto fmtMoney = [](char* buf, int n, double v) {
            if (std::abs(v) >= 1e6)      std::snprintf(buf, n, "%.1fM", v / 1e6);
            else if (std::abs(v) >= 1e3) std::snprintf(buf, n, "%.1fK", v / 1e3);
            else                         std::snprintf(buf, n, "%.0f", v);
        };

        char nlv_buf[24], cash_buf[24];
        fmtMoney(nlv_buf,  sizeof(nlv_buf),  nlv);
        fmtMoney(cash_buf, sizeof(cash_buf), acct.cash);

        sf::Color nlv_col = nlv > 0 ? COL_GREEN() : (nlv < 0 ? COL_SELL() : COL_DIM());

        drawText(std::to_string(tr.id()), {col_id,  y}, 12, row_col);
        drawText(nlv_buf,                 {col_nlv, y}, 12, nlv_col);
        drawText(cash_buf,                {col_cash, y}, 12, COL_TEXT());
        drawText(std::to_string(acct.shares), {col_sh, y}, 12,
                 acct.shares > 0 ? COL_BUY() : (acct.shares < 0 ? COL_SELL() : COL_DIM()));
        if (buy_qty > 0)
            drawText(std::to_string(buy_qty),  {col_bq, y}, 12, COL_BUY());
        if (sell_qty > 0)
            drawText(std::to_string(sell_qty), {col_sq, y}, 12, COL_SELL());
        y += ROW_H;
    }

    // Totals row
    drawLine({origin.x + 2.f, y}, {origin.x + W - 2.f, y}, COL_BORDER());
    y += 2.f;
    drawText("TOT", {col_id, y}, 12, COL_HEADER());
    drawText(std::to_string(total_buy_qty),  {col_bq, y}, 12, COL_BUY());
    drawText(std::to_string(total_sell_qty), {col_sq, y}, 12, COL_SELL());

    // Best bid/ask display
    y += ROW_H + 4.f;
    drawLine({origin.x + 2.f, y}, {origin.x + W - 2.f, y}, COL_BORDER());
    y += 4.f;
    char buf[64];
    if (book.hasBid() && book.hasAsk()) {
        std::snprintf(buf, sizeof(buf), "Bid: %.2f   Ask: %.2f   Sprd: %.2f",
            centsToDouble(book.bestBid()), centsToDouble(book.bestAsk()),
            centsToDouble(book.bestAsk() - book.bestBid()));
        drawText(buf, {origin.x + 4.f, y}, 12, COL_TEXT());
    }

    // Individual analyst opinions (2 rows of 5)
    y += ROW_H;
    drawText("Analyst fair values:", {origin.x + 4.f, y}, 12, COL_DIM());
    y += ROW_H;
    const auto& opinions = sim.getAnalysts().getOpinions();
    for (int row = 0; row < 2; ++row) {
        float ax = origin.x + 4.f;
        for (int col = 0; col < 5; ++col) {
            int idx = row * 5 + col;
            std::snprintf(buf, sizeof(buf), "A%d:%.2f", idx + 1, opinions[idx]);
            drawText(buf, {ax, y}, 12, COL_DIM());
            ax += 91.f;
        }
        y += ROW_H;
    }

    // Sim time
    y += ROW_H;
    SimTime t = sim.getSimTime();
    int mins = (int)t / 60, secs = (int)t % 60;
    std::snprintf(buf, sizeof(buf), "Sim time: %02d:%02d", mins, secs);
    drawText(buf, {origin.x + 4.f, y}, 12, COL_DIM());
}

// ── Trader panel (bottom grid) ────────────────────────────────────────────────

void Renderer::drawTraderPanel(sf::Vector2f origin, sf::Vector2f sz,
                               const Trader& trader, int idx, const Simulation& sim) {
    bool is_mm = (trader.id() <= NUM_MARKET_MAKERS);
    std::string title = (is_mm ? "MM" : "INV") + std::to_string(trader.id());
    drawPanel(origin, sz, title);

    const float PAD   = 17.f;
    const float ROW_H = 21.f;
    float y = origin.y + PAD;

    // Cash + shares header
    auto acct = sim.getExchange().getAccount(trader.id());
    char header[80];
    char cash_buf[20];
    double c = acct.cash;
    if (std::abs(c) >= 1e6)
        std::snprintf(cash_buf, sizeof(cash_buf), "%.2fM", c / 1e6);
    else
        std::snprintf(cash_buf, sizeof(cash_buf), "%.0f", c);
    double fv = trader.getFairValue(sim.getAnalysts().getOpinions());

    // Investor intention: BUY / SELL / HOLD derived from FV vs current bid/ask
    std::string intention;
    if (!is_mm) {
        const auto& bk = sim.getExchange().getBook();
        if (bk.hasBid() && bk.hasAsk()) {
            if      (fv > centsToDouble(bk.bestAsk())) intention = " [BUY]";
            else if (fv < centsToDouble(bk.bestBid())) intention = " [SELL]";
            else                                        intention = " [HOLD]";
        }
    }

    std::snprintf(header, sizeof(header), "$%s  Pos:%+d  FV:$%.2f%s",
                  cash_buf, acct.shares, fv, intention.c_str());
    drawText(header, {origin.x + 66.f, origin.y + 2.f}, 12,
             is_mm ? COL_GREEN() : COL_YELLOW());

    // Log entries
    const auto& log = trader.getLog();
    for (auto it = log.begin(); it != log.end(); ++it) {
        std::string display = it->text;
        if (display.size() > 46) display = display.substr(0, 45) + "~";

        sf::Color col = COL_TEXT();
        if (display.find("FILL") != std::string::npos)        col = COL_GREEN();
        else if (display.find("BUY")    != std::string::npos) col = COL_BUY();
        else if (display.find("SELL")   != std::string::npos) col = COL_SELL();
        else if (display.find("CANCEL") != std::string::npos) col = COL_YELLOW();

        drawText(display, {origin.x + 3.f, y}, 12, col);
        y += ROW_H;
        if (y + ROW_H > origin.y + sz.y) break;
    }
}
