/** 
* APP_PONGGAME.h - CV Controllable Pong game for Ornament and Crime
*
* (c) 2018, Jason Justian (chysn), Beize Maze, Swamp Flux
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#ifdef ENABLE_APP_PONG

#include <Arduino.h>
#include "OC_config.h"
#include "OC_apps.h"
#include "OC_ui.h"
#include "HSApplication.h"

/* Define player properties. INITIAL_BALL_DELAY is how many ISR cycles the ball takes to move. It
 * gets faster as the game goes on. PADDLE_DELAY is how many ISR cycles the player must wait before moving
 * again. This is to keep the game interesting at higher levels. PADDLE_WIDTH is the chunkiness of the paddle,
 * in pixels.
 *
 * Note: Each ISR cycle is about 60 microseconds.
 */
#define INITIAL_BALL_DELAY 20
#define PADDLE_DELAY 200
#define PADDLE_WIDTH 3
#define PADDLE_HEIGHT 16
#define BALL_WIDTH 2
#define BACK_CATCH 10

// there is more precision on the Y axis to fake various bounce angles
#define PRECISION_X 4
#define PRECISION_Y 7

/* This value is used for converting a ball's or paddle's Y position into a pitch value.
 * This number was determined experimentally, since I wasn't sure what the total range
 * for pitch values is.
 */
#define Y_POSITION_COEFF 128

/*
 * When checking the ADC, if I just look for whether the value is positive or negative,
 * then the value can't be zeroed, and the player can't move the paddle with the onboard controls.
 * This is probably because there's a little noise that randomly swirls around 0.
 * So this value simulates a center detent. This is another experimentally-determined value.
 */
#define CENTER_DETENT 640

// Length of audio bloops upon bounce
#define BLOOP_LENGTH 1000 // 17 * 100ms
#define BLOOP_PADDLE 0x8
#define BLOOP_WALL 0x16
#define BLOOP_SCORE 0x4

enum inputMode {
    ANALOG_INPUT_MODE,
    DIGITAL_INPUT_MODE,
    CPU_INPUT_MODE
};

struct Player {
    bool enabled = true;
    int score;
    int y_position;
    inputMode input_mode = ANALOG_INPUT_MODE;
    int paddle_x;
    int paddle_y = (32 - (PADDLE_HEIGHT>>1))<<PRECISION_Y;
    int movement_countdown = 0; // Used to limit the speed
    int paddle_speed = 0;
    static constexpr int BOUNDS_T = 11 << PRECISION_Y;
    static constexpr int BOUNDS_B = 61 << PRECISION_Y;
    static constexpr int PADDLE_RANGE = (BOUNDS_B - BOUNDS_T - (PADDLE_HEIGHT<<PRECISION_Y));
    static constexpr int Y_CENTER = (BOUNDS_T + BOUNDS_B)>>1;

    void movePaddleUp() {
        if (movement_countdown <= 0) {
            paddle_y -= 1<<PRECISION_Y;
            if (paddle_y < BOUNDS_T) {
                paddle_y = BOUNDS_T;
            }
            movement_countdown = PADDLE_DELAY;
        }
    }

    void movePaddleDown() {
        if (movement_countdown <= 0) {
            paddle_y += 1<<PRECISION_Y;
            if (paddle_y > BOUNDS_B - (PADDLE_HEIGHT<<PRECISION_Y)) {
                paddle_y = BOUNDS_B - (PADDLE_HEIGHT<<PRECISION_Y);
            }
            movement_countdown = PADDLE_DELAY;
        }
    }

    void handleAnalogInput(int cv){
        if(input_mode == ANALOG_INPUT_MODE){
            paddle_y = constrain(
                HSAPPLICATION_5V - cv,
                BOUNDS_T,
                BOUNDS_T + PADDLE_RANGE
            );
        }
    }

    void drawPaddle() {
        graphics.drawRect(paddle_x>>PRECISION_X, paddle_y>>PRECISION_Y, PADDLE_WIDTH, PADDLE_HEIGHT);
    }

    int getScore() {return score;}
};

