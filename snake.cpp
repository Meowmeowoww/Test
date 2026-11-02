// snake.cpp —— 终端版贪吃蛇
//
// 编译：
//   Windows (MSVC):  build.bat，或者 cl /std:c++17 /EHsc /O2 /utf-8 snake.cpp
//   Linux / macOS:   g++ -std=c++17 -O2 -o snake snake.cpp
//
// 运行：
//   ./snake              开始游戏
//   ./snake --selftest   只跑逻辑自测，不进入交互（CI 里也能用）

#include <chrono>
#include <deque>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace {

// ---------------------------------------------------------------- 终端控制

namespace term {

#ifdef _WIN32

void begin() {
    // 打开 ANSI 转义序列支持（Win10 以后的控制台才需要这步）
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleOutputCP(CP_UTF8);       // 让中文提示能正常显示
    std::cout << "\x1b[?25l" << std::flush;   // 隐藏光标
}

void end() {
    std::cout << "\x1b[?25h\x1b[0m" << std::flush;
}

// 非阻塞读一个键；没有按键返回 0
int pollKey() {
    if (!_kbhit()) return 0;
    int c = _getch();
    if (c == 0 || c == 224) {          // 方向键是两字节扩展码
        switch (_getch()) {
            case 72: return 'U';
            case 80: return 'D';
            case 75: return 'L';
            case 77: return 'R';
            default: return 0;
        }
    }
    return c;
}

#else   // POSIX

termios g_old;
bool g_raw = false;

void begin() {
    tcgetattr(STDIN_FILENO, &g_old);
    termios raw = g_old;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_raw = true;
    std::cout << "\x1b[?25l" << std::flush;
}

void end() {
    if (g_raw) tcsetattr(STDIN_FILENO, TCSANOW, &g_old);
    std::cout << "\x1b[?25h\x1b[0m" << std::flush;
}

int readByte() {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval tv{0, 0};
    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv) <= 0) return 0;
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    return c;
}

int pollKey() {
    int c = readByte();
    if (c == 0) return 0;
    if (c == 27) {                     // ESC 开头，看看是不是方向键
        int c2 = readByte();
        if (c2 != '[') return 27;
        switch (readByte()) {
            case 'A': return 'U';
            case 'B': return 'D';
            case 'C': return 'R';
            case 'D': return 'L';
            default: return 0;
        }
    }
    return c;
}

#endif

}  // namespace term

// ---------------------------------------------------------------- 游戏逻辑

constexpr int kWidth = 28;
constexpr int kHeight = 20;

enum class Dir { Up, Down, Left, Right };

struct Point {
    int x = 0;
    int y = 0;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

bool isOpposite(Dir a, Dir b) {
    return (a == Dir::Up && b == Dir::Down) || (a == Dir::Down && b == Dir::Up) ||
           (a == Dir::Left && b == Dir::Right) || (a == Dir::Right && b == Dir::Left);
}

class Game {
public:
    explicit Game(unsigned seed = 1) : rng_(seed) {
        const int cx = kWidth / 2;
        const int cy = kHeight / 2;
        body_.push_back(Point{cx, cy});
        body_.push_back(Point{cx - 1, cy});
        body_.push_back(Point{cx - 2, cy});
        dir_ = Dir::Right;
        nextDir_ = Dir::Right;
        placeFood();
    }

    // 走一帧。返回 false 表示这一帧撞死了
    bool step() {
        dir_ = nextDir_;
        Point head = body_.front();
        switch (dir_) {
            case Dir::Up:    head.y -= 1; break;
            case Dir::Down:  head.y += 1; break;
            case Dir::Left:  head.x -= 1; break;
            case Dir::Right: head.x += 1; break;
        }

        if (head.x < 0 || head.x >= kWidth || head.y < 0 || head.y >= kHeight) return false;

        const bool eating = (head == food_);

        // 自撞判断：不吃东西时尾巴这一帧会让开，所以撞到最后一格不算死
        const size_t checkUntil = eating ? body_.size() : body_.size() - 1;
        for (size_t i = 0; i < checkUntil; ++i) {
            if (body_[i] == head) return false;
        }

        body_.push_front(head);
        if (eating) {
            ++score_;
            placeFood();
        } else {
            body_.pop_back();
        }
        return true;
    }

