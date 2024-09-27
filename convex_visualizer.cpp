#include <ncurses.h>
#include <thread>
#include <sstream>
#include <stack>
#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <random>
#include <thread>
#include <functional>
#include <cmath>

#define GREEN 1
#define RED 2
#define YELLOW 3
#define MAGENTA 4
#define ORANGE 6
#define CYAN 5
#define UP 1
#define DOWN 2
#define RIGHT 3
#define LEFT 4

using namespace std;
struct Input {
    int num, maxX, maxY;
};

void generate_file(unsigned long len, unsigned long max_x, unsigned long max_y);
void generate_input(std::vector<Input> sizes);
std::vector<std::vector<long>> benchmark_graham(std::vector<Input> sizes, int runs);
std::vector<std::vector<long>> benchmark_jarvis(std::vector<Input> sizes, int runs);
std::vector<std::vector<long>> benchmark_quick(std::vector<Input> sizes, int runs);

struct Point {
    int x;
    int y;
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};
class Button {
public: 
    int x, y, width, height;
    bool selected;
    int id;
    string text;
    Button* next;
    Button* prev;
    function<void()> action;
    Button(int X, int Y, int Width, int Height, const string& Text, int ID,
           std::function<void()>Action) 
        : x(X), y(Y), width(Width), height(Height), selected(false), text(Text),
          next(nullptr), prev(nullptr), id(ID), action(Action) {}

    void Select() {
        selected = true;
        for (char& c: text) {
            c = toupper(c);
        }
    }
    void Deselect() {
        selected = false;
        for (char& c: text) {
            c = tolower(c);
        }
    }
    void Exec() {
        action();
    }
};

class Cursor {
public:
    int x, y, max_x, max_y, min_x, min_y;
    int cost;
    bool enabled;
    char icon;
    int type;
    Cursor(int X, int Y, int Max_X, int Max_Y, int Min_X, int Min_Y, char Icon,
           int Cost, int Type) :        x(X), y(Y), max_x(Max_X), max_y(Max_Y), 
                         min_x(Min_X), min_y(Min_Y), icon(Icon), enabled(true), 
                         cost(Cost), type(Type) {}

