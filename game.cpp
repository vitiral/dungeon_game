/*
 * Following tutorial from https://lazyfoo.net/tutorials/SDL
 *
 * I also just finished reading https://www.learncpp.com/, but didn't do many of
 * the "assignments".  Please note, I am COMPLETELY new to C++ although I have a
 * lot of experience with other languages which drew from or inspired various
 * C++ features (C, Java and Rust to name the main ones). So this code might
 * seem rather odd in some places -- I'm still learning!
 *
 * Since the code from the SDL tutorial is VERY messy, it provides a perfect
 * opportunity to hone my skills.  I'm applying C++ idioms including wrapper
 * classes for the C resource (i.e.  `Resource` class) and just common
 * programming best practices like no globals, etc.
 *
 * This file will grow to become a "single file" project. I don't plan on
 * splitting it up very much to make development easier. When I start making an
 * actual game I will do that kind of cleanup.
 *
 * ## Notes
 * https://lazyfoo.net/tutorials/SDL/index.php
 * - Lesson 10: you can make certain colors transparent.
 * - Lesson 11: how to clip out a sprite sheet
 * - Lesson 12: color modulation, great for early status affects!
 * - Lesson 13: alpha modulation for transparency
 * - Lesson 14: sprite movement
 * - Lesson 15: rotation
 *
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <iostream>
#include <cstdio>
#include <cassert>
#include <compare>
#include <set>
#include <vector>
#include <array>
#include <unordered_map>
// using uomap = unordered_map;


// There is no good debugging of events in SDL2, so we import this library.
#include "libs/evt2str/sdl_event_to_string.h"
#include "resource.h" // Resource and DEFER for wrapping C resources.
#include "test.h"

using namespace std;
using TimeMs = Uint32;

// *****************
// * SDL wrapper types and helper functions
using RWindow  = Resource<SDL_Window, SDL_DestroyWindow>;
using RRenderer = Resource<SDL_Renderer, SDL_DestroyRenderer>;
using RSurface = Resource<SDL_Surface, SDL_FreeSurface>;
using RTexture = Resource<SDL_Texture, SDL_DestroyTexture>;

// *****************
// * Game Objects

//Screen dimension constants
const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;
const TimeMs FRAME_LENGTH = 33; // 33ms, 30 fps

struct Loc;

struct Size {
  int w{}, h{};

  Size operator/ (const int r) const { return Size { w / r, h / r }; }
  bool operator==(Size r)  const { return w == r.w and w == r.w; }
};

TEST(size)
  Size sz = Size{10, 5};
  Size d2 = sz / 2;
  ASSERT_EQ(5, d2.w); ASSERT_EQ(2, d2.h);
END_TEST

class Timer {
public:
  string_view  m_name;
  TimeMs m_start;
  Timer(string_view name) : m_name{name} {
    m_start = SDL_GetTicks();
  }

  ~Timer() {
    TimeMs end = SDL_GetTicks();
    cout << "Timer " << m_name << " total=" << (end - m_start) << "ms\n";
  }
};

class Display {
  NO_COPY(Display);

public:
  Display() = default;

  Size        sz{SCREEN_WIDTH, SCREEN_HEIGHT};
  RWindow     window{};
  // RSurface    screen{};
  RRenderer   rend{};

  // RTexture    i_hello{};
  // RTexture    i_xOut{};
  // RTexture    i_png{};

  TimeMs frame{0};  // timeMs at start of frame

  bool init();
  RSurface optimize(RSurface& s, const std::string& path);
  RTexture loadTexture(const std::string& path);
  bool loadMedia();

  void frameDelay() {
    TimeMs now = SDL_GetTicks();
    TimeMs end = frame + FRAME_LENGTH;
    if(end > now) {
      SDL_Delay(end - now);
    }
    frame = SDL_GetTicks();
  }
};

struct Color {
  Uint8 r{}; Uint8 g{}; Uint8 b{}; Uint8 a{};
  Color(Uint8 r=0, Uint8 g=0, Uint8 b=0, Uint8 a=SDL_ALPHA_OPAQUE)
    : r{r}, g{g}, b{b}, a{a}
  {}

  void render(RRenderer& rend) {
    cout << "Rendering " << *this << '\n';
    SDL_SetRenderDrawColor(&*rend, r, g, b, a);
  }

  SDL_Color apply(SDL_Color c) {
    c.r = r;  c.g = g;  c.b = b;  c.a = a;
    return c;
  }

  friend ostream& operator<<(ostream& out, const Color& c);
};

ostream& operator<<(ostream& out, const Color& c) {
  return out << "Color("
             << static_cast<int>(c.r) << ", "
             << static_cast<int>(c.g) << ", "
             << static_cast<int>(c.b) << ", "
             << static_cast<int>(c.a) << ")";
}

// Bound val by [-abs:abs]
int bound(int abs, int val) {
  assert(abs >= 0);
  if(val > 0) return min(abs, val);
  return max(-abs, val);
}

TEST(bound)
  ASSERT_EQ(10, bound(10, 12));
  ASSERT_EQ(7,   bound(10, 7));
  ASSERT_EQ(-10, bound(10, -15));
  ASSERT_EQ(-5,  bound(10, -5));
END_TEST

struct Loc {
  int x{}, y{};

  bool operator==(Loc r)  const { return x == r.x and y == r.y; }
  Loc operator- ()        const { return Loc{-x, -y};           }
  Loc operator+ (Loc r)   const { return Loc{x+r.x, y+r.y};     }
  Loc operator- (Loc r)   const { return Loc{x-r.x, y-r.y};     }
  Loc operator/ (int r)   const { return Loc{x/r, y/r};     }
  Loc operator* (int r)   const { return Loc{x*r, y*r};     }

  Loc bound(int abs) { return Loc{::bound(abs, x), ::bound(abs, y)}; }
};

// Reduce magnitude of value, never crossing 0. Good for slowing down an entity.
int subMag(int val, int abs) {
  if(val > 0) {
    if(abs > val) return 0;
                  return val - abs;
  }
  if(abs > -val)  return 0;
                  return val + abs;
}

// updateVelocity
// dir: vector direction
// vel: current velocity
// acc: acceleration
// maxVel: maximum velocity
int updateVel(int dir, int vel, int acc, int maxVel) {
  if(dir) return bound(maxVel, vel + acc * dir);
  else    return subMag(vel, acc);
}

TEST(updateVel)
  const auto uv = updateVel;
  //        expect     dir     vel       acc      maxVel
  ASSERT_EQ(0,       uv(0,     10,       12,      15));
  ASSERT_EQ(0,       uv(0,    -10,       12,      15));
  ASSERT_EQ(12,      uv(1,     0,        12,      15));
  ASSERT_EQ(15,      uv(1,     12,       12,      15));
  ASSERT_EQ(-7,      uv(-1,    5,        12,      15));
END_TEST

struct Movement {
  Loc v{};    // velocity vector
  int a {3};  // acceleration per tick
  int maxV{15};

  void update(Loc dir) {
    assert(abs(dir.x) <= 1);
    assert(abs(dir.y) <= 1);
    v.x = updateVel(dir.x, v.x, a, maxV);
    v.y = updateVel(dir.y, v.y, a, maxV);
  }
};

class Game;

class Entity {
public:
  Uint64    id;
  Color     color{0xFF};
  Size      sz{50, 100};
  Loc       loc{0, 0};
  Movement  mv{};

  void render(Display& d, Game& g) {
    color.render(d.rend);
    SDL_Rect r = sdlRect(d, g);
    SDL_RenderFillRect(&*d.rend, &r);
    // auto v = sdlTriangle(d, g);
    // SDL_RenderGeometry(
    //   &*d.rend, /*texture=*/nullptr,
    //   v.data(), 3, nullptr, 0);
  }

  Loc move() {
    return loc + mv.v;
  }

  // Get the SDL Rectangle for the entity
  Loc           displayLoc(Display& d, Game& g);
  SDL_Rect      sdlRect(Display& d, Game& g);
  array<SDL_Vertex, 3> sdlTriangle(Display& d, Game& g);
};