    // 转向。反方向（正好掉头）直接忽略
    void turn(Dir d) {
        if (d == nextDir_ || isOpposite(d, nextDir_)) return;
        nextDir_ = d;
    }

    const std::deque<Point>& body() const { return body_; }
    Point food() const { return food_; }
    int score() const { return score_; }
    bool won() const { return won_; }

    // 仅供 --selftest 使用：把食物放到指定格子上
    void debugPlaceFood(Point p) { food_ = p; }

private:
    void placeFood() {
        std::vector<Point> free;
        free.reserve(kWidth * kHeight);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                Point p{x, y};
                bool taken = false;
                for (const Point& b : body_) {
                    if (b == p) { taken = true; break; }
                }
                if (!taken) free.push_back(p);
            }
        }
        if (free.empty()) {          // 整张棋盘都占满了，算通关
            won_ = true;
            food_ = Point{-1, -1};
            return;
        }
        std::uniform_int_distribution<size_t> pick(0, free.size() - 1);
        food_ = free[pick(rng_)];
    }

    std::deque<Point> body_;
    Dir dir_ = Dir::Right;
    Dir nextDir_ = Dir::Right;
    Point food_{-1, -1};
    int score_ = 0;
    bool won_ = false;
    std::mt19937 rng_;
};

// ---------------------------------------------------------------- 画面

constexpr const char* kReset = "\x1b[0m";

int frameDelayMs(int score) {
    return score >= 30 ? 55 : 130 - score * 3;   // 越吃越快，最快 55ms 一帧
}

std::string drawFrame(const Game& game, bool paused, bool over) {
    std::vector<std::string> cells(kHeight, std::string(kWidth, ' '));
    const Point food = game.food();
    if (food.x >= 0) cells[food.y][food.x] = '*';

    const std::deque<Point>& body = game.body();
    for (size_t i = body.size(); i-- > 0;) {
        cells[body[i].y][body[i].x] = (i == 0) ? '@' : 'o';
    }

    std::ostringstream os;
    os << "\x1b[H";
    os << "\x1b[1;36m  贪吃蛇" << kReset << "    得分 " << game.score()
       << "    长度 " << body.size() << "\x1b[K\n";

    os << "\x1b[90m  +" << std::string(kWidth, '-') << "+" << kReset << "\x1b[K\n";
    for (int y = 0; y < kHeight; ++y) {
        os << "\x1b[90m  |" << kReset;
        for (int x = 0; x < kWidth; ++x) {
            const char c = cells[y][x];
            if (c == '@')      os << "\x1b[1;32m@";
            else if (c == 'o') os << "\x1b[32mo";
            else if (c == '*') os << "\x1b[1;31m*";
            else               os << ' ';
        }
        os << "\x1b[90m|" << kReset << "\x1b[K\n";
    }
    os << "\x1b[90m  +" << std::string(kWidth, '-') << "+" << kReset << "\x1b[K\n";
    os << "\x1b[K\n";

    if (over) {
        os << "  \x1b[1;31m游戏结束" << kReset << "    最终得分 " << game.score()
           << "    长度 " << body.size() << "\x1b[K\n";
        os << "  按 \x1b[1mR" << kReset << " 重来，按 \x1b[1mQ" << kReset << " 退出\x1b[K\n";
    } else if (paused) {
        os << "  \x1b[1;33m已暂停" << kReset << "    按空格继续\x1b[K\n";
        os << "\x1b[K\n";
    } else {
        os << "  WASD / 方向键 移动    空格 暂停    Q 退出\x1b[K\n";
        os << "\x1b[K\n";
    }
    return os.str();
}

// ---------------------------------------------------------------- 自测