    void ChangeIcon(char newIcon) {
        icon = newIcon;
    }
    void Enable() {
        enabled = true;
    }
    void Disable() {
        enabled = false;
    }
    void Up() {
        if (y > min_y) {
            y--;
        }
    }
    void Down() {
        if (y < max_y) {
            y++;
        }
    }
    void Left() {
        if (x > min_x) {
            x--;
        }
    }
    void Right() {
        if (x < max_x) {
            x++;
        }
    }
};
class Window {
public:
    WINDOW * scr;
    int x, y, offset_x, offset_y, width, height;
    bool visible;
    Button* head;
    Button* tail;
    Button* selected;
    Cursor* cursor;
    std::vector<Point> points;
    std::vector<std::vector<Point>> hulls;
    int active_hull = 0;
    int active_scan = 1;
    bool playing_animation = false;
    bool playing_forward = true;
    int animation_speed = 250;
    Window(int X, int Y, int Width, int Height) 
        : x(X), y(Y), width(Width), height(Height), visible(true), 
          head(nullptr), tail(nullptr), selected(nullptr), cursor(nullptr), 
          offset_x(0), offset_y(0)
    {
        scr = newwin(height, width, y, x);
    }
    ~Window() {
        Button* current = head;
        Button* next;
        while (current) {
            next = current->next;
            delete current;
            if(next) {
                current = next; 
            }
            // need this if will run one more time double free ouchy :(
            else {
                break;
            }
        }
        // kill visual remnants and clenaup
        wborder(scr, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
        stopPlayingAnimation();
        clear();
        refresh();
        delwin(scr);
    }
    Point findRightmostPoint() {
        int highestIndex = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            if (points[i].x > points[highestIndex].x || 
               (points[i].x == points[highestIndex].x && points[i].y > points[highestIndex].y)) {
                highestIndex = i;
            }
        }
        return points[highestIndex];
    }
    Point findLeftmostPoint() {
        int lowestIndex = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            if (points[i].x < points[lowestIndex].x || 
               (points[i].x == points[lowestIndex].x && points[i].y < points[lowestIndex].y)) {
                lowestIndex = i;
            }
        }
        return points[lowestIndex];
    }
    void sortPointsLowest() {
        int lowestIndex = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            if (points[i].y > points[lowestIndex].y || 
               (points[i].y == points[lowestIndex].y && points[i].x < points[lowestIndex].x)) {
                lowestIndex = i;
            }
        }
        std::swap(points[0], points[lowestIndex]);
    }
    void sortPointsAngle(Point lowest) {
        std::sort(points.begin() + 1, points.end(), [&](const Point& a, const Point& b) {
            // Calculate polar angle with respect to lowest
            double angleA = std::atan2(lowest.y - a.y, a.x - lowest.x);
            double angleB = std::atan2(lowest.y - b.y, b.x - lowest.x);
            if (angleA != angleB) return angleA < angleB;

            // If angles are the same, sort by distance to lowest
            double distanceA = std::hypot(a.x - lowest.x, a.y - lowest.y);
            double distanceB = std::hypot(b.x - lowest.x, b.y - lowest.y);
            return distanceA < distanceB;
        });
    }
    int turnDirection(Point p1, Point p2, Point p3) { 
        return (p2.x - p1.x) * (-p3.y + p1.y) - (-p2.y + p1.y) * (p3.x - p1.x);
    }
    Point next_to_top(std::vector<Point> h) {
        Point top = h.back();
        h.pop_back();
        Point nextToTop = h.back();
        h.push_back(top);
        return nextToTop;
    }
    // TODO MAKE NOT SHIT
    string findLineChar(int dx, int dy, int sx, int sy) {
        string c;
        if(dy == 0) {
            c = "-";
        } else if (dx == 0) {
            c = "|";
        } else if ((dx > 0 && dy > 0 && sx == sy) || (dx < 0 && dy < 0 && sx == sy)) {
            c = "\\";
        } else {
            c = "/";
        }
        return c;

    }
    void refreshWindow() {
        clear();
        renderHull();
        renderPoints();
        renderHullsPoints();
        renderHud();
        refresh();
    }
    void renderHud() {
        wattron(scr, COLOR_PAIR(GREEN));
        write("select graham scan [1]", 2, height -3);
        write("select jarvis march [2]", 2, height-4);
        write("select quickhull [3]", 2, height-5);
        wattroff(scr, COLOR_PAIR(GREEN));

        wattron(scr, COLOR_PAIR(YELLOW));
        write("next frame [>]", 2, height -6);
        write("previous frame [<]", 2, height -7);
        write("play / pause animation [p]", 2, height -8);
        write("skip to beginning of animation [0]", 2, height-9);
        write("skip to end of animation [$]", 2, height-10);
        write("toggle animation direction [~]", 2, height-11);
        write("increase animation speed [+]", 2, height-12);
        write("decrease animation speed [-]", 2, height -13);
        wattroff(scr, COLOR_PAIR(YELLOW));
        wattron(scr, COLOR_PAIR(ORANGE));
        write("animation pause time (ms): " + std::to_string(animation_speed), 2, height-14);
        if(hulls.empty()) {
            write("frame: " + std::to_string(active_hull) + " / " + std::to_string(0), 2, height-15);
        }
        else {
            write("frame: " + std::to_string(active_hull) + " / " + std::to_string(hulls.size() -1), 2, height-15);
        }
        wattroff(scr, COLOR_PAIR(ORANGE));
    }
    // renders to screen line formed between point a and point b.
    void renderLineBetweenPoints(Point a, Point b){
        int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
        int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
        int err = dx + dy, e2;
        string c;
        for(;;) {
            write("#", a.x + offset_x, a.y + offset_y);
            if(a.x == b.x && a.y == b.y) break;
            e2 = 2 * err;
            if(e2 >= dy) { err += dy; a.x += sx; }
            if(e2 <= dx) { err += dx; a.y += sy; }

        }
    }

    void previousActiveHull() {
        if(active_hull > 0) {
            active_hull--;
        }
    }
    void nextActiveHull() {
        if(active_hull < hulls.size() -1) {
            active_hull++;
        }
    }
    // renders to screen hull formed by points in hull.
    void renderHull() {
        if(points.size() < 3)  return;

        for(int i = 0; i < hulls[active_hull].size() - 1; i++) {
            renderLineBetweenPoints(hulls[active_hull][i], hulls[active_hull][i + 1]);
        }
        renderLineBetweenPoints(hulls[active_hull][0], hulls[active_hull][hulls[active_hull].size()-1]);
    }
    bool playingAnimation() {
        return playing_animation;
    }
    void stopPlayingAnimation() {
        playing_animation = false;
    }
    void swapPlayingDirection() {
        if(playing_forward) {
            playing_forward = false;
        }
        else {
            playing_forward = true;
        }
    }
    void playHullAnimation() {
        playing_animation = true;
        std::thread([this]() {
            while(playing_animation) {
                std::this_thread::sleep_for(std::chrono::milliseconds(animation_speed));
                if(!playing_animation) {
                    break;
                }
                if(playing_forward) {
                    nextActiveHull();
                }
                else {
                    previousActiveHull();
                }

                refreshWindow();
            }
        }).detach();
    }
    void increaseAnimationSpeed() {
        if(animation_speed > 25) {
            animation_speed -= 25;
        }
        if(animation_speed < 25) {
            animation_speed = 25;
        }
    }
    void decreaseAnimationSpeed() {
        if(animation_speed < 1000) {
            animation_speed += 25;
        }
        if(animation_speed > 1000) {
            animation_speed = 1000;
        }
    }

    void setFrameStart() {
        active_hull = 0;
    }
    void setFrameEnd() {
        active_hull = hulls.size() - 1;
    }
    void performScan() {
        active_hull = 0;
        if(active_scan == 1) {
            grahamScan();
        }
        if(active_scan == 2) {
            jarvisMarch();
        }
        if(active_scan == 3) {
            quickHull();
        }
    }
    void setActiveScan(int n) {
        active_scan = n;
    }
    std::vector<Point> findLeftOfLine(std::vector<Point> p, Point leftmost, Point rightmost) {
        std::vector<Point> out;
        for(int i = 0; i < p.size(); i++) {
            if(turnDirection(leftmost, rightmost, p[i]) > 0) {
                out.push_back(p[i]);
            }
        }
        return out;
    }
    std::vector<Point> findRightOfLine(std::vector<Point> p, Point leftmost, Point rightmost) {
        std::vector<Point> out;
        for(int i = 0; i < p.size(); i++) {
            if(turnDirection(leftmost, rightmost, p[i]) < 0) {
                out.push_back(p[i]);
            }
        }
        return out;
    }
    void quickHull() {
        if(points.size() < 3) return;
        hulls.clear();
        std::vector<Point> h;

        Point leftmostPoint = findLeftmostPoint();
        h.push_back(leftmostPoint);
        hulls.push_back(h);

        Point rightmostPoint = findRightmostPoint();
        h.push_back(rightmostPoint);
        hulls.push_back(h);

        // Points on the right side
        std::vector<Point> s1 = findRightOfLine(points, leftmostPoint, rightmostPoint);
        // points on the left side
        std::vector<Point> s2 = findRightOfLine(points, rightmostPoint, leftmostPoint);

        // find hull of points to left
        findHull(s1, leftmostPoint, rightmostPoint);
        // find hull of points to right
        findHull(s2, rightmostPoint, leftmostPoint);
    }
    double pointLineDistance(Point p, Point q, Point r) {
        double num = abs((q.y - p.y) * r.x - (q.x - p.x) * r.y + q.x * p.y - q.y * p.x);
        double den = sqrt(pow(q.y - p.y, 2) + pow(q.x - p.x, 2));
        return num / den;
    }
    Point findFarthestAway(std::vector<Point> sk, Point leftmost, Point rightmost) {
        Point farthestPoint;
        double maxDistance = -1;

        for(int i = 0; i < sk.size(); i++) {
            double distance = pointLineDistance(leftmost, rightmost, sk[i]);
            if(distance > maxDistance) {
                maxDistance = distance;
                farthestPoint = sk[i];
            }
        }
        return farthestPoint;
    }
    void insertPointIntoHull(Point p, Point leftmost, Point rightmost) {
        std::vector<Point> h = hulls[hulls.size() -1];
        auto it = std::find(h.begin(), h.end(), leftmost);
        if(it != h.end()) {
            h.insert(it + 1, p);
            hulls.push_back(h);
        }

    }
    void findHull(std::vector<Point> sk, Point leftmost, Point rightmost) {
        if(sk.empty()) return;

        // find most extreme point from line
        Point farthestAway = findFarthestAway(sk, leftmost, rightmost);
        insertPointIntoHull(farthestAway, leftmost, rightmost);

        std::vector<Point> s1 = findRightOfLine(sk, leftmost, farthestAway);
        std::vector<Point> s2 = findRightOfLine(sk, farthestAway, rightmost);

        findHull(s1, leftmost, farthestAway);
        findHull(s2, farthestAway, rightmost);
    }
    void jarvisMarch() {
        if(points.size() < 3) return;
        hulls.clear();
        Point pointOnHull = findLeftmostPoint();
        Point leftmostPoint = pointOnHull;

        std::vector<Point> h;
        Point endpoint;
        do {
            h.push_back(pointOnHull);
            hulls.push_back(h);
            endpoint = points[0];
            for(int j = 0; j < points.size(); j++) {
                h.push_back(points[j]);
                hulls.push_back(h);
                h.pop_back();
                if( (pointOnHull.x == endpoint.x && pointOnHull.y == endpoint.y) || turnDirection(pointOnHull, endpoint, points[j]) > 0){
                    endpoint = points[j];
                }
            }
            pointOnHull = endpoint;
        } while (pointOnHull.x != leftmostPoint.x || pointOnHull.y != leftmostPoint.y) ;
        hulls.push_back(h);
    }
    void grahamScan() {
        if(points.size() < 3) return;
        hulls.clear();
        sortPointsLowest();
        sortPointsAngle(points[0]);

        std::vector<Point> h;
        for (size_t i = 0; i < points.size(); i++) {

            while (h.size() > 1 && turnDirection(next_to_top(h), h.back(), points[i]) <= 0) {
                h.pop_back();
            }
            h.push_back(points[i]);
            // essentially create a copy of each step in the algo into hulls
            hulls.push_back(h);
        }
    }
    void addPoint(int x = -1, int y = -1) {
        Point point;
        point.x = (x != -1) ? x : cursor->x - offset_x;
        point.y = (y != -1) ? y: cursor->y - offset_y;
        points.push_back(point);
    }
    void renderPoints() {
        for(Point p: points) {
            write("x", p.x + offset_x, p.y + offset_y);
        }
    }
    void renderHullsPoints() {
        if(points.size() < 3) return;
        wattron(scr, COLOR_PAIR(GREEN));
        for(Point p: hulls[active_hull]) {
            write("x", p.x + offset_x, p.y + offset_y);
        }
        wattroff(scr, COLOR_PAIR(GREEN));

    }
    void DrawVertical(int start_x, int start_y, int direction, int length, char icon) {
        for(int i = 0; i < length; i++) {
            mvwaddch(scr, start_y, start_x, icon);
            if(direction == DOWN) {
                start_y++;
            }
            if(direction == UP) {
                start_y++;
            }
        }
    }
    void DrawCursor() {
        mvwaddch(scr, cursor->y, cursor->x, cursor->icon);
        wattroff(scr ,COLOR_PAIR(GREEN));
        wattroff(scr, COLOR_PAIR(RED));
        wattroff(scr, COLOR_PAIR(YELLOW));
    }
    void KillCursor() {
        mvwaddch(scr, cursor->y, cursor->x, ' ');
    }
    void CursorMove(int direction) {
        /* remove the old spot of the cursor */
        KillCursor();
        if(direction == UP) {
            cursor->Up();
        }
        if(direction == DOWN) {
            cursor->Down();
        }
        if(direction == LEFT) {
            cursor->Left();
        }
        if(direction == RIGHT) {
            cursor->Right();
        }
    }
    void AddButton(int x, int y, int width, int height, string text, int id, 
                   std::function<void()>action) {
        if(!head) {
            head = new Button(x, y, width, height, text, id, action);
            tail = head;
            selected = head;
        } else {
             Button* temp = new Button(x, y, width, height, text, id, action);
             // make old tails next point to new button, make new tails prev point to previous tail.
             tail->next = temp;
             temp->prev = tail;
             // update tail to point to new tail, select it.
             tail = temp;
             selected = temp;
        }
        drawButton(selected);
    }
    void clearPoints() {
        active_hull = 0;
        points.clear();
        hulls.clear();
    }
    void SelectButton(int id) {
        Button* node = head;
        while(1) {
            if (node->id == id) {
                selected = node;
                selected->Select();
                drawButton(selected);
                return;
            }
            else {
                if(!node->next) {
                    return;
                }
                node = node->next;
            }
        }
    }
    void SelectNextButton() {
        if(!selected->next) {
            selected = head;
        }
        else {
            selected = selected->next;
        }
        if(selected == head) {
            tail->Deselect();
            drawButton(tail);
        }
        else {
            selected->prev->Deselect();
            drawButton(selected->prev);
        }
        selected->Select();
        drawButton(selected);
    }

    void SelectPrevButton() {
        if(!selected->prev) {
            selected = tail;
        }
        else {
            selected = selected->prev;
        }
        if(selected == tail) {
            head->Deselect();
            drawButton(head);
        }
        else {
            selected->next->Deselect();
            drawButton(selected->next);
        }
        selected->Select();
        drawButton(selected);
    }
    void drawButton(Button* button) {
        if(button->selected) wattron(scr, COLOR_PAIR(GREEN));
        mvwprintw(scr, button->y, button->x,button->text.c_str());
        if(button->selected) wattroff(scr, COLOR_PAIR(GREEN));
    }
    void refresh() {
        wrefresh(scr);
    }
    void clear() {
        wclear(scr);
    }
    void write(string text, int x, int y) {
        mvwprintw(scr, y, x, text.c_str());
    }
    void drawBox() {
        box(scr, 0, 0);
    }
    int grabChar() {
        int pressed = wgetch(scr);
        return pressed;
    }
    
};
void play(Window* prev_win);
void pre_interactive(Window* prev_win);
void start(Window* prev_win);
void finish(Window* prev_win);