class Pong : public HSApplication {
private:
    int bounces;
    int ball_delay; // The ball's delay at the next movement
    int ball_countdown; // Time (in increments of 60 microseconds) until the ball moves
    int bloop_countdown;
    int bloop_pitch;
    int hi_score; // The highest number of hits in a game since initialization

    int ball_x;
    int ball_y;
    int dir_x;
    int dir_y;

    static constexpr int BOUNDS_T = 11 << PRECISION_Y;
    static constexpr int BOUNDS_R = 126 << PRECISION_X;
    static constexpr int BOUNDS_B = 61 << PRECISION_Y;
    static constexpr int BOUNDS_L = 2 << PRECISION_X;
    static constexpr int PADDLE_W = PADDLE_WIDTH << PRECISION_X;
    static constexpr int PADDLE_H = PADDLE_HEIGHT << PRECISION_Y;

public:
    Player player1;
    Player player2;
    /* There are two types of game state properties: Those that should be initialized only once (like high score), and
     * those that need to be initialized after each game. Init() sets the first kind, and then calls StartNewGame()
     * to start a new game.
     */
    void Start() {
        bloop_countdown = 0;
        hi_score = 27;
        player1.input_mode = DIGITAL_INPUT_MODE;
        player2.input_mode = DIGITAL_INPUT_MODE;
        player1.paddle_x = BOUNDS_L + (6<<PRECISION_X);
        player2.paddle_x = BOUNDS_R - (6<<PRECISION_X) - (PADDLE_WIDTH<<PRECISION_X);

        StartNewGame();
    }
    void Resume() { }

    void ServeBall() {
        bounces = 0;
        // Game state
        ball_delay = INITIAL_BALL_DELAY;
        ball_countdown = ball_delay;

        // Ball properties
        ball_x = 64<<PRECISION_X;
        ball_y = 32<<PRECISION_Y;
        dir_x = 1;
        dir_y = 0;

        bloop_countdown = BLOOP_LENGTH;
        bloop_pitch = BLOOP_SCORE;
        // ball_y = random((BOUNDARY_TOP + 2)*2, (BOUNDARY_BOTTM - 2)*2); // Start off in a random spot
        // dir_x = random(0, 100) > 50 ? 1 : -1;
        //dir_y = random(0, 100) > 50 ? 1 : -1;
    }

    void StartNewGame() {
        player1.score = 0;
        player2.score = 0;
        player1.movement_countdown = 0;
        player2.movement_countdown = 0;

        ServeBall();
    }

    /* I'm using the ISR for keeping track of object timing. The interrupt is timer-based, so each
     * timer cycle is about 60 microseconds.
     */
    void Controller() {
        ball_countdown--;
        bloop_countdown--;
        if(player1.movement_countdown) --player1.movement_countdown;
        if(player2.movement_countdown) --player2.movement_countdown;

        MoveBall();

        // handle direct CV inputs
        player1.handleAnalogInput(In(0));
        player2.handleAnalogInput(In(3));

        // TODO:find out if i still need NLM / Buchla tweaks????
        // tweak for NLM to center the inputs around 5 octaves (6V on 1.2V/oct)
        //if (NorthernLightModular && p1_cv) p1_cv -= HSAPPLICATION_5V;
        // check the detent again, just for NLM
        // if (move_cv < -HEMISPHERE_CENTER_DETENT) movePaddleUp();
        // if (move_cv > HEMISPHERE_CENTER_DETENT) movePaddleDown();

        if(Gate(0) && !Gate(1)) player1.movePaddleUp();
        if(Gate(1) && !Gate(0)) player1.movePaddleDown();
        if(Gate(2) && !Gate(3)) player2.movePaddleUp();
        if(Gate(3) && !Gate(2)) player2.movePaddleDown();

        /* Handle output states:
         *
         * Outputs are set as follows. Note that outs A and B are triggers, so it's just a small value transposed
         * five octaves up. The trigger time is handled by counting down from BLOOP_LENGTH. There might be
         * a better way to do triggers, and I'll revisit this later.
         *
         * C and D outs are just scaled values. We have about a 60-step Y value, and I multiply that by Y_POSITION_COEFF
         * to get a pitch value to set. I tried to calibrate Y_POSITION_COEFF to get a CV range between 0 and 4-ish volts.
         */

        // Ball position CV (0 to 4-ish volts), based on the top of the ball
        uint32_t out_C = 5000 - ball_y;

        // Player paddle position CV (0 to 4-ish volts), based on the center of the paddle
        // uint32_t out_D = ((paddle_y + (PADDLE_HEIGHT / 2)) - BOUNDARY_TOP) * Y_POSITION_COEFF;

        Out(2, out_C);
        // Out(3, out_D);

        // make bloop noises
        if(bloop_countdown > 0){
            GateOut(3, OC::CORE::ticks & bloop_pitch);
        }
    }

