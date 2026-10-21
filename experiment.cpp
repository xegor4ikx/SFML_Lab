// ============================================================
// experiment.cpp - обчислювальний експеримент (варіант 23)
//
// Запуск:  SFML_Lab --experiment [файл.csv]
// Програма без відкриття вікна виконує серії запусків тієї самої
// фізичної моделі (physics.cpp) з різними параметрами,
// виводить таблиці в консоль і зберігає всі результати у CSV.
// ============================================================
#include "physics.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDt = Simulation::kFixedStep;  // основний крок інтегрування 1/240 с

    struct Row
    {
        std::string series;
        int layout;
        Params params;
        double offset;
        double dt;
        RunResult result;
        double tAnalytic;   // аналітичний час (лише для макета без перешкод), -1 якщо немає
        double vAnalytic;
    };

    // Аналітичний розв'язок для руху без перешкод:
    // a = g(sin α − μ cos α),  t = sqrt(2D / a),  v = a·t
    void analytic(const Params& p, double& t, double& v)
    {
        double alpha = p.angleDeg * kPi / 180.0;
        double a = p.g * (std::sin(alpha) - p.mu * std::cos(alpha));
        double distance = Simulation::kLength - Simulation::kBallRadius - 0.45;
        if (a <= 0.0)
        {
            t = -1.0;
            v = 0.0;
            return;
        }
        t = std::sqrt(2.0 * distance / a);
        v = a * t;
    }

    Row run(const std::string& series, int layout, Params p, double offset, double dt)
    {
        Row row{series, layout, p, offset, dt, runHeadless(layout, p, offset, dt), -1.0, 0.0};
        if (layout == 0)
            analytic(p, row.tAnalytic, row.vAnalytic);
        return row;
    }

    std::string stateCode(SimState s)
    {
        return s == SimState::Finished ? "finish" : "stop";
    }

    std::string num(double v, int prec)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(prec) << v;
        return os.str();
    }

    void printHeader(const std::string& title)
    {
        std::cout << "\n" << title << "\n";
        std::cout << " layout  angle    mu   offset      dt   state    t,s   hits  path,m  v,m/s   x,m   y,m  loss%  t_an,s\n";
        std::cout << " ------------------------------------------------------------------------------------------------\n";
    }

    void printRow(const Row& r)
    {
        const RunResult& res = r.result;
        std::cout << std::setw(7) << r.layout
                  << std::setw(7) << num(r.params.angleDeg, 1)
                  << std::setw(6) << num(r.params.mu, 2)
                  << std::setw(9) << num(r.offset, 2)
                  << std::setw(8) << num(r.dt, 4)
                  << std::setw(8) << stateCode(res.state)
                  << std::setw(7) << num(res.stats.time, 3)
                  << std::setw(7) << res.stats.collisions
                  << std::setw(8) << num(res.stats.path, 2)
                  << std::setw(7) << num(res.finalSpeed, 2)
                  << std::setw(6) << num(res.finalX, 2)
                  << std::setw(6) << num(res.finalY, 2)
                  << std::setw(7) << num(res.energyLoss * 100.0, 1)
                  << std::setw(8) << (r.tAnalytic > 0 ? num(r.tAnalytic, 3) : std::string("-"))
                  << "\n";
    }
}

