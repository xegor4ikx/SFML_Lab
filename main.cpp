// ============================================================
// Лабораторна робота №5. Використання SFML в C++ проєкті.
// Використання пакетного менеджера vcpkg
// Варіант 23. Рух кулі похилою поверхнею з перешкодами
// Виконав: Силін Єгор, група ФІТ 3-9
//
// main.cpp - графічний інтерфейс (SFML 3):
//   handleInput()   - оброблення клавіатури та миші;
//   updatePhysics() - передача кроку часу фізичній моделі;
//   drawScene()     - відображення площини, перешкод, кулі й параметрів.
// Фізичні обчислення винесено в physics.cpp.
// ============================================================
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "physics.h"

namespace
{
    // ------------------------------------------------------------
    // Розміри вікна та перетворення координат
    // ------------------------------------------------------------
    constexpr unsigned kWinW = 1280;
    constexpr unsigned kWinH = 790;
    constexpr float kScale = 100.f;                 // пікселів на 1 метр
    const sf::Vector2f kBoardOrigin{34.f, 52.f};    // лівий верхній кут площини
    constexpr float kPanelX = 708.f;                // ліва межа панелі параметрів
    constexpr std::size_t kMaxResults = 6;
    constexpr std::size_t kMaxTrajectory = 4000;

    const sf::Color kBackground(24, 28, 36);
    const sf::Color kBoardColor(44, 56, 72);
    const sf::Color kGridColor(56, 70, 88);
    const sf::Color kGridStrong(70, 86, 106);
    const sf::Color kText(232, 236, 242);
    const sf::Color kMuted(150, 160, 176);
    const sf::Color kAccent(255, 196, 84);
    const sf::Color kBallColor(96, 206, 255);
    const sf::Color kPegColor(240, 150, 60);
    const sf::Color kWallColor(214, 120, 74);
    const sf::Color kTrajectoryColor(255, 220, 110, 210);

    sf::Vector2f toScreen(Vec2 p)
    {
        return {kBoardOrigin.x + static_cast<float>(p.x) * kScale,
                kBoardOrigin.y + static_cast<float>(p.y) * kScale};
    }

    Vec2 toWorld(sf::Vector2f s)
    {
        return {(s.x - kBoardOrigin.x) / kScale, (s.y - kBoardOrigin.y) / kScale};
    }

    bool insideBoard(sf::Vector2f s)
    {
        Vec2 w = toWorld(s);
        return w.x >= 0.0 && w.x <= Simulation::kWidth && w.y >= 0.0 && w.y <= Simulation::kLength;
    }

    // Рядок UTF-8 -> sf::String (для українських написів)
    sf::String U(const std::string& s)
    {
        return sf::String::fromUtf8(s.begin(), s.end());
    }