    int get_hi_score() {return hi_score;}

    void handlePaddleBounce(int paddle_y) {
        dir_x = -dir_x;
        if((ball_y - paddle_y) < (4<<PRECISION_Y)) --dir_y;
        if((ball_y - paddle_y) >= (12<<PRECISION_Y)) ++dir_y;
        dir_y = constrain(dir_y,-6,6);
        bloop_countdown = BLOOP_LENGTH;
        bloop_pitch = BLOOP_PADDLE;
        ClockOut(0);
        // Level up!!
        if (!(++bounces % 3)) LevelUp();
    }

    void MoveBall() {
        /* MoveBall() is called with each loop cycle. Moving the ball with each loop would make the
         * game unplayable, so movements are delayed with countdowns. ISR() is responsible for decrementing
         * counters.
         */
        if (ball_countdown <= 0) {
            // Move the ball based on the current direction
            ball_x += dir_x;
            ball_y += dir_y;

            // Check the playfield boundaries. Oh, yes, O_C will crash if you go too far out of bounds.
            if (ball_y < BOUNDS_T || ball_y > BOUNDS_B) {
                dir_y = -dir_y;
                bloop_countdown = BLOOP_LENGTH;
                bloop_pitch = BLOOP_WALL;
                ClockOut(1);
            }

            // Check collision with Player 1
            
            if (dir_x < 0 &&
                (ball_x <= player1.paddle_x + PADDLE_W) &&
                (ball_x >= player1.paddle_x) &&
                (ball_y >= player1.paddle_y) &&
                (ball_y - BALL_WIDTH <= player1.paddle_y + PADDLE_H)) {

                handlePaddleBounce(player1.paddle_y);
            }
            

            // Check collision with Player 2
            if (dir_x > 0 &&
                (ball_x + BALL_WIDTH >= player2.paddle_x) &&
                (ball_x <= player2.paddle_x) &&
                (ball_y >= player2.paddle_y) &&
                (ball_y - BALL_WIDTH <= player2.paddle_y + PADDLE_H)){

                handlePaddleBounce(player2.paddle_y);
            }

            if (ball_x < BOUNDS_L + (BALL_WIDTH<<PRECISION_X) - (BACK_CATCH<<PRECISION_X)) {
                player2.score++;
                ServeBall();
            }
            if( ball_x > BOUNDS_R + (BACK_CATCH<<PRECISION_X)) {
                player1.score++;
                ServeBall();
            }

            if (player1.score > hi_score) {
                hi_score = player1.score;
                StartNewGame();
            }
            if (player2.score > hi_score) {
                hi_score = player2.score;
                StartNewGame();
            }

            ball_countdown = ball_delay; // Reset delay to start a new movement cycle
        }
    }

    // Performs the LevelUp. The game is designed to get brutal over time.
    void LevelUp() {
        --ball_delay;
        if(ball_delay < 1) ball_delay = 1;
    }

    /*
     * The ball is just a little 2x2 square, with ball_x and ball_y describing the upper-left corner.
     */
    void DrawBall() {
      graphics.drawFrame(ball_x >> PRECISION_X, ball_y >> PRECISION_Y, BALL_WIDTH, BALL_WIDTH);
    }

    /* If the paddle countdown has elapsed, the paddle may move. Whenever the paddle is moved, the downdown begins again
     * to constrain the speed of the paddle. See the bottom of MoveBall() for more deets.
     */