void finish(Window* prev_win) {
    delete prev_win;
    endwin();
    exit(0);
}
void benchmark(Window* prev_win) {
    if(prev_win) delete prev_win;

    Window *play = new Window(1, 0, COLS-1, LINES);
    keypad(play->scr, TRUE);
    play->drawBox();

    // TODO loading screen
    // loading bar on bottom of screen
    // perform timing on graham, jarvis, quick.
    // loading bar updates for each garaham jarvis and quick
    // at each step write to screen step like "Analysis on Graham Scan size n"
    // once quick finishes loading bar and text is cleared.

    // a vector for each input size, in that vector a vector for each algorithm, in that vector a vector for each part of each algorithm.
    std::vector<Input> sizes = {
        {100, 150, 70},
        {500, 150, 150},
        {1000, 250, 250},
        {5000, 500, 500},
        {10000, 1000, 1000},
        {30000, 1500, 1500},
        {75000, 4000, 4000},
        {100000, 5000, 5000},
//        {150000, 30000, 30000},
//        {200000, 65000, 65000},
    };

    int runs_per_file = 3;
    int runs = 5;
    std::vector<std::vector<std::vector<long>>> durations(3, std::vector<std::vector<long>>(sizes.size(), std::vector<long>(4, 0)));

    for(int i = 0; i < runs; i++) {
        generate_input(sizes);
        std::vector<std::vector<long>> gra = benchmark_graham(sizes, runs_per_file);
        std::vector<std::vector<long>> jar = benchmark_jarvis(sizes, runs_per_file);
        std::vector<std::vector<long>> qui = benchmark_quick(sizes, runs_per_file);
        for(int j = 0; j < sizes.size(); j++){
            for(int k = 0; k < 4; k++) {
                if(k < 2) {
                    durations[0][j][k] += gra[j][k];
                    durations[1][j][k] += jar[j][k];
                    durations[2][j][k] += qui[j][k];
                }
                else if(k < 3) {
                    durations[0][j][k] += gra[j][k];
                    durations[2][j][k] += qui[j][k];
                }
                else if(k < 4) {
                   durations[2][j][k] += qui[j][k];
                }

            }
        }
        play->write("DONE WITH SCAN FOR FILE SET: " + std::to_string(i + 1), 2, i + 2);
        play->refresh();
    }
    for(int j = 0; j < sizes.size(); j++) {
        for(int k = 0; k < 4; k++) {
            if(k < 2) {
                durations[0][j][k] /= runs;
                durations[1][j][k] /= runs;
                durations[2][j][k] /= runs;
            }
            else if(k < 3) {
                durations[0][j][k] /= runs;
                durations[2][j][k] /= runs;
            }
            else if(k < 4) {
                durations[2][j][k] /= runs;
            }
        }
    }
    play->clear();

    play->drawBox();
    play->write("Total time in microseconds", 2, 2);
    play->write("Input size", 2, 3);
    for(int i = 0; i < sizes.size(); i++) {
        play->write(std::to_string(sizes[i].num), 2, i + 4);
    }
    play->DrawVertical(12, 3, DOWN, sizes.size() + 1, '|');
    play->write("Graham Scan", 13, 3);
    play->DrawVertical(26, 3, DOWN, sizes.size() + 1, '|');
    play->write("Jarvis March", 27, 3);
    play->DrawVertical(40, 3, DOWN, sizes.size() + 1, '|');
    play->write("Quick Hull", 41, 3);

    // GRAHAM TOTAL
    for(int i = 0; i < durations[0].size(); i++) {
        int total = 0;
        total += durations[0][i][0];
        total += durations[0][i][1];
        total += durations[0][i][2];
        play->write(std::to_string(total), 13, i + 4);
    }
    for(int i = 0; i < durations[1].size(); i++) {
        int total = 0;
        total += durations[1][i][0];
        total += durations[1][i][1];
        play->write(std::to_string(total), 27, i + 4);
    }
    for(int i = 0; i < durations[2].size(); i++) {
        int total = 0;
        total += durations[2][i][0];
        total += durations[2][i][1];
        total += durations[2][i][2];
        total += durations[2][i][3];
        play->write(std::to_string(total), 41, i + 4);
    }

    play->write("Graham Scan Detailed (microseconds)", 53, 2);
    play->write("Input size", 53, 3);
    play->DrawVertical(63, 3, DOWN, sizes.size() + 1, '|');
    play->write("Initial find", 64, 3);
    play->DrawVertical(76, 3, DOWN, sizes.size() + 1, '|');
    play->write("Sort", 77, 3);
    play->DrawVertical(88, 3, DOWN, sizes.size() + 1, '|');
    play->write("Stack Iteration", 89, 3);
    for(int i = 0; i < sizes.size(); i++) {
        play->write(std::to_string(sizes[i].num), 53, i + 4);
    }
    for(int i = 0; i < durations[0].size(); i++) {
        play->write(std::to_string(durations[0][i][0]), 64, i + 4);
        play->write(std::to_string(durations[0][i][1]), 77, i + 4);
        play->write(std::to_string(durations[0][i][2]), 89, i+ 4);
    }
    play->write("Jarvis March Detailed (microseconds)", 2, 25);
    play->write("Input size", 2, 26);
    play->DrawVertical(12, 26, DOWN, sizes.size() + 1, '|');
    play->write("Initial find", 13, 26);
    play->DrawVertical(25, 26, DOWN, sizes.size() + 1, '|');
    play->write("March", 26, 26);
    for(int i = 0; i < sizes.size(); i++) {
        play->write(std::to_string(sizes[i].num), 2, i + 27);
    }
    for(int i = 0; i < durations[0].size(); i++) {
        play->write(std::to_string(durations[1][i][0]), 13, i + 27);
        play->write(std::to_string(durations[1][i][1]), 26, i + 27);
    }
    play->write("Quick Hull Detailed (microseconds)", 53, 25);
    play->write("Input size", 53, 26);
    play->DrawVertical(63, 26, DOWN, sizes.size() + 1, '|');
    play->write("Initial find", 64, 26);
    play->DrawVertical(76, 26, DOWN, sizes.size() + 1, '|');
    play->write("Partition", 77, 26);
    play->DrawVertical(88, 26, DOWN, sizes.size() + 1, '|');
    play->write("First side", 89, 26);
    play->DrawVertical(99, 26, DOWN, sizes.size() + 1, '|');
    play->write("Second Side", 100, 26);
    for(int i = 0; i < sizes.size(); i++) {
        play->write(std::to_string(sizes[i].num), 53, i + 27);
    }
    for(int i = 0; i < durations[0].size(); i++) {
        play->write(std::to_string(durations[2][i][0]), 64, i + 27);
        play->write(std::to_string(durations[2][i][1]), 77, i + 27);
        play->write(std::to_string(durations[2][i][2]), 89, i+ 27);
        play->write(std::to_string(durations[2][i][3]), 100, i+ 27);
    }

//    play->write("Jarvis March Detailed", 50, 2);
//    play->write("Quick Hull Detailed", 50, 45);
//    for(int i = 0; i < durations[1].size(); i++) {
//            play->write("initial find: " + std::to_string(durations[1][i][0]), 35, (i * 3) + 0);
//            play->write("march: " + std::to_string(durations[1][i][1]), 35, (i * 3) + 1);
//    }
//    
//    for(int i = 0; i < durations[1].size(); i++) {
//            play->write("initial find: " + std::to_string(durations[2][i][0]), 65, (i * 5) + 0);
//            play->write("partition: " + std::to_string(durations[2][i][1]), 65, (i * 5) + 1);
//            play->write("first side: " + std::to_string(durations[2][i][2]), 65, (i * 5) + 2);
//            play->write("second side: " + std::to_string(durations[2][i][3]), 65, (i * 5) + 3);
//    }
    play->refresh();
    // TODO MAIN SCREEN
    // bottom of screen has bar, tabs with [1], [2],...,[4].
    // order of tabs is graham/jarvis/quick/comprehensive.
    //
    // graham/jarvis/quick tabs render in depth detail on each of the algorithms.
    // 
    // breakdown of time for each stage of algo:

    // GRAHAM {
    // 1. find most smallest point j in points. swap points[j] and points[0]. 
    // 2. sort points[1]..[n] in decreasing angle from points[0].              
    // 3. peform scan push and pop to stack hull for each point[1]..[n].      
    // }
    // JARVIS {
    // 1. find most leftmost point and set to pointOnHull.
    // 2. repeat push pointOnHull to hull, set endpoint to points[0], iterate through points[0]..[n]
    // if points[j] is more extreme left turn from pointOnHull and endpoint, make points[j] endpoint 
    // until endpoint becomes most leftmost point.
    //}
    // QUICK {
    // Find most extreme point(s) from points vector sk.
    // partition points into two distinct groups of size (n-2) where n is number of points in sk.
    // }
    //~~~
    //comprehensive screen
    // bar graph showing each input size 0..n. each input size has bar split into threes showing the 
    // time it took for each algo. Each of these three bars are split into the different major
    // components of each algorithm.
    //
    // also include some lines of like 
    // graham total: xxx part 1: xxx, ...
    // ...
    // quick total: xxx part 1: xxx, ...
    //
    // graham scan 30% faster than jarvis. 30% slower than quickhull.
    // ...
    // quickhull 180% faster than graham. 350% slower than graham.
    //
    //
    for(;;) {
        int pressed = play->grabChar();
        if(pressed == KEY_BACKSPACE || pressed == '\b') {
            start(play);
        }

        switch(pressed) {
            case '1':
                if(pressed == '1') {
                    play->setActiveScan(1);
                }
            case '2':
                if(pressed == '2') {
                    play->setActiveScan(2);
                }
            case '3':
                if(pressed == '3') {
                    play->setActiveScan(3);
                }
        }
    }
}
Point next_to_top(std::vector<Point> h) {
    Point top = h.back();
    h.pop_back();
    Point nextToTop = h.back();
    h.push_back(top);
    return nextToTop;
}
int turnDirection(Point p1, Point p2, Point p3) {
    return (p2.x - p1.x) * (-p3.y + p1.y) - (-p2.y + p1.y) * (p3.x - p1.x);
}
std::vector<Point> findRightofLine(std::vector<Point>& p, Point leftmost, Point rightmost) {
    std::vector<Point> out;
    for(int i = 0; i < p.size(); i++) {
        if(turnDirection(leftmost, rightmost, p[i]) < 0) {
            out.push_back(p[i]);
        }
    }
    return out;

}
double pointLineDistance(Point p, Point q, Point r) {
    double num = abs((q.y - p.y) * r.x - (q.x - p.x) * r.y + q.x * p.y - q.y * p.x);
    double den = sqrt(pow(q.y - p.y, 2) + pow(q.x - p.x, 2));
    return num / den;
}
Point findFarthestAway(std::vector<Point>& sk, Point leftmost, Point rightmost) {
    Point farthestPoint;
    double maxDistance = -1;

    for(int i = 0; i < sk.size(); i++) {
        double distance = pointLineDistance(leftmost, rightmost, sk[i]);
        if(distance > maxDistance) {
            maxDistance = distance;
            farthestPoint = sk[i];
        }
    }
    return farthestPoint;
}
void insertPointIntoHull(std::vector<Point>& h, Point p, Point leftmost, Point rightmost) {
    auto it = std::find(h.begin(), h.end(), leftmost);
    if(it != h.end()) {
        h.insert(it + 1, p);
    }

}
std::vector<Point> findRightOfLine(std::vector<Point>& p, Point leftmost, Point rightmost) {
    std::vector<Point> out;
    for(int i = 0; i < p.size(); i++) {
        if(turnDirection(leftmost, rightmost, p[i]) < 0) {
            out.push_back(p[i]);
        }
    }
    return out;
}
void findHull(std::vector<Point>& sk, std::vector<Point>& h, Point leftmost, Point rightmost) {
    if(sk.empty()) return;

    // find most extreme point from line
    Point farthestAway = findFarthestAway(sk, leftmost, rightmost);
    h.push_back(farthestAway);

    std::vector<Point> s1 = findRightOfLine(sk, leftmost, farthestAway);
    std::vector<Point> s2 = findRightOfLine(sk, farthestAway, rightmost);

    findHull(s1, h, leftmost, farthestAway);
    findHull(s2, h, farthestAway, rightmost);
}
std::vector<long> time_quick_input(std::vector<Point> input) {
    std::vector<long> times;

    // DURATION FOR FIND INITIAL POINTS
    auto start = std::chrono::steady_clock::now();
    std::vector<Point> h;
    int highestIndex = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].x > input[highestIndex].x || 
           (input[i].x == input[highestIndex].x && input[i].y > input[highestIndex].y)) {
               highestIndex = i;
        }
    }
    Point rightmostPoint = input[highestIndex];
    int lowestIndex = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].x < input[lowestIndex].x || 
           (input[i].x == input[lowestIndex].x && input[i].y < input[lowestIndex].y)) {
               lowestIndex = i;
        }
    }
    Point leftmostPoint = input[lowestIndex];
    h.push_back(leftmostPoint);
    h.push_back(rightmostPoint);
    auto stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    // DURATION FOR PARTITION
    start = std::chrono::steady_clock::now();
    std::vector<Point> s1 = findRightofLine(input, leftmostPoint, rightmostPoint);
    std::vector<Point> s2 = findRightofLine(input, rightmostPoint, leftmostPoint);
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    std::ofstream debugFile("debug", std::ios::app);
    if(debugFile.is_open()){
        debugFile << "IN INITIAL CALL" << std::endl;;
        debugFile << "Left size is: " << s1.size() << std::endl;
        debugFile << "Right size is: " << s2.size() << std::endl;
    }
    debugFile.close();

    // DURATION FOR LEFT RECURSION
    start = std::chrono::steady_clock::now();
    findHull(s1, h, leftmostPoint, rightmostPoint);
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    // DURATION FOR RIGHT RECURSION
    start = std::chrono::steady_clock::now();
    findHull(s2, h, rightmostPoint, leftmostPoint);
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    return times;
} 
std::vector<long> time_jarvis_input(std::vector<Point> input) {
    std::vector<long> times;

    // DURATION FOR FIND MOST LEFT
    auto start = std::chrono::steady_clock::now();
    int leftIndex = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].x < input[leftIndex].x || 
           (input[i].x == input[leftIndex].x && input[i].y < input[leftIndex].y)) {
            leftIndex = i;
        }
    }
    Point pointOnHull = input[leftIndex];
    Point leftmost = pointOnHull;
    auto stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    // DURATION FOR MARCH
    start = std::chrono::steady_clock::now();
    std::vector<Point> h;
    Point endpoint;
    do {
        h.push_back(pointOnHull);
        endpoint = input[0];
        for(int j = 0; j < input.size(); j++) {
            if( (pointOnHull.x == endpoint.x && pointOnHull.y == endpoint.y) ||
                turnDirection(pointOnHull, endpoint, input[j]) > 0) { 
                endpoint = input[j];
            }
        }
        pointOnHull = endpoint;
    } while (pointOnHull.x != leftmost.x || pointOnHull.y != leftmost.y) ;
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    return times;
} 
std::vector<long> time_graham_input(std::vector<Point> input) {
    std::vector<long> times;

    // DURATION FOR FIND MOST LOW
    auto start = std::chrono::steady_clock::now();
    size_t lowestIndex = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].y > input[lowestIndex].y || 
        (input[i].y == input[lowestIndex].y && input[i].x < input[lowestIndex].x)) {
            lowestIndex = i;
        }
    }
    std::swap(input[0], input[lowestIndex]);
    auto stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    // DURATION FOR SORT POINTS IN DECREASING ANGLE
    start = std::chrono::steady_clock::now();
    Point lowest = input[0];
    std::sort(input.begin() + 1, input.end(), [&](const Point& a, const Point& b) {
        auto deltaY_A = a.y - lowest.y;
        auto deltaX_A = a.x - lowest.x;
        auto deltaY_B = b.y - lowest.y;
        auto deltaX_B = b.x - lowest.x;

        // Compare by slope first to avoid unnecessary distance calculations
        auto slopeA = deltaY_A * (deltaX_B);
        auto slopeB = deltaY_B * (deltaX_A);
        if (slopeA != slopeB) return slopeA < slopeB;

        // If slopes are equal, then compare by distance squared
        auto distanceSqA = deltaX_A * deltaX_A + deltaY_A * deltaY_A;
        auto distanceSqB = deltaX_B * deltaX_B + deltaY_B * deltaY_B;
        return distanceSqA < distanceSqB;
    });
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    // DURATION FOR SCAN
    start = std::chrono::steady_clock::now();
    std::vector<Point> h;
    for(size_t i = 0; i < input.size(); i++) {
        while (h.size() > 1 && turnDirection(next_to_top(h), h.back(), input[i]) <= 0) {
            h.pop_back();
        }
        h.push_back(input[i]);
    }
    stop = std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count());

    return times;
} 
std::vector<Point> read_input_file(std::string s) {
    std::vector<Point> points;
    s = "./inputs/" + s;

    std::ifstream file(s);
    if(file.is_open()) {
        std::string line;
        int x,y;
        while(getline(file, line)) {
            std::istringstream iss(line);
            iss >> x;
            iss >> y;
            Point p;
            p.x = x;
            p.y = y;
            points.push_back(p);
        }
    }
    return points;

}
std::vector<std::vector<long>> benchmark_quick(std::vector<Input> sizes, int runs) {
    std::vector<std::vector<long>> durations;

    // TODO READ IN ALL THE INPUT FILES INTO ARRAY OF INPUTS
    std::vector<std::vector<Point>> inputs;
    for(int i = 0; i < sizes.size(); i++) {
        inputs.push_back(read_input_file(std::to_string(sizes[i].num)));
    }

    // TODO TIME IT TAKES FOR EACH INPUT
    std::vector<long> running(3, 0);
    for(std::vector<Point> input: inputs) {
        running[0] = 0;
        running[1] = 0;
        running[2] = 0;
        for(int i = 0; i < runs; i++) {
            std::vector<long> result = time_quick_input(input);
            running[0] += result[0];
            running[1] += result[1];
            running[2] += result[2];
        }
        running[0] = running[0] / runs;
        running[1] = running[1] / runs;
        running[2] = running[2] / runs;
        durations.push_back(running);
    }
    return durations;
}
std::vector<std::vector<long>> benchmark_jarvis(std::vector<Input> sizes, int runs) {
    std::vector<std::vector<long>> durations;

    // TODO READ IN ALL THE INPUT FILES INTO ARRAY OF INPUTS
    std::vector<std::vector<Point>> inputs;
    for(int i = 0; i < sizes.size(); i++) {
        inputs.push_back(read_input_file(std::to_string(sizes[i].num)));
    }

    // TODO TIME IT TAKES FOR EACH INPUT
    std::vector<long> running(3, 0);
    for(std::vector<Point> input: inputs) {
        for(int i = 0; i < runs; i++) {
            std::vector<long> result = time_jarvis_input(input);
            running[0] += result[0];
            running[1] += result[1];
            running[2] += result[2];
        }
        running[0] = running[0] / runs;
        running[1] = running[1] / runs;
        running[2] = running[2] / runs;
        durations.push_back(running);
    }
    return durations;
}
std::vector<std::vector<long>> benchmark_graham(std::vector<Input> sizes, int runs) {
    std::vector<std::vector<long>> durations;

    // TODO READ IN ALL THE INPUT FILES INTO ARRAY OF INPUTS
    std::vector<std::vector<Point>> inputs;
    for(int i = 0; i < sizes.size(); i++) {
        inputs.push_back(read_input_file(std::to_string(sizes[i].num)));
    }

    // TODO TIME IT TAKES FOR EACH INPUT
    std::vector<long> running(3, 0);
    for(std::vector<Point> input: inputs) {
        for(int i = 0; i < runs; i++) {
            std::vector<long> result = time_graham_input(input);
            running[0] += result[0];
            running[1] += result[1];
            running[2] += result[2];
        }
        running[0] = running[0] / runs;
        running[1] = running[1] / runs;
        running[2] = running[2] / runs;
        durations.push_back(running);
    }
    return durations;
}
 