class Controller {
public:
  Uint8 w{},  a{}, s{}, d{};
  Uint8 ml{}, mr{};        // mouse left/right
};

const int MAX_EVENTS = 256;

// Game State
class Game {
  Uint64 m_nextId{};
  Uint64 nextId() { return m_nextId++; }
public:
  unordered_map<Uint64, Entity> entities;
  Controller controller{};
  TimeMs loop {0};  // game loop number (incrementing)

  Loc    center{0, 0};
  // Entity e1{};
  // Entity e2{.color{0, 0xFF}, .loc{-100, -100}};
  Entity eBack{};

  Entity& player() { return entities[0u]; }

  Entity& newEntity() {
    auto id = nextId();
    entities[id] = {.id = id};
    return entities[id];
  }

  Entity& entity(Uint64 id) { return entities[id]; }
  void erase(Uint64 id) { entities.erase(id); }

  bool quit{};
  bool ctrl{};
  void mouseEvent(SDL_MouseButtonEvent& e, bool pressed);
  void keyEvent(SDL_KeyboardEvent& e, bool pressed);
};

void Game::mouseEvent(SDL_MouseButtonEvent& e, bool pressed) {
  Controller& c = controller;
  switch (static_cast<int>(e.button)) {
    case SDL_BUTTON_LEFT:  c.ml = pressed;
    case SDL_BUTTON_RIGHT: c.mr = pressed;
    default: return;
  }
}