int runExperiment(const std::string& csvPath)
{
    std::vector<Row> rows;
    Params base;
    base.angleDeg = 25.0;
    base.mu = 0.05;

    // Серія 1. Вплив коефіцієнта тертя для кожного макета перешкод
    printHeader("Series 1: friction mu (angle 25 deg), all obstacle layouts");
    for (int layout = 0; layout < Simulation::layoutCount(); ++layout)
    {
        for (int i = 0; i <= 10; ++i)
        {
            Params p = base;
            p.mu = 0.05 * i;
            rows.push_back(run("mu", layout, p, 0.0, kDt));
            printRow(rows.back());
        }
    }

    // Серія 2. Вплив розташування перешкод: зсув усього поля перешкод по X
    printHeader("Series 2: obstacle offset (angle 25 deg, mu 0.05)");
    for (int layout : {1, 3})
    {
        for (int i = -8; i <= 8; ++i)
        {
            rows.push_back(run("offset", layout, base, 0.05 * i, kDt));
            printRow(rows.back());
        }
    }

    // Серія 3. Вплив кута нахилу; для макета без перешкод - порівняння з аналітикою
    printHeader("Series 3: angle (mu 0.10)");
    for (int layout : {0, 1})
    {
        for (int angle = 5; angle <= 45; angle += 5)
        {
            Params p = base;
            p.mu = 0.10;
            p.angleDeg = angle;
            rows.push_back(run("angle", layout, p, 0.0, kDt));
            printRow(rows.back());
        }
    }

    // Серія 4. Вплив кроку часу dt на результат чисельного моделювання
    printHeader("Series 4: time step dt (angle 25 deg, mu 0.05)");
    for (int layout : {0, 1, 2})
    {
        for (double dt : {1.0 / 30, 1.0 / 60, 1.0 / 120, 1.0 / 240, 1.0 / 480, 1.0 / 1000})
        {
            rows.push_back(run("dt", layout, base, 0.0, dt));
            printRow(rows.back());
        }
    }

    // Серія 5. Узагальнення: для кожного макета і μ - 17 запусків із різним
    // зсувом перешкод (-0,40 ... +0,40 м). Рух кулі серед перешкод дуже
    // чутливий до початкових умов, тому порівнюємо середні значення.
    std::cout << "\nSeries 5: mean over 17 obstacle offsets (angle 25 deg)\n";
    std::cout << " layout    mu  finished%  mean t,s  mean hits  mean path,m  mean loss%\n";
    std::cout << " ---------------------------------------------------------------------\n";
    for (int layout = 1; layout < Simulation::layoutCount(); ++layout)
    {
        for (int m = 0; m <= 9; ++m)
        {
            Params p = base;
            p.mu = 0.05 * m;
            int finished = 0, hits = 0;
            double sumT = 0.0, sumPath = 0.0, sumLoss = 0.0;
            const int n = 17;
            for (int i = -8; i <= 8; ++i)
            {
                rows.push_back(run("grid", layout, p, 0.05 * i, kDt));
                const RunResult& res = rows.back().result;
                hits += res.stats.collisions;
                sumPath += res.stats.path;
                if (res.state == SimState::Finished)
                {
                    ++finished;
                    sumT += res.stats.time;
                    sumLoss += res.energyLoss;
                }
            }
            std::cout << std::setw(7) << layout << std::setw(6) << num(p.mu, 2)
                      << std::setw(11) << num(100.0 * finished / n, 0)
                      << std::setw(10) << (finished ? num(sumT / finished, 2) : std::string("-"))
                      << std::setw(11) << num(double(hits) / n, 1)
                      << std::setw(13) << num(sumPath / n, 2)
                      << std::setw(12) << (finished ? num(100.0 * sumLoss / finished, 1) : std::string("-"))
                      << "\n";
        }
    }

    // Збереження в CSV
    std::ofstream out(csvPath);
    if (!out)
    {
        std::cerr << "Cannot write " << csvPath << "\n";
        return 1;
    }
    out << "series,layout,layout_name,angle_deg,mu,restitution,offset_m,dt_s,state,"
           "time_s,collisions,path_m,v_final,x_final,y_final,v_max,energy_loss,t_analytic,v_analytic\n";
    for (const Row& r : rows)
    {
        const RunResult& res = r.result;
        out << r.series << ',' << r.layout << ',' << Simulation::layoutName(r.layout) << ','
            << num(r.params.angleDeg, 1) << ',' << num(r.params.mu, 2) << ','
            << num(r.params.restitution, 2) << ',' << num(r.offset, 2) << ','
            << num(r.dt, 6) << ',' << stateCode(res.state) << ','
            << num(res.stats.time, 4) << ',' << res.stats.collisions << ','
            << num(res.stats.path, 4) << ',' << num(res.finalSpeed, 4) << ','
            << num(res.finalX, 4) << ',' << num(res.finalY, 4) << ','
            << num(res.stats.maxSpeed, 4) << ',' << num(res.energyLoss, 4) << ','
            << (r.tAnalytic > 0 ? num(r.tAnalytic, 4) : std::string("")) << ','
            << (r.tAnalytic > 0 ? num(r.vAnalytic, 4) : std::string("")) << '\n';
    }
    std::cout << "\nSaved " << rows.size() << " runs to " << csvPath << "\n";
    return 0;
}
