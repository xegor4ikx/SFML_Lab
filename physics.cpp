// ============================================================
// physics.cpp - реалізація фізичної моделі (варіант 23)
//
// Куля рухається похилою площиною з кутом нахилу α.
// Сили, що діють у площині руху:
//   - складова сили тяжіння вздовж схилу:  m·g·sin α  (вздовж осі Y);
//   - сила тертя ковзання: μ·N = μ·m·g·cos α, спрямована проти швидкості;
//   - сили реакції перешкод і бортів під час зіткнень
//     (відбиття з коефіцієнтом відновлення e).
// Інтегрування - напівнеявний метод Ейлера:
//   v(n+1) = v(n) + a·Δt,   r(n+1) = r(n) + v(n+1)·Δt
// ============================================================
#include "physics.h"

#include <algorithm>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Удар слабший за цю швидкість не рахується як окреме зіткнення
    // (куля просто котиться вздовж бортика)
    constexpr double kImpactThreshold = 0.10;   // м/с

    // Якщо за цей час куля зміщується менше ніж на kStallDistance,
    // вважаємо, що вона зупинилася
    constexpr double kStallTime = 1.5;          // с
    constexpr double kStallDistance = 0.02;     // м

    Obstacle peg(double x, double y, double r = 0.10)
    {
        return {ObstacleType::Peg, {x, y}, {x, y}, r};
    }

    Obstacle wall(double x1, double y1, double x2, double y2)
    {
        return {ObstacleType::Wall, {x1, y1}, {x2, y2}, 0.05};
    }

    // Найближча до точки p точка відрізка ab
    Vec2 closestOnSegment(Vec2 p, Vec2 a, Vec2 b)
    {
        Vec2 ab = b - a;
        double len2 = dot(ab, ab);
        if (len2 < 1e-12)
            return a;
        double t = std::clamp(dot(p - a, ab) / len2, 0.0, 1.0);
        return a + ab * t;
    }
}

Simulation::Simulation()
{
    loadLayout(1);
    reset();
}

// ---------------------------------------------------------------
// Керування моделюванням
// ---------------------------------------------------------------
void Simulation::reset()
{
    pos_ = startPos_;
    vel_ = {0.0, 0.0};
    stats_ = Stats{};
    state_ = SimState::Ready;
    anchorPos_ = pos_;
    anchorTime_ = 0.0;
    endEvent_ = false;
    accumulator_ = 0.0;
}

void Simulation::start()
{
    if (state_ == SimState::Finished || state_ == SimState::Stopped)
        reset();
    if (state_ == SimState::Ready)
    {
        // Початкова механічна енергія (на одиницю маси) відносно нижнього краю
        double alpha = params.angleDeg * kPi / 180.0;
        startHeightEnergy_ = params.g * (kLength - kBallRadius - pos_.y) * std::sin(alpha);
        anchorPos_ = pos_;
        anchorTime_ = 0.0;
    }
    state_ = SimState::Running;
}

void Simulation::pause()
{
    if (state_ == SimState::Running)
        state_ = SimState::Paused;
}

void Simulation::toggleRun()
{
    if (state_ == SimState::Running)
        pause();
    else
        start();
}

// ---------------------------------------------------------------
// Перешкоди
// ---------------------------------------------------------------
void Simulation::loadLayout(int index)
{
    obstacles_.clear();
    layout_ = std::clamp(index, 0, layoutCount() - 1);
    offset_ = 0.0;

    switch (layout_)
    {
    case 0:     // без перешкод
        break;

    case 1:     // кілки в шаховому порядку (дошка Гальтона)
        for (int row = 0; row < 7; ++row)
        {
            double y = 1.5 + 0.75 * row;
            bool even = (row % 2 == 0);
            double x0 = even ? 0.35 : 0.75;
            int count = even ? 8 : 7;
            for (int k = 0; k < count; ++k)
                obstacles_.push_back(peg(x0 + 0.8 * k, y));
        }
        break;

    case 2:     // зигзаг із похилих бортиків
        obstacles_.push_back(wall(0.0, 1.4, 4.7, 2.5));
        obstacles_.push_back(wall(6.4, 3.1, 1.7, 4.2));
        obstacles_.push_back(wall(0.0, 4.8, 4.7, 5.9));
        break;

    case 3:     // воронка з кілками
        obstacles_.push_back(peg(3.10, 1.20, 0.12));
        obstacles_.push_back(wall(0.30, 1.80, 2.75, 3.40));
        obstacles_.push_back(wall(6.10, 1.80, 3.65, 3.40));
        obstacles_.push_back(peg(3.20, 4.40, 0.12));
        obstacles_.push_back(peg(2.50, 5.20, 0.12));
        obstacles_.push_back(peg(3.90, 5.20, 0.12));
        obstacles_.push_back(peg(1.80, 6.00, 0.12));
        obstacles_.push_back(peg(3.20, 6.00, 0.12));
        obstacles_.push_back(peg(4.60, 6.00, 0.12));
        break;
    }
}