void Game::keyEvent(SDL_KeyboardEvent& e, bool pressed) {
  if(e.repeat) return;
  cout << "Keydown: " << sdlEventToString(SDL_Event{.key = e}) << '\n';
  Controller& c = controller;
  Entity& p = player();
  switch(e.keysym.sym) {
    case SDLK_UP:    p.loc.y += 5; return;
    case SDLK_DOWN:  p.loc.y -= 5; return;
    case SDLK_LEFT:  p.loc.x -= 5; return;
    case SDLK_RIGHT: p.loc.x += 5; return;

    case SDLK_a:     c.a = pressed; assert(c.a <= 1); break;
    case SDLK_s:     c.s = pressed; assert(c.s <= 1); break;
    case SDLK_d:     c.d = pressed; assert(c.d <= 1); break;
    case SDLK_w:     c.w = pressed; assert(c.w <= 1); break;

    case SDLK_LCTRL:
    case SDLK_RCTRL: ctrl = pressed; return;
    case SDLK_c:
      if(ctrl) { cout << "Got Cntrl+C\n"; quit = true; }
      return;
    default: cout << "  ... Hit default\n";
  }
}

// Returns Loc for the display. Note that this is (by default) for a rectangle
// being rendered on the display, where (x,y) is the "top left" and (x+w, y+h)
// is the "bottom right"
Loc Entity::displayLoc(Display& d, Game& g) {
  Loc rel = loc - g.center; // relative location to center
  rel.y = -rel.y;           // reverse so that +y goes down (SDL coordinates)
  rel = rel - Loc{sz.w / 2, sz.h / 2};      // get "top-left" position
  return rel + Loc{d.sz.w / 2, d.sz.h / 2}; // Update coordinates for SDL_Rect
}

SDL_Rect Entity::sdlRect(Display& d, Game& g) {
  Loc rel = displayLoc(d, g);
  return (SDL_Rect) {rel.x, rel.y, sz.w, sz.h};
};

array<SDL_Vertex, 3> Entity::sdlTriangle(Display& d, Game& g) {
  Loc rel = displayLoc(d, g);
  array<SDL_Vertex, 3> out{};
  // Center (top)
  out[0].position.x = rel.x + sz.w / 2;
  out[0].position.y = rel.y;

  // Left
  out[1].position.x = rel.x;
  out[1].position.y = rel.y + sz.h;

  // Right
  out[2].position.x = rel.x + sz.w;
  out[2].position.y = rel.y + sz.h;

  for(auto& v: out) v.color = color.apply(v.color);

  return out;
}

// Initialize SDL
bool Display::init() {
  // m_init = SDL_Init(SDL_INIT_VIDEO) >= 0;

  window = SDL_CreateWindow(
    "SDL Tutorial",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    SDL_WINDOW_SHOWN);

  if(window.isNull()) {
    printf("Window could not be created! Error: %s\n", SDL_GetError());
    return false;
  }

  rend = SDL_CreateRenderer(&*window, -1, SDL_RENDERER_ACCELERATED);
  if(rend.isNull()) {
    printf("Renderer could not be created! Error: %s\n", SDL_GetError());
    return false;
  }

  SDL_SetRenderDrawColor(&*rend, 0xFF, 0xFF, 0xFF, 0xFF);
  return true;
}