    std::string fmt(double value, int precision)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(precision) << value;
        return os.str();
    }

    std::string fmtSigned(double value, int precision)
    {
        return (value >= 0.0 ? "+" : "") + fmt(value, precision);
    }

    // ------------------------------------------------------------
    // Кнопки інтерфейсу
    // ------------------------------------------------------------
    enum ButtonId
    {
        BtnStartPause,
        BtnRestart,
        BtnAngleMinus,
        BtnAnglePlus,
        BtnMuMinus,
        BtnMuPlus,
        BtnShiftLeft,
        BtnShiftRight,
        BtnLayout0,     // BtnLayout0 + i - вибір макета i
    };

    struct Button
    {
        int id;
        sf::FloatRect rect;
        std::string label;
    };

    // Рядок таблиці результатів
    struct ResultRow
    {
        int layout;
        double angle;
        double mu;
        SimState state;
        double time;
        int collisions;
        double speed;
        double loss;
    };

    // Параметри командного рядка (для повторюваних скріншотів)
    struct Options
    {
        bool experiment = false;
        std::string csvPath = "experiment_results.csv";
        int layout = 1;
        double angle = 25.0;
        double mu = 0.05;
        double offset = 0.0;
        bool autoStart = false;
        double pauseAt = -1.0;
        std::string screenshot;
        bool exitAfterShot = false;
    };

    // ------------------------------------------------------------
    // Стан застосунку
    // ------------------------------------------------------------
    struct App
    {
        sf::RenderWindow window;
        sf::Font font;
        Simulation sim;
        std::vector<Button> buttons;
        std::deque<ResultRow> results;
        std::vector<sf::Vector2f> trajectory;
        int runNumber = 0;
        int dragIndex = -1;         // перешкода, яку перетягують мишею
        sf::Vector2f dragLast;
        bool showTrajectory = true;
        double pauseAt = -1.0;      // автоматична пауза (для скріншотів)
        std::string screenshot;
        bool exitAfterShot = false;
        bool shotPending = false;
        int frame = 0;
    };

    void clampParams(Params& p)
    {
        p.angleDeg = std::clamp(p.angleDeg, 0.0, 60.0);
        p.mu = std::clamp(p.mu, 0.0, 1.0);
    }

    void restart(App& app)
    {
        app.sim.reset();
        app.trajectory.clear();
    }

    void changeAngle(App& app, double delta)
    {
        app.sim.params.angleDeg = std::round(app.sim.params.angleDeg + delta);
        clampParams(app.sim.params);
    }

    void changeMu(App& app, double delta)
    {
        app.sim.params.mu = std::round((app.sim.params.mu + delta) * 100.0) / 100.0;
        clampParams(app.sim.params);
    }

    void selectLayout(App& app, int index)
    {
        app.sim.loadLayout(index);
        restart(app);
    }

    void startOrPause(App& app)
    {
        SimState s = app.sim.state();
        if (s == SimState::Finished || s == SimState::Stopped)
            restart(app);
        app.sim.toggleRun();
    }

    void createButtons(App& app)
    {
        const float x = kPanelX;
        auto add = [&app](int id, float bx, float by, float w, float h, const std::string& label)
        {
            app.buttons.push_back({id, sf::FloatRect({bx, by}, {w, h}), label});
        };
        // кнопки «−» і «+» біля параметрів
        add(BtnAngleMinus, x + 268, 98, 30, 26, "−");
        add(BtnAnglePlus, x + 304, 98, 30, 26, "+");
        add(BtnMuMinus, x + 268, 130, 30, 26, "−");
        add(BtnMuPlus, x + 304, 130, 30, 26, "+");
        add(BtnShiftLeft, x + 268, 162, 30, 26, "−");
        add(BtnShiftRight, x + 304, 162, 30, 26, "+");
        // вибір макета перешкод
        const char* names[] = {"1 · Без перешкод", "2 · Шаховий", "3 · Зигзаг", "4 · Воронка"};
        for (int i = 0; i < 4; ++i)
            add(BtnLayout0 + i, x + i * 136.f, 246, 128, 30, names[i]);
        // керування моделюванням
        add(BtnStartPause, x, 466, 268, 38, "Старт / Пауза   (Space)");
        add(BtnRestart, x + 276, 466, 268, 38, "Перезапуск   (R)");
    }

    void pressButton(App& app, int id)
    {
        switch (id)
        {
        case BtnStartPause: startOrPause(app); break;
        case BtnRestart: restart(app); break;
        case BtnAngleMinus: changeAngle(app, -1.0); break;
        case BtnAnglePlus: changeAngle(app, +1.0); break;
        case BtnMuMinus: changeMu(app, -0.01); break;
        case BtnMuPlus: changeMu(app, +0.01); break;
        case BtnShiftLeft: app.sim.shiftObstacles(-0.05); break;
        case BtnShiftRight: app.sim.shiftObstacles(+0.05); break;
        default:
            if (id >= BtnLayout0 && id < BtnLayout0 + Simulation::layoutCount())
                selectLayout(app, id - BtnLayout0);
            break;
        }
    }

    // ============================================================
    // 1. Оброблення введення
    // ============================================================
    void handleInput(App& app)
    {
        while (const std::optional event = app.window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                app.window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                switch (key->code)
                {
                case sf::Keyboard::Key::Escape: app.window.close(); break;
                case sf::Keyboard::Key::Space: startOrPause(app); break;
                case sf::Keyboard::Key::R: restart(app); break;
                case sf::Keyboard::Key::Up: changeAngle(app, +1.0); break;
                case sf::Keyboard::Key::Down: changeAngle(app, -1.0); break;
                case sf::Keyboard::Key::Right: changeMu(app, +0.01); break;
                case sf::Keyboard::Key::Left: changeMu(app, -0.01); break;
                case sf::Keyboard::Key::A: app.sim.shiftObstacles(-0.05); break;
                case sf::Keyboard::Key::D: app.sim.shiftObstacles(+0.05); break;
                case sf::Keyboard::Key::T: app.showTrajectory = !app.showTrajectory; break;
                case sf::Keyboard::Key::C: app.results.clear(); break;
                case sf::Keyboard::Key::Num1: case sf::Keyboard::Key::Numpad1: selectLayout(app, 0); break;
                case sf::Keyboard::Key::Num2: case sf::Keyboard::Key::Numpad2: selectLayout(app, 1); break;
                case sf::Keyboard::Key::Num3: case sf::Keyboard::Key::Numpad3: selectLayout(app, 2); break;
                case sf::Keyboard::Key::Num4: case sf::Keyboard::Key::Numpad4: selectLayout(app, 3); break;
                default: break;
                }
            }
            else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
            {
                sf::Vector2f p(mouse->position);
                if (mouse->button == sf::Mouse::Button::Left)
                {
                    bool onButton = false;
                    for (const Button& b : app.buttons)
                    {
                        if (b.rect.contains(p))
                        {
                            pressButton(app, b.id);
                            onButton = true;
                            break;
                        }
                    }
                    // ЛКМ на перешкоді - початок перетягування
                    if (!onButton && insideBoard(p))
                    {
                        app.dragIndex = app.sim.obstacleAt(toWorld(p));
                        app.dragLast = p;
                    }
                }
                else if (mouse->button == sf::Mouse::Button::Right && insideBoard(p))
                {
                    // ПКМ: на перешкоді - видалити, на порожньому місці - додати кілок
                    int index = app.sim.obstacleAt(toWorld(p));
                    if (index >= 0)
                        app.sim.removeObstacle(index);
                    else
                        app.sim.addPeg(toWorld(p));
                }
            }
            else if (const auto* moved = event->getIf<sf::Event::MouseMoved>())
            {
                if (app.dragIndex >= 0)
                {
                    sf::Vector2f p(moved->position);
                    p.x = std::clamp(p.x, kBoardOrigin.x, kBoardOrigin.x + float(Simulation::kWidth) * kScale);
                    p.y = std::clamp(p.y, kBoardOrigin.y, kBoardOrigin.y + float(Simulation::kLength) * kScale);
                    sf::Vector2f d = p - app.dragLast;
                    app.sim.moveObstacle(app.dragIndex, {d.x / kScale, d.y / kScale});
                    app.dragLast = p;
                }
            }
            else if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>())
            {
                if (released->button == sf::Mouse::Button::Left)
                    app.dragIndex = -1;
            }
        }
    }

    // ============================================================
    // 2. Оновлення фізичної моделі
    // ============================================================
    void updatePhysics(App& app, float dt)
    {
        // Обмежуємо крок (наприклад, після перетягування вікна),
        // щоб модель не «перестрибувала» перешкоди
        dt = std::min(dt, 0.05f);

        if (app.sim.state() == SimState::Running)
        {
            app.sim.advance(dt);

            // траєкторія центра кулі
            sf::Vector2f p = toScreen(app.sim.position());
            if (app.trajectory.empty())
                app.trajectory.push_back(toScreen(Vec2{Simulation::kWidth / 2.0, 0.45}));
            sf::Vector2f last = app.trajectory.back();
            if (std::hypot(p.x - last.x, p.y - last.y) > 1.5f)
                app.trajectory.push_back(p);
            if (app.trajectory.size() > kMaxTrajectory)
                app.trajectory.erase(app.trajectory.begin());

            if (app.pauseAt > 0.0 && app.sim.stats().time >= app.pauseAt)
            {
                app.sim.pause();
                app.pauseAt = -1.0;
                app.shotPending = !app.screenshot.empty();
            }
        }

        // Запуск завершився - заносимо результат у таблицю
        if (app.sim.consumeEndEvent())
        {
            const Stats& st = app.sim.stats();
            app.results.push_back({app.sim.layout(), app.sim.params.angleDeg, app.sim.params.mu,
                                   app.sim.state(), st.time, st.collisions, app.sim.speed(),
                                   app.sim.energyLoss()});
            ++app.runNumber;
            if (app.results.size() > kMaxResults)
                app.results.pop_front();
            app.shotPending = !app.screenshot.empty();
        }
    }

    // ============================================================
    // 3. Відображення сцени
    // ============================================================
    void drawText(App& app, const std::string& s, float x, float y, unsigned size,
                  sf::Color color = kText, bool bold = false)
    {
        sf::Text text(app.font, U(s), size);
        text.setFillColor(color);
        text.setStyle(bold ? sf::Text::Bold : sf::Text::Regular);
        text.setPosition({x, y});
        app.window.draw(text);
    }

    void drawTextCentered(App& app, const std::string& s, sf::FloatRect r, unsigned size, sf::Color color)
    {
        sf::Text text(app.font, U(s), size);
        text.setFillColor(color);
        sf::FloatRect b = text.getLocalBounds();
        text.setPosition({std::round(r.position.x + (r.size.x - b.size.x) / 2.f - b.position.x),
                          std::round(r.position.y + (r.size.y - b.size.y) / 2.f - b.position.y)});
        app.window.draw(text);
    }

    // Товста лінія із заокругленими кінцями (для бортиків)
    void drawThickLine(sf::RenderWindow& w, sf::Vector2f a, sf::Vector2f b, float thickness, sf::Color color)
    {
        sf::Vector2f d = b - a;
        float len = std::hypot(d.x, d.y);
        sf::RectangleShape rect({len, thickness});
        rect.setOrigin({0.f, thickness / 2.f});
        rect.setPosition(a);
        rect.setRotation(sf::radians(std::atan2(d.y, d.x)));
        rect.setFillColor(color);
        w.draw(rect);
        sf::CircleShape cap(thickness / 2.f);
        cap.setOrigin({thickness / 2.f, thickness / 2.f});
        cap.setFillColor(color);
        cap.setPosition(a);
        w.draw(cap);
        cap.setPosition(b);
        w.draw(cap);
    }

    void drawArrow(sf::RenderWindow& w, sf::Vector2f from, sf::Vector2f to, sf::Color color, float width = 2.f)
    {
        sf::Vector2f d = to - from;
        float len = std::hypot(d.x, d.y);
        if (len < 4.f)
            return;
        sf::Vector2f u = d / len;
        sf::Vector2f n(-u.y, u.x);
        drawThickLine(w, from, to - u * 6.f, width, color);
        sf::ConvexShape head(3);
        head.setPoint(0, to);
        head.setPoint(1, to - u * 11.f + n * 5.5f);
        head.setPoint(2, to - u * 11.f - n * 5.5f);
        head.setFillColor(color);
        w.draw(head);
    }

    void drawBoard(App& app)
    {
        sf::RenderWindow& w = app.window;
        const float bw = float(Simulation::kWidth) * kScale;
        const float bh = float(Simulation::kLength) * kScale;

        drawText(app, "Вигляд згори на похилу площину 6,4 × 7,0 м  (схил спрямований донизу)",
                 kBoardOrigin.x, 16, 15, kMuted);

        sf::RectangleShape board({bw, bh});
        board.setPosition(kBoardOrigin);
        board.setFillColor(kBoardColor);
        w.draw(board);

        // Сітка: тонкі лінії через 0,5 м, підписи через 1 м
        for (int i = 1; i < 14; ++i)
        {
            float y = kBoardOrigin.y + i * 0.5f * kScale;
            sf::RectangleShape line({bw, 1.f});
            line.setPosition({kBoardOrigin.x, y});
            line.setFillColor(i % 2 == 0 ? kGridStrong : kGridColor);
            w.draw(line);
        }
        for (int i = 1; i < 13; ++i)
        {
            float x = kBoardOrigin.x + i * 0.5f * kScale;
            sf::RectangleShape line({1.f, bh});
            line.setPosition({x, kBoardOrigin.y});
            line.setFillColor(i % 2 == 0 ? kGridStrong : kGridColor);
            w.draw(line);
        }
        for (int m = 0; m <= 7; ++m)
            drawText(app, std::to_string(m), 14, kBoardOrigin.y + m * kScale - 9, 13, kMuted);

        // Фініш - нижній край площини
        for (int i = 0; i < 32; ++i)
        {
            sf::RectangleShape cell({bw / 32.f, 8.f});
            cell.setPosition({kBoardOrigin.x + i * bw / 32.f, kBoardOrigin.y + bh - 8.f});
            cell.setFillColor(i % 2 == 0 ? sf::Color(235, 235, 235) : sf::Color(40, 40, 40));
            w.draw(cell);
        }
        drawText(app, "ФІНІШ", kBoardOrigin.x + 8, kBoardOrigin.y + bh - 30, 14, sf::Color(235, 235, 235), true);

        // Стрілка напрямку складової сили тяжіння
        sf::Vector2f a0(kBoardOrigin.x + bw - 46, kBoardOrigin.y + 18);
        drawArrow(w, a0, a0 + sf::Vector2f(0, 46), kAccent, 3.f);
        drawText(app, "g·sin α", a0.x - 66, a0.y + 12, 14, kAccent);

        // Стартова позиція
        sf::CircleShape startMark(float(Simulation::kBallRadius) * kScale + 3.f);
        startMark.setOrigin({startMark.getRadius(), startMark.getRadius()});
        startMark.setPosition(toScreen({Simulation::kWidth / 2.0, 0.45}));
        startMark.setFillColor(sf::Color::Transparent);
        startMark.setOutlineThickness(1.5f);
        startMark.setOutlineColor(sf::Color(120, 140, 165));
        w.draw(startMark);
        drawText(app, "старт", startMark.getPosition().x + 22, startMark.getPosition().y - 10, 13, kMuted);

        // Рамка площини
        board.setFillColor(sf::Color::Transparent);
        board.setOutlineThickness(3.f);
        board.setOutlineColor(sf::Color(126, 142, 164));
        w.draw(board);
    }

    void drawObstacles(App& app)
    {
        sf::Vector2f mouse(sf::Mouse::getPosition(app.window));
        int hover = insideBoard(mouse) ? app.sim.obstacleAt(toWorld(mouse)) : -1;

        const auto& obs = app.sim.obstacles();
        for (int i = 0; i < static_cast<int>(obs.size()); ++i)
        {
            const Obstacle& ob = obs[i];
            bool active = (i == hover || i == app.dragIndex);
            if (ob.type == ObstacleType::Peg)
            {
                float r = float(ob.radius) * kScale;
                sf::CircleShape c(r);
                c.setOrigin({r, r});
                c.setPosition(toScreen(ob.a));
                c.setFillColor(kPegColor);
                c.setOutlineThickness(active ? 3.f : 2.f);
                c.setOutlineColor(active ? sf::Color::White : sf::Color(150, 82, 30));
                app.window.draw(c);
            }
            else
            {
                float t = float(ob.radius) * 2.f * kScale;
                if (active)
                    drawThickLine(app.window, toScreen(ob.a), toScreen(ob.b), t + 5.f, sf::Color::White);
                drawThickLine(app.window, toScreen(ob.a), toScreen(ob.b), t, kWallColor);
            }
        }
    }

    void drawBall(App& app)
    {
        if (app.showTrajectory && app.trajectory.size() > 1)
        {
            sf::VertexArray line(sf::PrimitiveType::LineStrip);
            for (const sf::Vector2f& p : app.trajectory)
                line.append(sf::Vertex{p, kTrajectoryColor});
            app.window.draw(line);
        }

        sf::Vector2f c = toScreen(app.sim.position());
        float r = float(Simulation::kBallRadius) * kScale;

        sf::CircleShape shadow(r);
        shadow.setOrigin({r, r});
        shadow.setPosition(c + sf::Vector2f(3.f, 4.f));
        shadow.setFillColor(sf::Color(0, 0, 0, 90));
        app.window.draw(shadow);

        sf::CircleShape ball(r);
        ball.setOrigin({r, r});
        ball.setPosition(c);
        ball.setFillColor(kBallColor);
        ball.setOutlineThickness(2.f);
        ball.setOutlineColor(sf::Color::White);
        app.window.draw(ball);

        sf::CircleShape glare(r * 0.32f);
        glare.setOrigin({r * 0.32f, r * 0.32f});
        glare.setPosition(c + sf::Vector2f(-r * 0.35f, -r * 0.35f));
        glare.setFillColor(sf::Color(255, 255, 255, 170));
        app.window.draw(glare);

        // Вектор швидкості (25 пікселів на 1 м/с); після фінішу не показуємо
        if (app.sim.state() != SimState::Finished)
        {
            Vec2 v = app.sim.velocity();
            sf::Vector2f tip = c + sf::Vector2f(float(v.x), float(v.y)) * 25.f;
            drawArrow(app.window, c, tip, sf::Color(255, 255, 255, 200), 2.f);
        }
    }

    // Вигляд збоку: трикутник із кутом нахилу α
    void drawSideView(App& app)
    {
        const float x = kPanelX + 372, y = 84, w = 172, h = 150;
        sf::RectangleShape frame({w, h});
        frame.setPosition({x, y});
        frame.setFillColor(sf::Color(32, 38, 50));
        frame.setOutlineThickness(1.f);
        frame.setOutlineColor(sf::Color(70, 82, 100));
        app.window.draw(frame);
        drawText(app, "Вигляд збоку", x + 8, y + 5, 13, kMuted);

        double alpha = app.sim.params.angleDeg * 3.14159265358979 / 180.0;
        float base = 140.f;
        float height = base * float(std::tan(alpha));
        if (height > 100.f)
        {
            height = 100.f;
            base = alpha > 1e-6 ? height / float(std::tan(alpha)) : base;
        }
        sf::Vector2f bottomRight(x + 16 + 140.f, y + h - 14);
        sf::Vector2f bottomLeft(bottomRight.x - base, bottomRight.y);
        sf::Vector2f top(bottomLeft.x, bottomRight.y - height);

        sf::ConvexShape tri(3);
        tri.setPoint(0, bottomLeft);
        tri.setPoint(1, bottomRight);
        tri.setPoint(2, top);
        tri.setFillColor(sf::Color(76, 92, 116));
        tri.setOutlineThickness(1.f);
        tri.setOutlineColor(sf::Color(140, 156, 180));
        app.window.draw(tri);

        // Куля на схилі відповідно до координати y
        float k = float(app.sim.position().y / Simulation::kLength);
        sf::Vector2f along = top + (bottomRight - top) * k;
        sf::Vector2f dir = bottomRight - top;
        float len = std::hypot(dir.x, dir.y);
        sf::Vector2f normal = len > 0 ? sf::Vector2f(dir.y / len, -dir.x / len) : sf::Vector2f(0, -1);
        sf::CircleShape ball(6.f);
        ball.setOrigin({6.f, 6.f});
        ball.setPosition(along + normal * 6.f);
        ball.setFillColor(kBallColor);
        app.window.draw(ball);

        // Дуга кута α біля нижньої вершини
        if (alpha > 1e-3)
        {
            sf::VertexArray arc(sf::PrimitiveType::LineStrip);
            const float rArc = 34.f;
            for (int i = 0; i <= 16; ++i)
            {
                float t = 3.14159265f - float(alpha) * i / 16.f;   // від основи до схилу
                arc.append(sf::Vertex{bottomRight + sf::Vector2f(std::cos(t), -std::sin(t)) * rArc, kAccent});
            }
            app.window.draw(arc);
        }
        drawText(app, "α = " + fmt(app.sim.params.angleDeg, 0) + "°", x + w - 64, y + 5, 14, kAccent, true);
    }

    void drawPanel(App& app)
    {
        const float x = kPanelX;
        const Simulation& sim = app.sim;
        const Params& p = sim.params;
        const Stats& st = sim.stats();

        drawText(app, "Рух кулі похилою поверхнею з перешкодами", x, 14, 21, kText, true);
        drawText(app, "Лабораторна робота №5 · варіант 23 · SFML 3", x, 44, 14, kMuted);

        // --- Параметри
        drawText(app, "ПАРАМЕТРИ МОДЕЛІ", x, 78, 14, kAccent, true);
        drawText(app, "Кут нахилу α", x, 100, 16);
        drawText(app, fmt(p.angleDeg, 0) + "°", x + 190, 100, 16, kText, true);
        drawText(app, "↑ ↓", x + 340, 102, 13, kMuted);
        drawText(app, "Коеф. тертя μ", x, 132, 16);
        drawText(app, fmt(p.mu, 2), x + 190, 132, 16, kText, true);
        drawText(app, "← →", x + 340, 134, 13, kMuted);
        drawText(app, "Зсув перешкод", x, 164, 16);
        drawText(app, fmtSigned(sim.offset(), 2) + " м", x + 190, 164, 16, kText, true);
        drawText(app, "A D", x + 340, 166, 13, kMuted);
        drawText(app, "Коеф. відновлення e", x, 196, 16);
        drawText(app, fmt(p.restitution, 2), x + 190, 196, 16, kText, true);
        drawText(app, "Макет перешкод (1–4):  " + std::string(Simulation::layoutName(sim.layout())),
                 x, 226, 14, kMuted);

        drawSideView(app);

        // --- Стан
        SimState s = sim.state();
        sf::Color stateColor = s == SimState::Running ? sf::Color(110, 220, 120)
                             : s == SimState::Paused ? kAccent
                             : s == SimState::Finished ? kBallColor
                             : s == SimState::Stopped ? sf::Color(255, 110, 100)
                                                      : kMuted;
        drawText(app, "СТАН МОДЕЛЮВАННЯ", x, 292, 14, kAccent, true);
        drawText(app, Simulation::stateName(s), x + 190, 290, 16, stateColor, true);

        const float c2 = x + 276;
        float y = 316;
        drawText(app, "t = " + fmt(st.time, 2) + " с", x, y, 16);
        drawText(app, "шлях s = " + fmt(st.path, 2) + " м", c2, y, 16);
        y += 24;
        drawText(app, "x = " + fmt(sim.position().x, 2) + " м", x, y, 16);
        drawText(app, "y = " + fmt(sim.position().y, 2) + " м", c2, y, 16);
        y += 24;
        drawText(app, "|v| = " + fmt(sim.speed(), 2) + " м/с", x, y, 16);
        drawText(app, "v max = " + fmt(st.maxSpeed, 2) + " м/с", c2, y, 16);
        y += 24;
        drawText(app, "Зіткнень: " + std::to_string(st.collisions), x, y, 16);
        drawText(app, "Втрати енергії: " + fmt(sim.energyLoss() * 100.0, 0) + " %", c2, y, 16);
        y += 28;
        double tanA = std::tan(p.angleDeg * 3.14159265358979 / 180.0);
        if (sim.canStartMoving())
            drawText(app, "a = g(sin α − μ cos α) = " + fmt(sim.freeAcceleration(), 2) +
                     " м/с²   (tg α = " + fmt(tanA, 2) + " > μ)", x, y, 15, kMuted);
        else
            drawText(app, "tg α = " + fmt(tanA, 2) + " ≤ μ = " + fmt(p.mu, 2) +
                     " — тертя утримує кулю на місці", x, y, 15, sf::Color(255, 140, 120));

        // --- Таблиця результатів
        drawText(app, "РЕЗУЛЬТАТИ ЗАПУСКІВ", x, 520, 14, kAccent, true);
        drawText(app, "(C — очистити)", x + 190, 520, 13, kMuted);
        const float cols[] = {x, x + 34, x + 196, x + 244, x + 296, x + 364, x + 420, x + 490};
        const char* head[] = {"№", "Макет", "α, °", "μ", "t, с", "Зітк.", "v, м/с", "Стан"};
        for (int i = 0; i < 8; ++i)
            drawText(app, head[i], cols[i], 544, 14, kMuted, true);
        sf::RectangleShape sep({544.f, 1.f});
        sep.setPosition({x, 564});
        sep.setFillColor(sf::Color(80, 92, 110));
        app.window.draw(sep);

        int number = app.runNumber - static_cast<int>(app.results.size()) + 1;
        float ry = 570;
        for (const ResultRow& r : app.results)
        {
            bool fin = r.state == SimState::Finished;
            sf::Color c = fin ? kText : sf::Color(255, 140, 120);
            drawText(app, std::to_string(number++), cols[0], ry, 14, c);
            drawText(app, Simulation::layoutName(r.layout), cols[1], ry, 14, c);
            drawText(app, fmt(r.angle, 0), cols[2], ry, 14, c);
            drawText(app, fmt(r.mu, 2), cols[3], ry, 14, c);
            drawText(app, fmt(r.time, 2), cols[4], ry, 14, c);
            drawText(app, std::to_string(r.collisions), cols[5], ry, 14, c);
            drawText(app, fmt(r.speed, 2), cols[6], ry, 14, c);
            drawText(app, fin ? "фініш" : "зупинка", cols[7], ry, 14, c);
            ry += 21;
        }
        if (app.results.empty())
            drawText(app, "Ще немає завершених запусків", x, ry, 14, kMuted);

        // --- Підказки
        drawText(app, "Миша: ЛКМ — перетягнути перешкоду,  ПКМ — додати / видалити кілок",
                 x, 728, 13, kMuted);
        drawText(app, "Space — старт/пауза   R — перезапуск   T — траєкторія   Esc — вихід",
                 x, 748, 13, kMuted);
    }

    void drawButtons(App& app)
    {
        sf::Vector2f mouse(sf::Mouse::getPosition(app.window));
        for (const Button& b : app.buttons)
        {
            bool selected = b.id >= BtnLayout0 && b.id - BtnLayout0 == app.sim.layout();
            bool hover = b.rect.contains(mouse);
            sf::RectangleShape r(b.rect.size);
            r.setPosition(b.rect.position);
            r.setFillColor(selected ? sf::Color(255, 196, 84, 60)
                           : hover ? sf::Color(70, 84, 104) : sf::Color(50, 60, 76));
            r.setOutlineThickness(1.5f);
            r.setOutlineColor(selected ? kAccent : sf::Color(96, 110, 132));
            app.window.draw(r);
            unsigned size = (b.label.size() <= 3) ? 18 : (b.id >= BtnLayout0 ? 13 : 14);
            drawTextCentered(app, b.label, b.rect, size, selected ? kAccent : kText);
        }
    }

    void drawScene(App& app)
    {
        app.window.clear(kBackground);
        drawBoard(app);
        drawObstacles(app);
        drawBall(app);
        drawPanel(app);
        drawButtons(app);
    }

    // Збереження вмісту вікна у файл (для скріншотів у звіт)
    void saveScreenshot(App& app)
    {
        sf::Texture texture(app.window.getSize());
        texture.update(app.window);
        if (texture.copyToImage().saveToFile(app.screenshot))
            std::cout << "Screenshot saved: " << app.screenshot << "\n";
    }

    // Пошук шрифту: поруч із exe, у робочій папці або на рівень вище
    bool loadFont(sf::Font& font, const std::filesystem::path& exeDir)
    {
        const std::filesystem::path candidates[] = {
            exeDir / "resources/fonts/DejaVuSans.ttf",
            "resources/fonts/DejaVuSans.ttf",
            "../resources/fonts/DejaVuSans.ttf",
            "../../resources/fonts/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf",
        };
        for (const auto& path : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && font.openFromFile(path))
                return true;
        }
        return false;
    }

    Options parseOptions(int argc, char* argv[])
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](double fallback)
            {
                return (i + 1 < argc) ? std::stod(argv[++i]) : fallback;
            };
            if (a == "--experiment")
            {
                o.experiment = true;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                    o.csvPath = argv[++i];
            }
            else if (a == "--layout") o.layout = static_cast<int>(next(2)) - 1;
            else if (a == "--angle") o.angle = next(25);
            else if (a == "--mu") o.mu = next(0.05);
            else if (a == "--offset") o.offset = next(0);
            else if (a == "--start") o.autoStart = true;
            else if (a == "--pause-at") o.pauseAt = next(-1);
            else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
            else if (a == "--exit-after-shot") o.exitAfterShot = true;
        }
        return o;
    }
}

