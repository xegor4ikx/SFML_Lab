// ============================================================
// Лабораторна робота №5. Варіант 23
// Рух кулі похилою поверхнею з перешкодами
// Виконав: Силін Єгор, група ФІТ 3-9
//
// physics.h - фізична модель задачі.
// Модуль не залежить від SFML: він лише обчислює стан системи,
// а відображенням займається main.cpp.
// ============================================================
#pragma once

#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Двовимірний вектор у координатах площини (метри).
// Вісь X - поперек схилу (вправо), вісь Y - вздовж схилу (вниз).
// ---------------------------------------------------------------
struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double length(Vec2 a) { return std::sqrt(dot(a, a)); }

// Тип перешкоди
enum class ObstacleType
{
    Peg,    // круглий кілок
    Wall    // бортик (відрізок з товщиною)
};

// Перешкода на площині
struct Obstacle
{
    ObstacleType type = ObstacleType::Peg;
    Vec2 a;               // центр кілка або початок бортика
    Vec2 b;               // кінець бортика (для кілка не використовується)
    double radius = 0.1;  // радіус кілка або половина товщини бортика
};

// Стан моделювання
enum class SimState
{
    Ready,      // куля на старті, моделювання не запущене
    Running,    // йде моделювання
    Paused,     // призупинено
    Finished,   // куля дійшла до нижнього краю площини
    Stopped     // куля зупинилася (тертя або перешкода не дають рухатися)
};

// Параметри фізичної моделі, які змінює користувач
struct Params
{
    double angleDeg = 25.0;     // кут нахилу площини, градуси
    double mu = 0.05;           // коефіцієнт тертя ковзання
    double restitution = 0.70;  // коефіцієнт відновлення при зіткненні
    double g = 9.81;            // прискорення вільного падіння, м/с²
};

// Накопичені показники одного запуску
struct Stats
{
    double time = 0.0;      // час руху, с
    int collisions = 0;     // кількість ударів об перешкоди та борти
    double path = 0.0;      // пройдений шлях, м
    double maxSpeed = 0.0;  // максимальна швидкість, м/с
};

// ---------------------------------------------------------------
// Модель: куля на похилій площині з перешкодами
// ---------------------------------------------------------------
class Simulation
{
public:
    static constexpr double kWidth = 6.4;         // ширина площини, м
    static constexpr double kLength = 7.0;        // довжина площини вздовж схилу, м
    static constexpr double kBallRadius = 0.15;   // радіус кулі, м
    static constexpr double kPegRadius = 0.12;    // радіус нового кілка, м
    static constexpr double kFixedStep = 1.0 / 240.0;   // крок інтегрування, с
    static constexpr double kMaxTime = 60.0;      // обмеження часу одного запуску, с

    Params params;

    Simulation();

    // Керування моделюванням
    void reset();           // повернути кулю на старт (повторний запуск)
    void start();           // запустити або продовжити
    void pause();           // призупинити
    void toggleRun();       // Space: старт / пауза / новий запуск

    // Перешкоди
    void loadLayout(int index);     // завантажити один із готових макетів
    void shiftObstacles(double dx); // зсунути всі перешкоди вздовж осі X
    void moveObstacle(int index, Vec2 delta);
    void addPeg(Vec2 center);
    void removeObstacle(int index);
    int obstacleAt(Vec2 point) const;  // індекс перешкоди під точкою або -1

    // Чисельне моделювання
    void advance(double frameDt);   // час кадру dt -> ціле число кроків kFixedStep
    void step(double dt);           // один крок методу Ейлера

    // Отримання стану
    SimState state() const { return state_; }
    const Vec2& position() const { return pos_; }
    const Vec2& velocity() const { return vel_; }
    const Stats& stats() const { return stats_; }
    const std::vector<Obstacle>& obstacles() const { return obstacles_; }
    int layout() const { return layout_; }
    double offset() const { return offset_; }

    double speed() const { return length(vel_); }
    double freeAcceleration() const;  // g(sin α − μ cos α) - прискорення без зіткнень
    bool canStartMoving() const;      // чи виконується умова tg α > μ
    double energyLoss() const;        // частка втраченої механічної енергії (0..1)

    // Подія завершення запуску (для таблиці результатів)
    bool consumeEndEvent();

    static int layoutCount() { return 4; }
    static const char* layoutName(int index);
    static const char* stateName(SimState s);

private:
    void collideWithObstacle(const Obstacle& ob);
    void collideWithPoint(Vec2 closest, double minDist);
    void collideWithWalls();

    Vec2 pos_;
    Vec2 vel_;
    Vec2 startPos_{kWidth / 2.0, 0.45};
    SimState state_ = SimState::Ready;
    Stats stats_;
    std::vector<Obstacle> obstacles_;
    int layout_ = 1;
    double offset_ = 0.0;
    double startHeightEnergy_ = 0.0;

    // Для визначення зупинки кулі
    Vec2 anchorPos_;
    double anchorTime_ = 0.0;
    bool endEvent_ = false;

    // Накопичувач часу для фіксованого кроку моделювання
    double accumulator_ = 0.0;
};

// Результат одного запуску без графіки (для обчислювального експерименту)
struct RunResult
{
    SimState state = SimState::Ready;
    Stats stats;
    double finalSpeed = 0.0;
    double finalX = 0.0;
    double finalY = 0.0;
    double energyLoss = 0.0;
};

RunResult runHeadless(int layout, const Params& params, double obstacleOffset, double dt);

// Обчислювальний експеримент: серії запусків, таблиця в консолі та CSV-файл
int runExperiment(const std::string& csvPath);