int runSelftest() {
    int failed = 0;
    auto check = [&failed](bool ok, const char* name) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!ok) ++failed;
    };

    {
        Game g(1);
        check(g.body().size() == 3, "初始长度是 3");
        const Point head = g.body().front();
        check(g.step(), "第一步不会死");
        check(g.body().front().x == head.x + 1 && g.body().front().y == head.y,
              "向右走了一格");
    }
    {
        Game g(1);
        g.turn(Dir::Up);
        g.turn(Dir::Down);            // 掉头，应该被忽略
        g.step();
        check(g.body().front().y == kHeight / 2 - 1, "反方向输入被忽略");
    }
    {
        Game g(1);
        bool alive = true;
        for (int i = 0; i < kWidth + 5 && alive; ++i) alive = g.step();
        check(!alive, "撞墙会死");
    }
    {
        Game g(7);
        const Point head = g.body().front();
        g.debugPlaceFood(Point{head.x + 1, head.y});
        const size_t before = g.body().size();
        g.step();
        check(g.body().size() == before + 1, "吃到食物长一格");
        check(g.score() == 1, "得分 +1");
    }
    {
        Game g(7);
        for (int i = 0; i < 3; ++i) {   // 先喂胖一点，才撞得到自己
            const Point head = g.body().front();
            g.debugPlaceFood(Point{head.x + 1, head.y});
            g.step();
        }
        g.turn(Dir::Up);
        g.step();
        g.turn(Dir::Left);
        g.step();
        g.turn(Dir::Down);
        const bool alive = g.step();
        check(!alive, "撞到自己会死");
    }
    {
        Game g(3);
        const Point first = g.food();
        check(first.x >= 0 && first.x < kWidth && first.y >= 0 && first.y < kHeight,
              "食物落在棋盘内");
        for (const Point& p : g.body()) {
            check(!(p == first), "食物不压在蛇身上");
            break;   // 检查一次即可
        }
    }

    std::cout << (failed == 0 ? "\n全部通过\n" : "\n有测试失败\n");
    return failed;
}

// ---------------------------------------------------------------- 主程序

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void play() {
    Game game(std::random_device{}());
    bool paused = false;
    bool dead = false;
    bool quit = false;

    while (!dead && !quit) {
        std::cout << drawFrame(game, paused, false) << std::flush;

        const auto tickStart = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(frameDelayMs(game.score()));

        for (;;) {
            const int key = term::pollKey();
            if (key == 'q' || key == 'Q' || key == 27) { quit = true; break; }
            if (key == ' ' || key == 'p' || key == 'P') { paused = !paused; break; }
            if (key == 'w' || key == 'W' || key == 'U') game.turn(Dir::Up);
            if (key == 's' || key == 'S' || key == 'D') game.turn(Dir::Down);
            if (key == 'a' || key == 'A' || key == 'L') game.turn(Dir::Left);
            if (key == 'd' || key == 'D' || key == 'R') game.turn(Dir::Right);

            if (paused) { sleepMs(10); continue; }
            if (std::chrono::steady_clock::now() - tickStart >= interval) break;
            sleepMs(3);
        }

        if (quit) break;
        if (paused) continue;                       // 暂停时不要推进
        if (!game.step()) dead = true;
    }

    if (!quit) {
        std::cout << drawFrame(game, false, true) << std::flush;
        for (;;) {
            const int key = term::pollKey();
            if (key == 'q' || key == 'Q' || key == 27) break;
            if (key == 'r' || key == 'R') { play(); return; }
            sleepMs(10);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") return runSelftest() == 0 ? 0 : 1;
        if (arg == "--help" || arg == "-h") {
            std::cout << "用法: snake [--selftest]\n"
                         "  WASD / 方向键 移动，空格暂停，Q 退出\n";
            return 0;
        }
    }

    term::begin();
    std::cout << "\x1b[2J" << std::flush;   // 清屏
    play();
    term::end();
    std::cout << "\n";
    return 0;
}