RTexture Display::loadTexture(const std::string& path) {
  RTexture out{IMG_LoadTexture(&*rend, path.c_str())};
  if(out.isNull()) {
    cout << "Unable to load " << path << "! Error: " << IMG_GetError() << '\n';
  }
  return out;
}

bool Display::loadMedia() {
  return true;
  // return not (
  //   (i_hello = loadTexture("data/02_img.bmp")).isNull()
  //   or (i_xOut  = loadTexture("data/x.bmp")).isNull()
  //   or (i_png   = loadTexture("data/png_loaded.png")).isNull()
  // );
}

// *****************
// * Event Loop
//
void consumeEvents(Game& g) {
  SDL_Event e;
  while(SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_MOUSEBUTTONDOWN: g.mouseEvent(e.button, true);  break;
      case SDL_MOUSEBUTTONUP:   g.mouseEvent(e.button, false); break;
      case SDL_KEYDOWN:         g.keyEvent  (e.key,    true);  break;
      case SDL_KEYUP:           g.keyEvent  (e.key,    false); break;
      case SDL_QUIT:
        cout << "Got SDL_QUIT\n";
        g.quit = true;
        break;
    }
  }
}

void update(Game& g) {
  Controller& c = g.controller;
  // TODO: loop instead
  Entity& e = g.player();
  e.mv.update(Loc{c.d - c.a, c.w - c.s});
  e.loc = e.move();

  // Controller& c = g.controller;
  // g.e1.mv.update(Loc{c.d - c.a, c.w - c.s});
  // g.e1.loc = g.e1.move();
}

void paintScreen(Display& d, Game& g) {
  SDL_RenderClear(&*d.rend);
  // SDL_RenderCopy(&*d.rend, &*d.i_png, NULL, NULL);

  g.eBack.loc = g.center;
  g.eBack.sz  = d.sz;
  g.eBack.render(d, g);
  for(auto& it: g.entities) {
    it.second.render(d, g);
  }
  SDL_RenderPresent(&*d.rend);
}

void eventLoop(Display& d, Game& g) {
  d.frame = SDL_GetTicks();

  // Demonstrate stretching/shrinking an image
  SDL_Rect stretchRect {
    .x = 0, .y = 0,
    .w = SCREEN_WIDTH / 2, .h = SCREEN_HEIGHT / 2
  };

  SDL_Event e;
  while(not g.quit){
    consumeEvents(g);
    update(g);
    paintScreen(d, g);
    d.frameDelay();
    g.loop += 1;
  }
}

// *****************
// * Game
int game() {
  if(SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }
  DEFER( SDL_Quit(); cout << "SDL_Quit Successfully\n" );

  int imgFlags = IMG_INIT_PNG;
  if(not (IMG_Init(imgFlags) & imgFlags)) {
    printf("Could not initialize! Error: %s\n", IMG_GetError());
    return 1;
  }
  DEFER( IMG_Quit() );

  Display d{};
  d.init();

  if(not d.loadMedia()) {
    return 1;
  }

  Game g{};
  g.eBack.color = {0x99, 0x99, 0x99};
  auto p = g.newEntity(); // player
  ASSERT_EQ(p.id, 0);
  p.sz = {50, 100};
  p.color = {0xFF, 0x00, 0x00};

  auto e = g.newEntity(); // reference
  ASSERT_EQ(e.id, 1);
  e.sz = {100, 200};
  e.loc = {100, 100};
  Loc exp = {100, 100};
  assert(e.loc == exp);
  assert(not (p.loc == exp));
  e.color = {0x00, 0x00, 0xFF};

  eventLoop(d, g);
  return 0;
}

int tests() {
  cout << "Running tests\n";
  CALL_TEST(size);
  CALL_TEST(bound);
  CALL_TEST(updateVel);
  return 0;
}

int main(int argc, char* args[]) {
  cout << "argc=" << argc << '\n';

  if(argc > 1) {
    if(0 == strcmp(args[1], "--test")) {
      return tests();
    }
  }

  int err = game();
  return err;
}