void interactive(Window* prev_win, int num) {
    if(prev_win) delete prev_win;

    Window *play = new Window(1, 0, COLS-1, LINES);
    keypad(play->scr, TRUE);
    play->drawBox();

    play->cursor = new Cursor((COLS/2), (LINES/2), COLS-3, LINES-2, 1, 1, '@', 500, 1);
    if(num) {
        std::string input_file = "./inputs/" + std::to_string(num);
        std::ifstream file(input_file);
        if(file.is_open()) {
            std::string line;
            int x,y;
            while(getline(file, line)) {
                std::istringstream iss(line);
                Point point;
                iss >> x;
                iss >> y;
                play->addPoint(x, y);
            }
        }
        file.close();
        play->renderPoints();
        play->performScan();
        play->renderHull();
        play->renderHullsPoints();
    }

    play->renderHud();
    play->DrawCursor();

    for(;;) {
        int pressed = play->grabChar();
        if(pressed == KEY_BACKSPACE || pressed == '\b') {
            play->stopPlayingAnimation();
            pre_interactive(play);
        }
        switch(pressed) {
            case 'h': 
                if(pressed == 'h') play->offset_x -= 5;
            case 'j': 
                if(pressed == 'j') play->offset_y -= 5;
            case 'k': 
                if(pressed == 'k') play->offset_y += 5;
            case 'l': 
                if(pressed == 'l') play->offset_x += 5;
            case '<':
                if(pressed == '<') play->previousActiveHull();
            case '>':
                if(pressed == '>') play->nextActiveHull();
            case ' ':
                if(pressed == ' ') {
                    play->addPoint();
                    play->performScan();
                }
            case 'p':
                if(pressed == 'p') {
                    if(play->playingAnimation()) {
                        play->stopPlayingAnimation();
                    }
                    else {
                        play->playHullAnimation();
                    }
                }
            case '$':
                if(pressed == '$') {
                        play->setFrameEnd();
                    }
            case '0':
                if(pressed == '0') {
                        play->setFrameStart();
                    }
            case '-':
                if(pressed == '-') play->decreaseAnimationSpeed();
            case '+': 
                if(pressed == '+') play->increaseAnimationSpeed();
            case '~': 
                if(pressed == '~') play->swapPlayingDirection();
            case '1':
                if(pressed == '1') {
                    play->setActiveScan(1);
                    play->performScan();
                }
            case '2':
                if(pressed == '2') {
                    play->setActiveScan(2);
                    play->performScan();
                }
            case '3':
                if(pressed == '3') {
                    play->setActiveScan(3);
                    play->performScan();
                }
            default:
                play->refreshWindow();
        }
        // cursor keys
        switch(pressed) {
            case KEY_UP:
                if(pressed == KEY_UP) play->CursorMove(UP);
            case KEY_DOWN:
                if(pressed == KEY_DOWN) play->CursorMove(DOWN);
            case KEY_LEFT:
                if(pressed == KEY_LEFT) play->CursorMove(LEFT);
            case KEY_RIGHT:
                if(pressed == KEY_RIGHT) play->CursorMove(RIGHT);
            default:
                play->refreshWindow();
                play->DrawCursor();
        }
    }
}
void pre_interactive(Window* prev_win) {
    if(prev_win) delete prev_win;

    Window *play = new Window(1, 0, COLS-1, LINES);
    keypad(play->scr, TRUE);

    std::vector<Input> sizes = {
        {25, 30, 15},
        {50, 100, 35},
        {100, 150, 70},
        {1000, 250, 150},
        {10000, 1000, 400},
        {100000, 4000, 2000}
    };
    play->AddButton((COLS/2)-(11/2), LINES/2, 20, 1, "0", 1, [=]() 
                                                               { interactive(play, 0);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -2, 20, 1, "25", 1, [=]() 
                                                               { interactive(play, 25);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -4, 20, 1, "50", 1, [=]() 
                                                               { interactive(play, 50);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -6, 20, 1, "100", 1, [=]() 
                                                               { interactive(play, 100);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -8, 20, 1, "1000", 1, [=]() 
                                                               { interactive(play, 1000);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -10, 20, 1, "10000", 1, [=]() 
                                                               { interactive(play, 10000);});
    play->AddButton((COLS/2)-(11/2), LINES/2 -12, 20, 1, "100000", 1, [=]() 
                                                               { interactive(play, 100000);});
    play->AddButton((COLS/2)-(9/2) - 1, (LINES/2) -14, 20, 1, "rebuild input files", 1, [=]() 
                                                               { generate_input(sizes);});

    play->SelectButton(1);
    play->drawBox();

    for(;;) {
        int pressed = play->grabChar();
        if(pressed == KEY_BACKSPACE || pressed == '\b') {
            start(play);
        }
        if(pressed == KEY_UP) {
            play->SelectPrevButton();
        }
        if(pressed == KEY_DOWN) {
            play->SelectNextButton();
        }
        if(pressed == KEY_ENTER || pressed == '\n') {
            play->selected->Exec();
        }
    }
}
void start(Window* prev_win) {
    if(prev_win) delete prev_win;

    Window *start = new Window(1, 0, COLS-1, LINES);
    keypad(start->scr, TRUE);
    start->AddButton((COLS/2)-(9/2), (LINES/2) -2, 20, 1, "interactive", 1, [=]() 
                                                                    { pre_interactive(start);});

    start->AddButton((COLS/2)-(9/2), (LINES/2) -4, 20, 1, "benchmark", 1, [=]() 
                                                                    { benchmark(start);});
    start->drawBox();
    start->SelectButton(1);

    for(;;) {
        int pressed = start->grabChar();
        if(pressed == KEY_UP) {
            start->SelectPrevButton();
        }
        if(pressed == KEY_DOWN) {
            start->SelectNextButton();
        }
        if(pressed == KEY_ENTER || pressed == '\n') {
            start->selected->Exec();
        }
        if(pressed == KEY_BACKSPACE || pressed == '\b') {
            finish(start);
        }
    }
}

void generate_input(std::vector<Input> sizes)  {
    for(Input num: sizes) {
        generate_file(num.num, num.maxX, num.maxY);
    }
}
void generate_file(unsigned long len, unsigned long max_x, unsigned long max_y) {
    std::string file_path = "./inputs/" + std::to_string(len);
    std::ofstream outfile(file_path);

    std::random_device rd;
    std::mt19937 gen(rd()); 
    std::uniform_int_distribution<> distribX(1, max_x); 
    std::uniform_int_distribution<> distribY(1, max_y); 
    int random_numberX;
    int random_numberY;

    if(outfile.is_open()) {
        for(int i=0; i < len; i++)   {
            random_numberX = distribX(gen);
            random_numberY = distribY(gen);
            outfile << random_numberX << " ";
            outfile << random_numberY << std::endl;
        }
    }
    outfile.close();
}
int main()
{
    // BOILER PLATE NCURSES
	initscr();
	raw();
	noecho();
    start_color();
    curs_set(0);

    // INIT COLORS
    init_color(9, 1000, 647, 0);
    init_pair(GREEN, COLOR_GREEN, COLOR_BLACK);
    init_pair(RED, COLOR_RED, COLOR_BLACK);
    init_pair(YELLOW, COLOR_YELLOW, COLOR_BLACK);
    init_pair(MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CYAN, COLOR_CYAN, COLOR_BLACK);
    init_pair(ORANGE, 9, COLOR_BLACK);

    // from start rest of program is called
    start(nullptr);
}