int main(int argc, char* argv[])
{
    Options opt = parseOptions(argc, argv);

    // Режим обчислювального експерименту - без вікна
    if (opt.experiment)
        return runExperiment(opt.csvPath);

    App app;
    app.sim.params.angleDeg = opt.angle;
    app.sim.params.mu = opt.mu;
    clampParams(app.sim.params);
    app.sim.loadLayout(opt.layout);
    app.sim.shiftObstacles(opt.offset);
    app.sim.reset();
    app.pauseAt = opt.pauseAt;
    app.screenshot = opt.screenshot;
    app.exitAfterShot = opt.exitAfterShot;

    std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();
    if (!loadFont(app.font, exeDir))
    {
        std::cerr << "Font resources/fonts/DejaVuSans.ttf not found\n";
        return 1;
    }

    sf::ContextSettings settings;
    settings.antiAliasingLevel = 4;
    app.window.create(sf::VideoMode({kWinW, kWinH}),
                      U("ЛР5 · Варіант 23 · Рух кулі похилою поверхнею з перешкодами"),
                      sf::Style::Titlebar | sf::Style::Close, sf::State::Windowed, settings);
    app.window.setFramerateLimit(60);
    createButtons(app);

    if (opt.autoStart)
        app.sim.start();

    sf::Clock clock;
    while (app.window.isOpen())
    {
        float dt = clock.restart().asSeconds();

        handleInput(app);           // 1. Оброблення подій
        updatePhysics(app, dt);     // 2. Оновлення фізичної моделі
        drawScene(app);             // 3. Відображення сцени

        // Скріншот без запуску моделювання - через кілька кадрів
        ++app.frame;
        if (!app.screenshot.empty() && !opt.autoStart && app.frame == 20)
            app.shotPending = true;
        if (app.shotPending)
        {
            saveScreenshot(app);
            app.shotPending = false;
            if (app.exitAfterShot)
                app.window.close();
        }

        app.window.display();
    }
    return 0;
}