    void View() {
        // Frame
        gfxFrame(0, 9, 128, 55);

        // Net
        gfxDottedLine( 64, 10, 64, 63);

        // Header
        gfxPos(1,1);
        int p1_score = player1.getScore();
        int p2_score = player2.getScore();
        int hi_score = get_hi_score();
        
        gfxPrint(0,0,p1_score); // left
        gfxPrint(46,0,"HI:"); // center
        gfxPrint(hi_score); // center
        gfxPrint(110,0,p2_score); // right
        
        // gfxPrint(12,32,"B:");
        // gfxPrint((ball_y>>PRECISION) - player1.paddle_y);
        // // gfxPrint(ball_y>>PRECISION);
        // // gfxPrint(" P:");
        // // gfxPrint(player1.paddle_y);
        // gfxPrint(70,32,"D:");
        // gfxPrint(dir_y);
        
        // out_C
        gfxPrint(70,18,BOUNDS_B - ball_y);
        
        // In(0)
        gfxPrint(12,18,In(0));
        gfxPrint(12,32,player1.paddle_y);



        DrawGame();
    }

    void DrawGame() {
        // Game pieces
        DrawBall();
        player1.drawPaddle();
        player2.drawPaddle();
    }
};

Pong pong_instance;

// App stubs
void PONGGAME_init() {
    pong_instance.BaseStart();
}

static constexpr size_t PONGGAME_storageSize() {
    return 0;
}

static size_t PONGGAME_save(void *storage) {
    return 0;
}

static size_t PONGGAME_restore(const void *storage) {
    return 0;
}

void PONGGAME_isr() {
    pong_instance.BaseController();
}

void PONGGAME_handleAppEvent(OC::AppEvent event) {
}

void PONGGAME_loop() {
}

void PONGGAME_menu() {
    pong_instance.BaseView();
}

void PONGGAME_screensaver() {
    // Game pieces only
    pong_instance.DrawBall();
    pong_instance.player1.drawPaddle();
    pong_instance.player2.drawPaddle();
}

/* Controlling the game with the buttons is the worst experience ever, so this is really just here
 * to demonstrate how the buttons work.
 */
void PONGGAME_handleButtonEvent(const UI::Event &event) {
    if (UI::EVENT_BUTTON_DOWN == event.type && OC::CONTROL_BUTTON_L == event.control) {
        // pong_instance.SnapToBall();
    }
    if (UI::EVENT_BUTTON_PRESS == event.type) {
        switch (event.control) {
          case OC::CONTROL_BUTTON_UP:
              pong_instance.player1.input_mode = ANALOG_INPUT_MODE;
            break;

          case OC::CONTROL_BUTTON_DOWN:
               pong_instance.player2.input_mode = ANALOG_INPUT_MODE;
            break;

          case OC::CONTROL_ENCODER_L:
            pong_instance.player1.input_mode = DIGITAL_INPUT_MODE;
            break;

          case OC::CONTROL_BUTTON_R:
               pong_instance.player2.input_mode = DIGITAL_INPUT_MODE;
            break;
        }
    }
    if (UI::EVENT_BUTTON_LONG_PRESS == event.type && OC::CONTROL_BUTTON_L == event.control) {
      pong_instance.LevelUp();
    }
}

/* The UI::Event has a value property, which is positive when the encoder is turned clockwise and
 * negative when it's turned widdershins. I just wanted to say "widdershins."
 */
void PONGGAME_handleEncoderEvent(const UI::Event &event) {
    if (OC::CONTROL_ENCODER_L == event.control && pong_instance.player1.input_mode == DIGITAL_INPUT_MODE) {
        if (event.value < 0) pong_instance.player1.movePaddleUp();
        if (event.value > 0) pong_instance.player1.movePaddleDown();
    }
    if (OC::CONTROL_ENCODER_R == event.control && pong_instance.player2.input_mode == DIGITAL_INPUT_MODE) {
        if (event.value < 0) pong_instance.player2.movePaddleDown();
        if (event.value > 0) pong_instance.player2.movePaddleUp();
    }
}

#endif