void Simulation::shiftObstacles(double dx)
{
    for (Obstacle& ob : obstacles_)
    {
        ob.a.x += dx;
        ob.b.x += dx;
    }
    offset_ += dx;
}

void Simulation::moveObstacle(int index, Vec2 delta)
{
    if (index < 0 || index >= static_cast<int>(obstacles_.size()))
        return;
    obstacles_[index].a = obstacles_[index].a + delta;
    obstacles_[index].b = obstacles_[index].b + delta;
}

void Simulation::addPeg(Vec2 center)
{
    obstacles_.push_back(peg(center.x, center.y, kPegRadius));
}

void Simulation::removeObstacle(int index)
{
    if (index >= 0 && index < static_cast<int>(obstacles_.size()))
        obstacles_.erase(obstacles_.begin() + index);
}

int Simulation::obstacleAt(Vec2 point) const
{
    // Перевіряємо з кінця, щоб першою бралася перешкода, намальована зверху
    for (int i = static_cast<int>(obstacles_.size()) - 1; i >= 0; --i)
    {
        const Obstacle& ob = obstacles_[i];
        Vec2 q = (ob.type == ObstacleType::Peg) ? ob.a : closestOnSegment(point, ob.a, ob.b);
        if (length(point - q) <= ob.radius + 0.08)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------
// Чисельне моделювання
// ---------------------------------------------------------------
void Simulation::advance(double frameDt)
{
    if (state_ != SimState::Running || frameDt <= 0.0)
        return;

    // Фіксований крок моделювання: реальний час кадру dt (з sf::Clock)
    // накопичується і «витрачається» рівними кроками kFixedStep = 1/240 с.
    // Так результат не залежить від частоти кадрів, зіткнення не
    // «проскакують», а запуск у вікні збігається з режимом --experiment.
    accumulator_ += frameDt;
    while (accumulator_ >= kFixedStep && state_ == SimState::Running)
    {
        step(kFixedStep);
        accumulator_ -= kFixedStep;
    }
}

void Simulation::step(double dt)
{
    const double alpha = params.angleDeg * kPi / 180.0;
    const double gAlong = params.g * std::sin(alpha);              // g·sin α
    const double frictionAcc = params.mu * params.g * std::cos(alpha);  // μ·g·cos α

    // 1. Сила тяжіння: прискорення g·sin α вниз по схилу
    vel_.y += gAlong * dt;

    // 2. Тертя ковзання зменшує модуль швидкості на μ·g·cos α·Δt.
    //    Якщо швидкість менша за це значення, куля зупиняється
    //    (тертя не може змусити її рухатися назад) - це і є умова спокою tg α ≤ μ.
    double v = length(vel_);
    double dv = frictionAcc * dt;
    if (v > dv)
        vel_ = vel_ * ((v - dv) / v);
    else
        vel_ = {0.0, 0.0};

    // 3. Нове положення
    Vec2 oldPos = pos_;
    pos_ = pos_ + vel_ * dt;

    // 4. Зіткнення з перешкодами та бічними бортами
    for (const Obstacle& ob : obstacles_)
        collideWithObstacle(ob);
    collideWithWalls();

    // 5. Статистика
    stats_.time += dt;
    stats_.path += length(pos_ - oldPos);
    stats_.maxSpeed = std::max(stats_.maxSpeed, length(vel_));

    // 6. Фініш - куля торкнулася нижнього краю площини
    if (pos_.y >= kLength - kBallRadius)
    {
        pos_.y = kLength - kBallRadius;
        state_ = SimState::Finished;
        endEvent_ = true;
        return;
    }

    // 7. Зупинка: куля довго не зміщується або вичерпано час
    if (length(pos_ - anchorPos_) > kStallDistance)
    {
        anchorPos_ = pos_;
        anchorTime_ = stats_.time;
    }
    else if (stats_.time - anchorTime_ > kStallTime)
    {
        // час руху - до моменту, коли куля востаннє помітно змістилася
        stats_.time = anchorTime_;
        vel_ = {0.0, 0.0};
        state_ = SimState::Stopped;
        endEvent_ = true;
        return;
    }
    if (stats_.time > kMaxTime)
    {
        state_ = SimState::Stopped;
        endEvent_ = true;
    }
}

void Simulation::collideWithObstacle(const Obstacle& ob)
{
    Vec2 closest = (ob.type == ObstacleType::Peg) ? ob.a : closestOnSegment(pos_, ob.a, ob.b);
    collideWithPoint(closest, ob.radius + kBallRadius);
}

// Зіткнення кулі з нерухомою точкою перешкоди.
// n - нормаль від перешкоди до центра кулі; нормальна складова
// швидкості змінює знак і зменшується в e разів, дотична - зберігається.
void Simulation::collideWithPoint(Vec2 closest, double minDist)
{
    Vec2 d = pos_ - closest;
    double dist = length(d);
    if (dist >= minDist)
        return;

    Vec2 n = (dist > 1e-9) ? d * (1.0 / dist) : Vec2{0.0, -1.0};

    // Виштовхуємо кулю з перешкоди, щоб вона не «застрягала» всередині
    pos_ = closest + n * minDist;

    double vn = dot(vel_, n);
    if (vn < 0.0)
    {
        // Слабкий контакт (куля котиться вздовж перешкоди) - без відскоку
        bool impact = -vn > kImpactThreshold;
        double e = impact ? params.restitution : 0.0;
        vel_ = vel_ - n * ((1.0 + e) * vn);
        if (impact)
            ++stats_.collisions;
    }
}

void Simulation::collideWithWalls()
{
    const double r = kBallRadius;
    auto bounce = [this](double& component)
    {
        bool impact = std::abs(component) > kImpactThreshold;
        component = impact ? -component * params.restitution : 0.0;
        if (impact)
            ++stats_.collisions;
    };

    if (pos_.x < r)
    {
        pos_.x = r;
        if (vel_.x < 0.0) bounce(vel_.x);
    }
    if (pos_.x > kWidth - r)
    {
        pos_.x = kWidth - r;
        if (vel_.x > 0.0) bounce(vel_.x);
    }
    if (pos_.y < r)     // верхній борт (куля може відскочити вгору по схилу)
    {
        pos_.y = r;
        if (vel_.y < 0.0) bounce(vel_.y);
    }
}

// ---------------------------------------------------------------
// Допоміжні величини
// ---------------------------------------------------------------
double Simulation::freeAcceleration() const
{
    double alpha = params.angleDeg * kPi / 180.0;
    return params.g * (std::sin(alpha) - params.mu * std::cos(alpha));
}

bool Simulation::canStartMoving() const
{
    double alpha = params.angleDeg * kPi / 180.0;
    return std::tan(alpha) > params.mu;
}

double Simulation::energyLoss() const
{
    // E = g·h + v²/2 (на одиницю маси), h - висота над нижнім краєм
    if (startHeightEnergy_ <= 1e-9 || state_ == SimState::Ready)
        return 0.0;
    double alpha = params.angleDeg * kPi / 180.0;
    double h = (kLength - kBallRadius - pos_.y) * std::sin(alpha);
    double e = params.g * h + 0.5 * dot(vel_, vel_);
    return std::clamp(1.0 - e / startHeightEnergy_, 0.0, 1.0);
}

bool Simulation::consumeEndEvent()
{
    bool e = endEvent_;
    endEvent_ = false;
    return e;
}

const char* Simulation::layoutName(int index)
{
    switch (index)
    {
    case 0: return "Без перешкод";
    case 1: return "Шаховий порядок";
    case 2: return "Зигзаг з бортиків";
    case 3: return "Воронка";
    default: return "?";
    }
}

const char* Simulation::stateName(SimState s)
{
    switch (s)
    {
    case SimState::Ready: return "Готово до запуску";
    case SimState::Running: return "Моделювання";
    case SimState::Paused: return "Пауза";
    case SimState::Finished: return "Фініш";
    case SimState::Stopped: return "Зупинка";
    }
    return "?";
}

// ---------------------------------------------------------------
// Запуск моделі без графіки з фіксованим кроком dt
// ---------------------------------------------------------------
RunResult runHeadless(int layout, const Params& params, double obstacleOffset, double dt)
{
    Simulation sim;
    sim.params = params;
    sim.loadLayout(layout);
    sim.shiftObstacles(obstacleOffset);
    sim.reset();
    sim.start();
    while (sim.state() == SimState::Running)
        sim.step(dt);

    RunResult r;
    r.state = sim.state();
    r.stats = sim.stats();
    r.finalSpeed = sim.speed();
    r.finalX = sim.position().x;
    r.finalY = sim.position().y;
    r.energyLoss = sim.energyLoss();
    return r;
}
