/*
// ATM-Software-CPP � Radu Salagean 2015
//
// Rebuild & Port by XtremePrime 2021:
// -- In the spirit of the original, all code was kept in one .cpp file
// -- Ported from SFML 2.2 to SFML 2.5.1
// -- Increased the readability and modularity
// -- Future proofed for cross-platform
//
// Android port: March 2022 - Radu Salagean
*/

//- Platform definitions
#if defined(_WIN32) || defined(_WIN64)
#define TARGET_WIN
#elif defined(TARGET_OS_MAC)
#define TARGET_MAC
#elif defined(__ANDROID__)
#define TARGET_ANDROID
#elif defined(__linux__) || defined(__unix__)
#define TARGET_LINUX
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <list>
#include <string>
#include <algorithm>
#include <ctime>
#include <cmath>

#include <SFML/System.hpp>
#include <SFML/Audio.hpp>
#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>

#ifdef TARGET_WIN
 #define NOMINMAX
// #include <windows.h>
#endif //_WIN32

#ifdef TARGET_ANDROID
#include <android/asset_manager.h>
#include <android/log.h>
#include <jni.h>
#include <android/native_activity.h>
#include <SFML/System/NativeActivity.hpp>
#endif

#ifdef TARGET_ANDROID

#define SHOW_CURSOR

class AndroidGlue
{
private:
    // General
    ANativeActivity* nativeActivityHandle;
    JavaVM* vm;
    JNIEnv* env;

    // Vibrate
    jobject vibrateObject;
    jmethodID vibrateMethod;

    void init()
    {
        // First we'll need the native activity handle
        nativeActivityHandle = sf::getNativeActivity();

        // Retrieve the AssetManager so we can read resources from the APK
        assetManager = nativeActivityHandle->assetManager;

        // Retrieve the JVM and JNI environment
        vm = nativeActivityHandle->vm;
        env = nativeActivityHandle->env;

        // Attach this thread to the main thread
        attachToMainThread();

        // Retrieve class information
        jclass nativeActivity = env->FindClass("android/app/NativeActivity");
        jclass context = env->FindClass("android/content/Context");

        initVibration(context, nativeActivity);

        // Free references
        env->DeleteLocalRef(context);
        env->DeleteLocalRef(nativeActivity);
    }

    bool attachToMainThread()
    {
        JavaVMAttachArgs attachargs;
        attachargs.version = JNI_VERSION_1_6;
        attachargs.name = "NativeThread";
        attachargs.group = nullptr;
        jint res = vm->AttachCurrentThread(&env, &attachargs);

        if (res == JNI_ERR)
            return false;
        return true;
    }

    void initVibration(jclass context, jclass nativeActivity)
    {
        // Get the value of a constant
        jfieldID vibratorServiceFieldId = env->GetStaticFieldID(context, "VIBRATOR_SERVICE", "Ljava/lang/String;");
        jobject vibratorServiceObject = env->GetStaticObjectField(context, vibratorServiceFieldId);

        // Get the method 'getSystemService' and call it
        jmethodID getSystemServiceMethodId = env->GetMethodID(nativeActivity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        vibrateObject = env->CallObjectMethod(nativeActivityHandle->clazz, getSystemServiceMethodId, vibratorServiceObject);

        // Get the object's class and retrieve the member name
        jclass vibrateClass = env->GetObjectClass(vibrateObject);
        vibrateMethod = env->GetMethodID(vibrateClass, "vibrate", "(J)V");

        // Free references
        env->DeleteLocalRef(vibrateClass);
        env->DeleteLocalRef(vibratorServiceObject);
    }

public:
    // Asset Manager
    AAssetManager* assetManager;

    AndroidGlue()
    {
        init();
    }

    void vibrate(int durationMillis)
    {
        jlong durationMillisJlong = durationMillis;
        // Bzzz!
        env->CallVoidMethod(vibrateObject, vibrateMethod, durationMillisJlong);
    }

    void release()
    {
        // Free references
        env->DeleteLocalRef(vibrateObject);

        // Detach thread again
        vm->DetachCurrentThread();
    }
};

#endif

struct Animation
{
    virtual void update(sf::Time deltaTime) = 0;
    virtual bool isEnded() = 0;
    virtual ~Animation() = default;
};

template <class T>
class GenericAnimation : public Animation
{
private:
    bool ended = false;

protected:
    sf::Time duration;
    sf::Clock runningTimeClock;
    std::function<void(T animatedProperty)> onUpdateCallback;
    std::function<void()> onAnimationEndCallback;

    GenericAnimation(sf::Time duration,
                     std::function<void(T)> onUpdateCallback,
                     std::function<void()> onAnimationEndCallback)
    {
        this->duration = duration;
        this->onUpdateCallback = onUpdateCallback;
        this->onAnimationEndCallback = onAnimationEndCallback;
    }
    
    bool isEnded()
    {
        return ended;
    }

    virtual void onAnimationEnd()
    {
        ended = true;
        if (onAnimationEndCallback) onAnimationEndCallback();
    }

public:
    virtual ~GenericAnimation() = default;
};

class AlphaAnimation : public GenericAnimation<int>
{
private:
    float targetAlpha;
    float currentAlpha;
    float alphaDiff;

    void setAlpha(float alpha)
    {
        float sanitizedAlpha;
        if (alphaDiff > 0.f)
            sanitizedAlpha = std::min(targetAlpha, alpha);
        else
            sanitizedAlpha = std::max(targetAlpha, alpha);
        this->currentAlpha = sanitizedAlpha;
        onUpdateCallback(sanitizedAlpha);
    }

    int getSanitizedColorComponent(int component) {
        return std::max(0, std::min(255, component));
    }

public:
    AlphaAnimation(sf::Time duration, int startAlpha, int targetAlpha,
                   std::function<void(int)> onUpdateCallback,
                   std::function<void()> onAnimationEndCallback = {}) :
                   GenericAnimation(duration, onUpdateCallback, onAnimationEndCallback)
    {
        int startAlphaSafe = getSanitizedColorComponent(startAlpha);
        int targetAlphaSafe = getSanitizedColorComponent(targetAlpha);
        this->alphaDiff = targetAlphaSafe - startAlphaSafe;
        this->targetAlpha = targetAlphaSafe;
        setAlpha(startAlphaSafe);
    }

    ~AlphaAnimation() = default;

    void update(sf::Time deltaTime)
    {
        if (isEnded()) return;
        if (runningTimeClock.getElapsedTime() <= duration)
        {
            float currentAlphaDiff = (deltaTime * (float) alphaDiff) / duration;
            float newAlpha = currentAlpha + currentAlphaDiff;
            setAlpha(newAlpha);
        } else {
            onAnimationEnd();
        }
    }
};

enum class OffsetAnimationUpdateType
{
    SET_POSITION,
    MOVE
};

struct OffsetAnimationUpdate
{
    OffsetAnimationUpdateType type;
    sf::Vector2f value;
};

class OffsetAnimation : public GenericAnimation<OffsetAnimationUpdate>
{
private:
    sf::Vector2f getSafeIncrementalOffset(sf::Vector2f proposedOffset)
    {
        sf::Vector2f safeOffset;
        sf::Vector2f availableSpace = targetOffset - cumulatedOffset;
        if (targetOffset.x > 0.f)
            safeOffset.x = std::min(availableSpace.x, proposedOffset.x);
        else
            safeOffset.x = std::max(availableSpace.x, proposedOffset.x);
        if (targetOffset.y > 0.f)
            safeOffset.y = std::min(availableSpace.y, proposedOffset.y);
        else
            safeOffset.y = std::max(availableSpace.y, proposedOffset.y);
        return safeOffset;
    }

protected:
    sf::Vector2f targetOffset;
    sf::Vector2f cumulatedOffset;

public:
    OffsetAnimation(sf::Time duration, 
                    std::function<void(OffsetAnimationUpdate)> onUpdateCallback,
                    std::function<void()> onAnimationEndCallback) :
                    GenericAnimation(duration, onUpdateCallback, onAnimationEndCallback) {}

    OffsetAnimation(sf::Time duration,
                    sf::Vector2f startPosition, 
                    sf::Vector2f targetOffset,
                    std::function<void(OffsetAnimationUpdate)> onUpdateCallback,
                    std::function<void()> onAnimationEndCallback) :
                    GenericAnimation(duration, onUpdateCallback, onAnimationEndCallback)
    {
        onUpdateCallback(OffsetAnimationUpdate {
            OffsetAnimationUpdateType::SET_POSITION, startPosition
        });
        this->targetOffset = targetOffset;
    }

    virtual ~OffsetAnimation() = default;

    void update(sf::Time deltaTime)
    {
        if (isEnded()) return;
        if (runningTimeClock.getElapsedTime() <= duration)
        {
            float offsetX = (deltaTime * targetOffset.x) / duration;
            float offsetY = (deltaTime * targetOffset.y) / duration;
            sf::Vector2f safeIncrementalOffset = getSafeIncrementalOffset(sf::Vector2f(offsetX, offsetY));
            this->cumulatedOffset += safeIncrementalOffset;
            onUpdateCallback(OffsetAnimationUpdate {
                OffsetAnimationUpdateType::MOVE, safeIncrementalOffset
            });
        } else {
            onAnimationEnd();
        }
    }
};

enum VerticalOffsetAnimationType
{
    TOP_TO_ORIGIN,
    ORIGIN_TO_TOP
};

class VerticalOffsetAnimation : public OffsetAnimation
{
public:
    VerticalOffsetAnimation(
            sf::Time duration,
            sf::Vector2f originPosition,
            VerticalOffsetAnimationType type,
            float animatedSubjectHeight,
            std::function<void(OffsetAnimationUpdate)> onUpdateCallback,
            std::function<void()> onAnimationEndCallback
    ) : OffsetAnimation(duration, onUpdateCallback, onAnimationEndCallback)
    {
        switch (type) {
            case VerticalOffsetAnimationType::TOP_TO_ORIGIN:
                onUpdateCallback({
                    OffsetAnimationUpdateType::SET_POSITION,
                    sf::Vector2f(originPosition.x, originPosition.y - animatedSubjectHeight)
                });
                this->targetOffset = sf::Vector2f(0, animatedSubjectHeight);
                break;
            case VerticalOffsetAnimationType::ORIGIN_TO_TOP:
                onUpdateCallback({
                    OffsetAnimationUpdateType::SET_POSITION,
                    originPosition
                });
                this->targetOffset = sf::Vector2f(0, -animatedSubjectHeight);
                break;
        }
    }

    ~VerticalOffsetAnimation() = default;
};

class ActionTimer
{
private:
    sf::Clock clock;
    sf::Time targetDuration;
    std::function<void()> callback;

public:
    ActionTimer(sf::Time targetDuration, std::function<void()> callback)
    {
        this->targetDuration = targetDuration;
        this->callback = callback;
    }

    void update()
    {
        sf::Time elapsedTime = clock.getElapsedTime();
        if (elapsedTime <= targetDuration) return;
        callback();
    }
};

void handleOffsetAnimationUpdate(sf::Sprite* sprite, OffsetAnimationUpdate* update)
{
    switch (update->type)
    {
        case OffsetAnimationUpdateType::SET_POSITION:
            sprite->setPosition(update->value);
            break;
        case OffsetAnimationUpdateType::MOVE:
            sprite->move(update->value);
            break;
    }
}

class Atm
{
private:
    //- Title and Version
    std::string title = "ATM Software RELOADED";
    std::string ver = "1.1";

    //- Screen Size
    const int CANVAS_WIDTH = 960, CANVAS_HEIGHT = 620;
    sf::Vector2u currentWindowSize;

    //- States
    bool cardVisible = true, cashLargeVisible = false, cashSmallVisible = false, receiptVisible = false;
    unsigned short int scrState = 1;
    unsigned short int pin = 0; unsigned short int pinCount = 0; unsigned short int pinRetry = 0;
    int amount = 0; unsigned short int amountCount = 0;
    bool blocked = false;
    bool accountSuspendedFlag = false;
    bool windowHasFocus = true;

    //- User Data Structure
    struct User
    {
        std::string iban;
        std::string lastName;
        std::string firstName;
        unsigned short int pin;
        unsigned long long int balance;
    };

    //- General
    sf::View view;
    sf::VideoMode screen;
    sf::RenderWindow window;
    sf::Event event;
    sf::Font font;
    sf::Time elapsed;

    //- Textures and Sprites
    sf::Texture backgroundTexture;             sf::Sprite backgroundSprite;

    sf::Texture cardTexture;                   sf::Sprite cardSprite;
    sf::Vector2f cardSpritePosition = sf::Vector2f(740, 198);
    sf::RectangleShape cardMask;

    sf::Texture cashLargeTexture;              sf::Sprite cashLargeSprite;
    sf::Vector2f cashLargeSpritePosition = sf::Vector2f(90, 370);
    sf::RectangleShape cashLargeMask;

    sf::Texture cashSmallTexture;              sf::Sprite cashSmallSprite;
    sf::Vector2f cashSmallSpritePosition = sf::Vector2f(695, 463);
    sf::RectangleShape cashSmallMask;

    sf::Texture receiptTexture;                sf::Sprite receiptSprite;
    sf::Vector2f receiptSpritePosition = sf::Vector2f(740, 54);
    sf::RectangleShape receiptMask;

    //- Sound Buffers and Sounds
    sf::SoundBuffer cardSndBuf;                sf::Sound cardSnd;
    sf::SoundBuffer menuSndBuf;                sf::Sound menuSnd;
    sf::SoundBuffer clickSndBuf;               sf::Sound clickSnd;
    sf::SoundBuffer keySndBuf;                 sf::Sound keySnd;
    sf::SoundBuffer cashSndBuf;                sf::Sound cashSnd;
    sf::SoundBuffer printReceiptSndBuf;        sf::Sound printReceiptSnd;

    //- Processing Time
    sf::Time processingTime = sf::seconds(2);

    //- Text
    sf::Text scrClock;
    sf::Text usernameScr;
    sf::Text ibanScr;
    sf::Text L1Txt;
    sf::Text R1Txt;
    sf::Text R3Txt;
    sf::Text dialog;
    sf::Text liveTxt;

    //- Shapes
    sf::RectangleShape pinBorderShape;
    sf::RectangleShape amountBorderShape;

    //- Users
    std::vector<User> users;
    User* user;

    //- Text files
    const std::string databasePath = "database/database.txt";
    std::stringstream database;
    std::ofstream log;

    //- Out String Stream
    std::ostringstream oss;

    //- Screen Info Strings
    std::ostringstream usernameScrStr;
    std::ostringstream ibanScrStr;
    std::ostringstream convert;
    std::ostringstream balance;

    //- Chat arrays for live text
    std::string pinLiveTxt = "****"; std::string amountLiveTxt = "";

    //- Outstanding Click / Touch Event
    sf::Vector2i* outstandIngInteractionEvent;

    //- Cursor
    const int CURSOR_CIRCLE_RADIUS = 16;
    sf::CircleShape cursorCircle = sf::CircleShape(CURSOR_CIRCLE_RADIUS);
    sf::Color cursorCircleIdleColor = sf::Color(255, 0, 0, 0);

    //- Action Timer
    ActionTimer* actionTimer;

    //- Animations

    std::list<Animation*> runningAnimations;
    Animation* cursorAnimation = nullptr;
    // this is different than the card sound time because the end click is not at the end of the sound
    sf::Time cardAnimationTime = sf::milliseconds(1002);
    sf::Time cursorFadeOutAnimationTime = sf::seconds(1.5);

    //- Frame Delta Clock
    sf::Clock frameDeltaClock;

    //- Routine Action Codes
    enum RoutineCode {
        CARD_IN = 1,
        CARD_OUT = 2,
        KEY_SOUND = 3,
        MENU_SOUND = 4,
        CASH_LARGE_OUT = 5,
        CASH_SMALL_IN = 6,
        RECEIPT_OUT = 7
    };

    //- Vibration Length
    enum VibrationDuration {
        SHORT = 20,
        MEDIUM = 40
    };

#ifdef TARGET_ANDROID
    AndroidGlue androidGlue;
#endif

    void initWin()
    {
#ifdef TARGET_ANDROID // We support letterbox mode on Android devices
        screen = sf::VideoMode(sf::VideoMode::getDesktopMode());
        view.setSize(CANVAS_WIDTH, CANVAS_HEIGHT);
        view.setCenter(view.getSize().x / 2, view.getSize().y / 2);
        view = getLetterboxView(view, screen.width, screen.height);
        window.create(screen, "");
        window.setView(view);
#else
        screen = sf::VideoMode(CANVAS_WIDTH, CANVAS_HEIGHT);
        window.create(screen, programTitle(), sf::Style::Titlebar | sf::Style::Close);
#endif
        window.setFramerateLimit(60);
        window.setKeyRepeatEnabled(false);
        currentWindowSize = window.getSize();
    }

    // https://github.com/SFML/SFML/wiki/Source:-Letterbox-effect-using-a-view
    sf::View getLetterboxView(sf::View view, int windowWidth, int windowHeight) {

        // Compares the aspect ratio of the window to the aspect ratio of the view,
        // and sets the view's viewport accordingly in order to achieve a letterbox effect.
        // A new view (with a new viewport set) is returned.

        float windowRatio = windowWidth / (float) windowHeight;
        float viewRatio = view.getSize().x / (float) view.getSize().y;
        float sizeX = 1;
        float sizeY = 1;
        float posX = 0;
        float posY = 0;

        bool horizontalSpacing = true;
        if (windowRatio < viewRatio)
            horizontalSpacing = false;

        // If horizontalSpacing is true, the black bars will appear on the left and right side.
        // Otherwise, the black bars will appear on the top and bottom.

        if (horizontalSpacing)
        {
            sizeX = viewRatio / windowRatio;
            posX = (1 - sizeX) / 2.f;
        }
        else
        {
            sizeY = windowRatio / viewRatio;
            posY = (1 - sizeY) / 2.f;
        }
        view.setViewport(sf::FloatRect(posX, posY, sizeX, sizeY));
        return view;
    }

    sf::Vector2i* getScaledPointerCoordinates(int originalX, int originalY)
    {
#ifdef TARGET_ANDROID
        int left = view.getViewport().left * currentWindowSize.x;
        int top = view.getViewport().top * currentWindowSize.y;
        float scaleX = (currentWindowSize.x - 2 * left) / (float) CANVAS_WIDTH;
        float scaleY = (currentWindowSize.y - 2 * top) / (float) CANVAS_HEIGHT;
        int processedX = (originalX - left) / scaleX;
        int processedY = (originalY - top) / scaleY;
        return new sf::Vector2i(processedX, processedY);
#else
        return new sf::Vector2i(originalX, originalY);
#endif
    }

    void loadDatabase()
    {
#ifdef TARGET_ANDROID
        // In order to properly read asset files in android, we use the Asset NDK Module
        // Links:
        // https://developer.android.com/ndk/reference/group/asset
        // https://stackoverflow.com/a/33957074
        if (androidGlue.assetManager != nullptr)
        {
            // Open your file
            std::string filePath = res(databasePath);
            AAsset* file = AAssetManager_open(androidGlue.assetManager, filePath.c_str(), AASSET_MODE_BUFFER);
            // Get the file length
            size_t fileLength = AAsset_getLength(file);

            // Allocate memory to read your file
            char* fileContent = new char[fileLength + 1];

            // Read your file
            AAsset_read(file, fileContent, fileLength);
            // For safety you can add a 0 terminating character at the end of your file ...
            fileContent[fileLength] = '\0';

            // Do whatever you want with the content of the file
            database << fileContent;

            // Free the memory you allocated earlier
            delete [] fileContent;
        }
#else
        std::ifstream fileStream(res(databasePath));
        if (fileStream.is_open())
        {
            database << fileStream.rdbuf();
            fileStream.close();
        }
#endif

        if (database.tellp() > std::streampos(0))
        {
            oss << getTimeCli() << "User database loaded"; logMsg(oss.str());
            loadClients();
        }
        else
        {
            oss << getTimeCli() << "User database not found"; logMsg(oss.str());
            loadPlaceholderClient();
        }
    }

    void init()
    {
        //- Create new log file
#ifndef TARGET_ANDROID
        log.open(getLogFileName().c_str());
#endif

        //- Init States
        initStates();

        //- Initialize Window
        initWin();

        //- Initialize CLI
        system("color 0A");
        oss << "================================================================================"; logMsg(oss.str());
        oss << "==================================ATM Software=================================="; logMsg(oss.str());
        oss << "================================================================================"; logMsg(oss.str());
        oss << getTimeCli() << "ATM is now powered on"; logMsg(oss.str());

        //- Load database
        loadDatabase();

        //- Load fonts
        if (font.loadFromFile(res("courier_new.ttf")))
        {
            oss << getTimeCli() << "Font loaded"; logMsg(oss.str());
        }
        else
        {
            oss << getTimeCli() << "Font not found"; logMsg(oss.str());
            window.close();
        }

        //- Load textures
        if (!backgroundTexture.loadFromFile(res("backgnd_texture.png")) ||
            !cardTexture.loadFromFile(res("card_texture.png")) ||
            !cashLargeTexture.loadFromFile(res("cash_large_texture.jpg")) ||
            !cashSmallTexture.loadFromFile(res("cash_small_texture.jpg")) ||
            !receiptTexture.loadFromFile(res("receipt_texture.jpg")))
        {
            oss << getTimeCli() << "One or more textures not found"; logMsg(oss.str());
            window.close();
        }
        else
        {
            oss << getTimeCli() << "Textures loaded"; logMsg(oss.str());
        }

        //- Load sounds in the sound buffer
        std::vector<sf::SoundBuffer*> soundPtr = { &cardSndBuf, &menuSndBuf, &clickSndBuf, &keySndBuf, &cashSndBuf, &printReceiptSndBuf };
        std::vector<std::string> soundArr = {
                res("card_snd.wav"),
                res("menu_snd.wav"),
                res("click_snd.wav"),
                res("key_snd.wav"),
                res("cash_snd.wav"),
                res("print_receipt_snd.wav")
        };
        bool soundOk = true;
        for (int i = 0; i < soundPtr.size(); ++i)
        {
            if (!soundPtr[i]->loadFromFile(soundArr[i]))
            {
                oss << getTimeCli() << "\"" << soundArr[i] << "\" not found"; logMsg(oss.str());
                window.close();
                soundOk = false;
                break;
            }
        }
        if(soundOk)
            oss << getTimeCli() << "Sounds loaded"; logMsg(oss.str());

        //- Ready to go
        oss << getTimeCli() << "ATM is ready to use"; logMsg(oss.str());

        //- Assign font to text
        scrClock.setFont(font);
        usernameScr.setFont(font);
        ibanScr.setFont(font);
        L1Txt.setFont(font);
        R1Txt.setFont(font);
        R3Txt.setFont(font);
        dialog.setFont(font);
        liveTxt.setFont(font);

        //- Assign texture to sprite
        backgroundSprite.setTexture(backgroundTexture);

        sf::IntRect ir;

        ir = sf::IntRect(716, 0, 197, 198);
        cardSprite.setTexture(cardTexture);                             cardSprite.setPosition(cardSpritePosition);
        cardMask.setSize(sf::Vector2f(ir.width, ir.height));            cardMask.setPosition(ir.left, ir.top);
        cardMask.setTexture(&backgroundTexture);                        cardMask.setTextureRect(ir);

        ir = sf::IntRect(80, 0, 484, 370);
        cashLargeSprite.setTexture(cashLargeTexture);                   cashLargeSprite.setPosition(cashLargeSpritePosition);
        cashLargeMask.setSize(sf::Vector2f(ir.width, ir.height));       cashLargeMask.setPosition(ir.left, ir.top);
        cashLargeMask.setTexture(&backgroundTexture);                   cashLargeMask.setTextureRect(ir);

        ir = sf::IntRect(688, 250, 250, 213);
        cashSmallSprite.setTexture(cashSmallTexture);                   cashSmallSprite.setPosition(cashSmallSpritePosition);
        cashSmallMask.setSize(sf::Vector2f(ir.width, ir.height));       cashSmallMask.setPosition(ir.left, ir.top);
        cashSmallMask.setTexture(&backgroundTexture);                   cashSmallMask.setTextureRect(ir);

        ir = sf::IntRect(716, 0, 197, 54);
        receiptSprite.setTexture(receiptTexture);                       receiptSprite.setPosition(receiptSpritePosition);
        receiptMask.setSize(sf::Vector2f(ir.width, ir.height));         receiptMask.setPosition(ir.left, ir.top);
        receiptMask.setTexture(&backgroundTexture);                     receiptMask.setTextureRect(ir);

        //- Assign buffer to sounds
        cardSnd.setBuffer(cardSndBuf);
        menuSnd.setBuffer(menuSndBuf);
        clickSnd.setBuffer(clickSndBuf);
        keySnd.setBuffer(keySndBuf);
        cashSnd.setBuffer(cashSndBuf);
        printReceiptSnd.setBuffer(printReceiptSndBuf);

        // Cursor
        cursorCircle.setFillColor(cursorCircleIdleColor);
    }

    void initStates()
    {
        //- Initialize States
        cardVisible = true; cashLargeVisible = false; cashSmallVisible = false; receiptVisible = false;
        scrState = 1;
        pin = 0; pinCount = 0; pinRetry = 0;
        amount = 0; amountCount = 0;
        amountLiveTxt = "";
        convert.str("");
        balance.str("");
        outstandIngInteractionEvent = nullptr;
        actionTimer = nullptr;
    }

    void handleActionTimer()
    {
        if (actionTimer != nullptr)
        {
            actionTimer->update();
        }
    }

    void handleEvents()
    {
        while (window.pollEvent(event))
        {
            switch (event.type)
            {
#ifdef TARGET_ANDROID
            // On Android MouseLeft/MouseEntered are (for now) triggered,
            // whenever the app loses or gains focus.
            //  ^ comment taken from the official SFML Android Sample Project:
            // https://github.com/SFML/SFML/blob/2e6c363e644b430fd137a7dbed9836f965796610/examples/android/app/src/main/jni/main.cpp#L137
            case sf::Event::MouseLeft:
                windowHasFocus = false;
                break;
            case sf::Event::MouseEntered:
                windowHasFocus = true;
                break;
#endif
            case sf::Event::Closed:
                window.close();
                break;
            case sf::Event::Resized:
                currentWindowSize = sf::Vector2u(event.size.width, event.size.height);
#ifdef TARGET_ANDROID
                view = getLetterboxView(view, event.size.width, event.size.height);
                window.setView(view);
#endif
                break;
            case sf::Event::TouchBegan:
                if (event.touch.finger == 0)
                    updatePointerLocation(event.touch.x, event.touch.y);
                break;
            case sf::Event::MouseButtonPressed:
                updatePointerLocation(event.mouseButton.x, event.mouseButton.y);
                break;
            }
        }
    }

    void updatePointerLocation(int rawX, int rawY)
    {
        sf::Vector2i* position = getScaledPointerCoordinates(rawX, rawY);
        cursorCircle.setPosition(
                position->x - CURSOR_CIRCLE_RADIUS / (float) 2,
                position->y - CURSOR_CIRCLE_RADIUS / (float) 2
        );
        addCursorAnimation(
                new AlphaAnimation(
                        cursorFadeOutAnimationTime, 127, 0,
                        [this](int alpha) -> void {
                            int currentColorInt = cursorCircle.getFillColor().toInteger();
                            currentColorInt = currentColorInt >> 8; // get rid of the old alpha
                            currentColorInt = currentColorInt << 8; // retreat
                            int newColor = currentColorInt | alpha;
                            cursorCircle.setFillColor(sf::Color(newColor));
                        },
                        [this]() -> void {
                            cursorCircle.setFillColor(cursorCircleIdleColor);
                            delete cursorAnimation;
                            cursorAnimation = nullptr;
                        }));
        if (!canAcceptInput()) {
            delete position;
            return;
        }
        if (outstandIngInteractionEvent != nullptr) delete outstandIngInteractionEvent;
        outstandIngInteractionEvent = position;
    }

    bool canAcceptInput()
    {
        return actionTimer == nullptr && runningAnimations.empty();
    }

    int getClickableObjectCode(int x, int y)
    {
        //===============================
        //Input Codes
        //===============================
        //Screen Buttons:
        //L1 = 1    R1 = 5
        //L2 = 2    R2 = 6
        //L3 = 3    R3 = 7
        //L4 = 4    R4 = 8
        //===============================
        //Keys:
        //1 = 9     2 = 12    3 = 16
        //4 = 10    5 = 13    6 = 17
        //7 = 11    8 = 14    9 = 18
        //          0 = 15
        //===============================
        //Action Buttons:
        //Cancel = 25
        //Clear  = 19
        //OK     = 20
        //===============================
        //Objects:
        //card       = 21
        //cashLarge  = 22
        //cashSmall  = 23
        //receipt    = 24
        //===============================
        //exit = 26
        //===============================

        if ((11 <= x) && (x <= 55)) //- Left Column Screen Buttons
        {
            if ((125 <= y) && (y <= 163)) //- Button: L1
            {
                return 1;
            }
            if ((174 <= y) && (y <= 210)) //- Button: L2
            {
                return 2;
            }
            if ((221 <= y) && (y <= 259)) //- Button: L3
            {
                return 3;
            }
            if ((269 <= y) && (y <= 305)) //- Button: L4
            {
                return 4;
            }
        }
        if ((588 <= x) && (x <= 632)) //- Right Column Screen Buttons
        {
            if ((127 <= y) && (y <= 163)) //- Button: R1
            {
                return 5;
            }
            if ((175 <= y) && (y <= 212)) //- Button: R2
            {
                return 6;
            }
            if ((223 <= y) && (y <= 259)) //- Button: R3
            {
                return 7;
            }
            if ((270 <= y) && (y <= 308)) //- Button: R4
            {
                return 8;
            }
        }
        if ((209 <= x) && (x <= 255) && !cashLargeVisible) //- Column 1 Keypad
        {
            if ((410 <= y) && (y <= 449)) //- Key: 1
            {
                return 9;
            }
            if ((457 <= y) && (y <= 496)) //- Key: 4
            {
                return 10;
            }
            if ((504 <= y) && (y <= 543)) //- Key: 7
            {
                return 11;
            }
        }
        if ((264 <= x) && (x <= 310) && !cashLargeVisible) //- Column 2 Keypad
        {
            if ((410 <= y) && (y <= 449)) //- Key: 2
            {
                return 12;
            }
            if ((457 <= y) && (y <= 496)) //- Key: 5
            {
                return 13;
            }
            if ((504 <= y) && (y <= 543)) //- Key: 8
            {
                return 14;
            }
            if ((551 <= y) && (y <= 590)) //- Key 0
            {
                return 15;
            }
        }
        if ((319 <= x) && (x <= 365) && !cashLargeVisible) //- Column 3 Keypad
        {
            if ((410 <= y) && (y <= 449)) //- Key: 3
            {
                return 16;
            }
            if ((457 <= y) && (y <= 496)) //- Key: 6
            {
                return 17;
            }
            if ((504 <= y) && (y <= 543)) //- Key: 9
            {
                return 18;
            }
        }
        if ((385 <= x) && (x <= 455) && !cashLargeVisible) //- Column 4 Keypad
        {
            if ((410 <= y) && (y <= 449)) //- Button: Cancel
            {
                return 25;
            }
            if ((457 <= y) && (y <= 496)) //- Button: Clear
            {
                return 19;
            }
            if ((504 <= y) && (y <= 543)) //- Button: OK
            {
                return 20;
            }
        }
        if ((cardSprite.getGlobalBounds().left <= x) && (x <= (cardSprite.getGlobalBounds().left + cardSprite.getGlobalBounds().width))) //- Object: card (x axis)
        {
            if ((cardSprite.getGlobalBounds().top <= y) && (y <= (cardSprite.getGlobalBounds().top + cardSprite.getGlobalBounds().height)) && cardVisible) //- Object: card (y axis) and visibility
            {
                return 21;
            }
        }
        if ((cashLargeSprite.getGlobalBounds().left <= x) && (x <= (cashLargeSprite.getGlobalBounds().left + cashLargeSprite.getGlobalBounds().width))) //- Object: cashLarge (x axis)
        {
            if ((cashLargeSprite.getGlobalBounds().top <= y) && (y <= (cashLargeSprite.getGlobalBounds().top + cashLargeSprite.getGlobalBounds().height)) && cashLargeVisible) //- Object: cashLarge (y axis) and visibility
            {
                return 22;
            }
        }
        if ((cashSmallSprite.getGlobalBounds().left <= x) && (x <= (cashSmallSprite.getGlobalBounds().left + cashSmallSprite.getGlobalBounds().width))) //- Object: cashSmall (x axis)
        {
            if ((cashSmallSprite.getGlobalBounds().top <= y) && (y <= (cashSmallSprite.getGlobalBounds().top + cashSmallSprite.getGlobalBounds().height)) && cashSmallVisible) //- Object: cashSmall (y axis) and visibility
            {
                return 23;
            }
        }
        if ((receiptSprite.getGlobalBounds().left <= x) && (x <= (receiptSprite.getGlobalBounds().left + receiptSprite.getGlobalBounds().width))) //- Object: receipt (x axis)
        {
            if ((receiptSprite.getGlobalBounds().top <= y) && (y <= (receiptSprite.getGlobalBounds().top + receiptSprite.getGlobalBounds().height)) && receiptVisible) //- Object: receipt (y axis) and visibility
            {
                return 24;
            }
        }
        if ((12 <= x) && (x <= 92)) //- Exit Button (x axis)
        {
            if ((563 <= y) && (y <= 603)) //- Exit Button (y axis)
            {
                return 26;
            }
        }
        return 0;
    }

    void update(sf::Time deltaTime)
    {
        //======================================================================================================================================================================================================================
        //Screen States (scrState)
        //======================================================================================================================================================================================================================
        //(1)Insert card --> (23)Processing --> (2)Insert PIN --> (3)MAIN MENU --Withdraw----------> (4)Enter Amount --> (5)Confirm -------------------------> (6)Processing -----> (7)Receipt? --y/n--> (8)Another transaction?
        //                                                    |                |                                     |
        //                                                    |                |                                     |
        //                                                    |                |                                     |
        //                                                    |                |                                     --> (10)Not enough funds
        //                                                    |                |
        //                                                    |                --Deposit-----------> (11)Enter amount --> (12)Confirm --> (13)Insert cash ---> (24)Processing -->(14)Receipt? --y/n---> (15)Another transaction?
        //                                                    |                |
        //                                                    |                |
        //                                                    |                |
        //                                                    |                --Account Balance---> (17)Processing ----> (18)Balance = ***. Receipt? -y/n---> (19)Another transaction?
        //                                                    |
        //                                                    |
        //                                                    |
        //                                                    --> (21)Wrong PIN
        //                                                    |
        //                                                    --> (22)Account Blocked (3 Wrong Attempts)
        //======================================================================================================================================================================================================================
        //======================================================================================================================================================================================================================

        int clickableObjectCode = -1;
        if (outstandIngInteractionEvent != nullptr)
        {
            clickableObjectCode = getClickableObjectCode(outstandIngInteractionEvent->x, outstandIngInteractionEvent->y);
            delete outstandIngInteractionEvent;
            outstandIngInteractionEvent = nullptr;
        }

        switch (scrState)
        {
            case 1: //- (1) Insert Card
                if (clickableObjectCode == 21)
                {
                    eventRoutine(RoutineCode::CARD_IN, [this]() -> void {
                        scrState = 23;
                    });
                }
                break;
            case 2: //- (2) Insert PIN
                if (pinCount < 4)
                {
                    switch (clickableObjectCode)
                    {
                        case 9: //- 1
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 1;
                            pinCount++;
                            break;
                        case 10://- 4
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 4;
                            pinCount++;
                            break;
                        case 11://- 7
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 7;
                            pinCount++;
                            break;
                        case 12://- 2
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 2;
                            pinCount++;
                            break;
                        case 13://- 5
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 5;
                            pinCount++;
                            break;
                        case 14://- 8
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 8;
                            pinCount++;
                            break;
                        case 15://- 0
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 0;
                            pinCount++;
                            break;
                        case 16://- 3
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 3;
                            pinCount++;
                            break;
                        case 17://- 6
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 6;
                            pinCount++;
                            break;
                        case 18://- 9
                            eventRoutine(RoutineCode::KEY_SOUND);
                            pin = pin * 10 + 9;
                            pinCount++;
                            break;
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            pinCount = 0;
                            pin = 0;
                            break;
                    }
                }
                if (pinCount == 4)
                {
                    switch (clickableObjectCode)
                    {
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            pinCount = 0;
                            pin = 0;
                            break;
                        case 20://- OK
                            eventRoutine(RoutineCode::MENU_SOUND);
                            User* useLookupResult = findUserByPin(pin);
                            if (useLookupResult != nullptr)
                            {
                                signIn(useLookupResult);
                                oss << getTimeCli() << "Cardholder successfully authenticated:"; logMsg(oss.str());
                                oss << "\t\t\t  Full Name: " << user->lastName << " " << user->firstName; logMsg(oss.str());
                                oss << "\t\t\t  IBAN: " << user->iban; logMsg(oss.str());
                                scrState = 3;
                            }
                            else
                            {
                                pinRetry++;
                                if (pinRetry == 3)
                                {
                                    oss << getTimeCli() << "Cardholder entered a wrong PIN 3 times in a row"; logMsg(oss.str());
                                    scrState = 22;
                                    blocked = true;
                                }
                                else
                                {
                                    oss << getTimeCli() << "Cardholder entered a wrong PIN"; logMsg(oss.str());
                                    scrState = 21;
                                }
                            }
                            pin = 0;
                            pinCount = 0;
                            break;
                    }
                }
                break;
            case 3: //- (3) MAIN MENU
                switch (clickableObjectCode)
                {
                    case 1:
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 4;
                        break;
                    case 5:
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 11;
                        break;
                    case 7:
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 17;
                        break;
                }
                break;
            case 4: //- (4) Enter amount (Withdraw)
                if (amountCount < 7)
                {
                    switch (clickableObjectCode)
                    {
                        case 9: //- 1
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 1;
                            amountCount++;
                            break;
                        case 10://- 4
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 4;
                            amountCount++;
                            break;
                        case 11://- 7
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 7;
                            amountCount++;
                            break;
                        case 12://- 2
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 2;
                            amountCount++;
                            break;
                        case 13://- 5
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 5;
                            amountCount++;
                            break;
                        case 14://- 8
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 8;
                            amountCount++;
                            break;
                        case 15://- 0
                            if (amount)
                            {
                                eventRoutine(RoutineCode::KEY_SOUND);
                                amount = amount * 10 + 0;
                                amountCount++;
                            }
                            break;
                        case 16://- 3
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 3;
                            amountCount++;
                            break;
                        case 17://- 6
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 6;
                            amountCount++;
                            break;
                        case 18://- 9
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 9;
                            amountCount++;
                            break;
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amount = 0;
                            amountCount = 0;
                            convert.str("");
                            amountLiveTxt = convert.str();
                            break;
                        case 20://- OK
                            if (amount)
                            {
                                eventRoutine(RoutineCode::MENU_SOUND);
                                amountCount = 0;
                                if (amount <= user->balance)
                                    scrState = 5;
                                else
                                {
                                    scrState = 10;
                                    amount = 0;
                                }
                                amountLiveTxt = "";
                                convert.str("");
                            }
                            break;
                    }
                }
                if (amountCount == 7)
                {
                    switch (clickableObjectCode)
                    {
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amount = 0;
                            amountCount = 0;
                            convert.str("");
                            amountLiveTxt = convert.str();
                            break;
                        case 20://- OK
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amountCount = 0;
                            if (amount <= user->balance)
                                scrState = 5;
                            else
                            {
                                scrState = 10;
                                amount = 0;
                            }
                            amountLiveTxt = "";
                            convert.str("");
                            break;
                    }
                }
                convert.str("");
                convert << amount;
                amountLiveTxt = convert.str();
                break;
            case 5: //- (5) Confirm (Withdraw)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 6;
                        break;
                    case 7: //- No (R3)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 4;
                        amount = 0; amountCount = 0;
                        amountLiveTxt = "";
                        convert.str("");
                        break;
                }
                break;
            case 6: //- (6) Processing (Withdraw)
                eventRoutine(RoutineCode::CASH_LARGE_OUT, [this]() -> void {
                    user->balance = user->balance - amount;
                    oss << getTimeCli() << user->lastName << " " << user->firstName << " withdrew " << amount << " RON"; logMsg(oss.str());
                    amount = 0; amountCount = 0;
                    amountLiveTxt = "";
                    convert.str("");
                    scrState = 7;
                });
                break;
            case 7: //- (7) Receipt? (Withdraw)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        if (!cashLargeVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            eventRoutine(RoutineCode::RECEIPT_OUT);
                            scrState = 8;
                        }
                        break;
                    case 7: //- No (R3)
                        if (!cashLargeVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            scrState = 8;
                        }
                        break;
                    case 22: //- Cash Large
                        vibrate(VibrationDuration::SHORT);
                        cashLargeVisible = false;
                        break;
                }
                break;
            case 8: //- (8) Another transaction? (Withdraw)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        if (!receiptVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            scrState = 3;
                        }
                        break;
                    case 7: //- No (R3)
                        if (!receiptVisible)
                        {
                            if (!cardVisible)
                            {
                                eventRoutine(RoutineCode::MENU_SOUND);
                                oss << getTimeCli() << user->lastName << " " << user->firstName << " finished the session"; logMsg(oss.str());
                                eventRoutine(RoutineCode::CARD_OUT);
                            }
                        }
                        break;
                    case 24: //- Receipt
                        vibrate(VibrationDuration::SHORT);
                        receiptVisible = false;
                        break;
                }
                break;
            case 10: //- (10) Not Enough Funds
                switch (clickableObjectCode)
                {
                    case 7:
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 4;
                        break;
                }
                break;
            case 11: //- (11) Enter Amount (Deposit)
                if (amountCount < 7)
                {
                    switch (clickableObjectCode)
                    {
                        case 9: //- 1
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 1;
                            amountCount++;
                            break;
                        case 10://- 4
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 4;
                            amountCount++;
                            break;
                        case 11://- 7
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 7;
                            amountCount++;
                            break;
                        case 12://- 2
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 2;
                            amountCount++;
                            break;
                        case 13://- 5
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 5;
                            amountCount++;
                            break;
                        case 14://- 8
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 8;
                            amountCount++;
                            break;
                        case 15://- 0
                            if (amount)
                            {
                                eventRoutine(RoutineCode::KEY_SOUND);
                                amount = amount * 10 + 0;
                                amountCount++;
                            }
                            break;
                        case 16://- 3
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 3;
                            amountCount++;
                            break;
                        case 17://- 6
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 6;
                            amountCount++;
                            break;
                        case 18://- 9
                            eventRoutine(RoutineCode::KEY_SOUND);
                            amount = amount * 10 + 9;
                            amountCount++;
                            break;
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amount = 0;
                            amountCount = 0;
                            convert.str("");
                            amountLiveTxt = convert.str();
                            break;
                        case 20://- OK
                            if (amount)
                            {
                                eventRoutine(RoutineCode::MENU_SOUND);
                                amountCount = 0;
                                scrState = 12;
                                amountLiveTxt = "";
                                convert.str("");
                            }
                            break;
                    }
                }
                if (amountCount == 7)
                {
                    switch (clickableObjectCode)
                    {
                        case 19://- Clear
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amount = 0;
                            amountCount = 0;
                            convert.str("");
                            amountLiveTxt = convert.str();
                            break;
                        case 20://- OK
                            eventRoutine(RoutineCode::MENU_SOUND);
                            amountCount = 0;
                            scrState = 12;
                            amountLiveTxt = "";
                            convert.str("");
                            break;
                    }
                }
                convert.str("");
                convert << amount;
                amountLiveTxt = convert.str();
                break;
            case 12: //- Confirm (Deposit)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        cashSmallVisible = true;
                        scrState = 13;
                        break;
                    case 7: //- No (R3)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 11;
                        amount = 0; amountCount = 0;
                        amountLiveTxt = "";
                        convert.str("");
                        break;
                }
                break;
            case 13: //- Insert Cash
                switch (clickableObjectCode)
                {
                    case 23:
                        eventRoutine(RoutineCode::CASH_SMALL_IN, [this]() -> void {
                            scrState = 24;
                        });
                        break;
                }
                break;
            case 14: //- Receipt? (Deposit)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        if (!cashSmallVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            eventRoutine(RoutineCode::RECEIPT_OUT);
                            scrState = 15;
                        }
                        break;
                    case 7: //- No (R3)
                        if (!cashSmallVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            scrState = 15;
                        }
                        break;
                }
                break;
            case 15: //- Another transaction? (Deposit)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        if (!receiptVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            scrState = 3;
                        }
                        break;
                    case 7: //- No (R3)
                        if (!receiptVisible)
                        {
                            if (!cardVisible)
                            {
                                eventRoutine(RoutineCode::MENU_SOUND);
                                oss << getTimeCli() << user->lastName << " " << user->firstName << " finished the session"; logMsg(oss.str());
                                eventRoutine(RoutineCode::CARD_OUT);
                            }
                        }
                        break;
                    case 24: //- Receipt
                        vibrate(VibrationDuration::SHORT);
                        receiptVisible = false;
                        break;
                }
                break;
            case 17: //- Processing (Account Balance)
                handleTimedAction(processingTime, [this]() -> void {
                    oss << getTimeCli() << user->lastName << " " << user->firstName << "'s balance is: " << user->balance << " RON"; logMsg(oss.str());
                    amount = 0; amountCount = 0;
                    amountLiveTxt = "";
                    convert.str("");
                    scrState = 18;
                });
                break;
            case 18: //- Balance = ***. Receipt?
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        eventRoutine(RoutineCode::RECEIPT_OUT);
                        scrState = 19;
                        break;
                    case 7: //- No (R3)
                        eventRoutine(RoutineCode::MENU_SOUND);
                        scrState = 19;
                        break;
                }
                balance.str("");
                balance << user->balance << " RON";
                amountLiveTxt = balance.str();
                break;
            case 19: //- Another Transaction? (Account Balance)
                switch (clickableObjectCode)
                {
                    case 1: //- Yes (L1)
                        if (!receiptVisible)
                        {
                            eventRoutine(RoutineCode::MENU_SOUND);
                            scrState = 3;
                        }
                        break;
                    case 7: //- No (R3)
                        if (!receiptVisible)
                        {
                            if (!cardVisible)
                            {
                                eventRoutine(RoutineCode::MENU_SOUND);
                                oss << getTimeCli() << user->lastName << " " << user->firstName << " finished the session"; logMsg(oss.str());
                                eventRoutine(RoutineCode::CARD_OUT);
                            }
                        }
                        break;
                    case 24: //- Receipt
                        vibrate(VibrationDuration::SHORT);
                        receiptVisible = false;
                        break;
                }
                break;
            case 21: //- (21) Wrong PIN
                if (clickableObjectCode == 20)
                {
                    eventRoutine(RoutineCode::MENU_SOUND);
                    scrState = 2;
                }
                break;
            case 22: //- (22) Account suspended
                if (!accountSuspendedFlag)
                {
                    oss << getTimeCli() << "ACCOUNT SUSPENDED"; logMsg(oss.str());
                    accountSuspendedFlag = true;
                }
                if (clickableObjectCode == 20)
                {
                    eventRoutine(RoutineCode::MENU_SOUND);
                    eventRoutine(RoutineCode::CARD_OUT);
                }
                break;
            case 23: //- (23) Processing (for card in)
                handleTimedAction(processingTime, [this]() -> void {
                    if (!blocked)
                        scrState = 2;
                    else
                        scrState = 22;
                });
                break;
            case 24: //- (24) Processing (deposit)
                handleTimedAction(processingTime, [this]() -> void {
                    user->balance = user->balance + amount;
                    oss << getTimeCli() << user->lastName << " " << user->firstName << " deposited " << amount << " RON"; logMsg(oss.str());
                    amount = 0; amountCount = 0;
                    amountLiveTxt = "";
                    convert.str("");
                    scrState = 14;
                });
                break;
        }

        if (clickableObjectCode == 25) //- Button: Cancel
        {
            if (!cardVisible) {
                eventRoutine(RoutineCode::MENU_SOUND);
                if (scrState != 1 && scrState != 2 && scrState != 21 && scrState != 22 &&
                    scrState != 23) {
                    oss << getTimeCli() << user->lastName << " "
                        << user->firstName << " canceled the session";
                    logMsg(oss.str());
                }
                eventRoutine(RoutineCode::CARD_OUT);
            }
        }
        if (clickableObjectCode == 26) //- Button: Exit
        {
            vibrate(VibrationDuration::SHORT);
            clickSnd.play();
            window.close();
        }

        //- Update animations
        // Although we don't have concurrent object (non-cursor) animations yet,
        //  we have the system to support that
        for (auto iterator = runningAnimations.cbegin(); iterator != runningAnimations.end(); iterator++)
        {
            auto animation = iterator.operator*();
            animation->update(deltaTime);
            // Clean up ended animations
            if (animation->isEnded())
            {
                delete animation;
                runningAnimations.erase(iterator--);
            }
        }
        if (cursorAnimation != nullptr)
        {
            cursorAnimation->update(deltaTime);
        }
    }

    void render(sf::RenderWindow& window)
    {
        window.clear();

        window.draw(backgroundSprite);
        if (cardVisible)
        {
            window.draw(cardSprite);
            window.draw(cardMask);
        }
        if (cashLargeVisible)
        {
            window.draw(cashLargeSprite);
            window.draw(cashLargeMask);
        }
        if (cashSmallVisible)
        {
            window.draw(cashSmallSprite);
            window.draw(cashSmallMask);
        }
        if (receiptVisible)
        {
            window.draw(receiptSprite);
            window.draw(receiptMask);
        }
        scrRender();

#ifdef SHOW_CURSOR
        window.draw(cursorCircle);
#endif

        window.display();
    }

    void scrRender()
    {
        //- Show Live "OK" Instruction
        if (pinCount == 4 || amountCount == 7)
        {
            initSfText(&R3Txt, "Apasati OK", 350, 200, 18, sf::Color::Yellow, sf::Color::Yellow, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Screen Clock Text Setup
        initSfText(&scrClock, getTimeGui(), 490, 25, 13, sf::Color::Red, sf::Color::Red, sf::Text::Bold);
        window.draw(scrClock);

        //- Client Name and IBAN Text Setup
        if (scrState != 1 && scrState != 2 && scrState != 21 && scrState != 22 && scrState != 23)
        {
            initSfText(&usernameScr, usernameScrStr.str(), 85, 25, 13, sf::Color::Cyan, sf::Color::Cyan, sf::Text::Regular);
            window.draw(usernameScr);

            initSfText(&ibanScr, ibanScrStr.str(), 85, 290, 13, sf::Color::White, sf::Color::White, sf::Text::Regular);
            window.draw(ibanScr);
        }

        //- Processing
        if (scrState == 23 || scrState == 17 || scrState == 6 || scrState == 24)
        {
            initSfText(&R3Txt, "In curs de procesare...", 250, 200, 20, sf::Color::Red, sf::Color::Red, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Receipt?
        if (scrState == 7 || scrState == 14 || scrState == 18)
        {
            if (scrState == 18)
            {
                initSfText(&liveTxt, amountLiveTxt, 280, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
                window.draw(liveTxt);
            }
            initSfText(&dialog, "Doriti bonul aferent tranzactiei?", 90, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            initSfText(&L1Txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(L1Txt);
            initSfText(&R3Txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Confirm?
        if (scrState == 5 || scrState == 12)
        {
            initSfText(&dialog, "Confirmare", 255, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            initSfText(&L1Txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(L1Txt);
            initSfText(&R3Txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Another Transaction?
        if (scrState == 8 || scrState == 15 || scrState == 19)
        {
            initSfText(&dialog, "Doriti sa efectuati\no noua tranzactie?", 200, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            initSfText(&L1Txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(L1Txt);
            initSfText(&R3Txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Enter amount
        if (scrState == 4 || scrState == 11)
        {
            initSfText(&dialog, "Introduceti suma", 210, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            pinBorderShape.setPosition(230, 150);
            pinBorderShape.setSize(sf::Vector2f(180, 30));
            pinBorderShape.setFillColor(sf::Color::Black);
            pinBorderShape.setOutlineColor(sf::Color::White);
            pinBorderShape.setOutlineThickness(2);
            window.draw(pinBorderShape);
            initSfText(&liveTxt, amountLiveTxt, 270, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(liveTxt);
            initSfText(&R3Txt, "RON", 425, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
        }

        //- Main Screen Setup
        switch (scrState)
        {
        case 1:
            initSfText(&dialog, "    Bun venit!\nIntroduceti cardul", 180, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            break;
        case 2:
            initSfText(&dialog, "Introduceti codul PIN", 170, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            amountBorderShape.setPosition(230, 150);
            amountBorderShape.setSize(sf::Vector2f(180, 30));
            amountBorderShape.setFillColor(sf::Color::Black);
            amountBorderShape.setOutlineColor(sf::Color::White);
            amountBorderShape.setOutlineThickness(2);
            window.draw(amountBorderShape);
            pinLiveTxt = std::string(pinCount, '*');
            initSfText(&liveTxt, pinLiveTxt, 290, 150, 25, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(liveTxt);
            break;
        case 3:
            initSfText(&L1Txt, "<--- Retragere", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(L1Txt);
            initSfText(&R1Txt, "Depunere --->", 390, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R1Txt);
            initSfText(&R3Txt, "Interogare Sold --->", 300, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
            break;
        case 10:
            initSfText(&dialog, "Sold insuficient", 210, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            initSfText(&R3Txt, "Modificati suma --->", 300, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
            window.draw(R3Txt);
            break;
        case 13:
            initSfText(&dialog, "Plasati numerarul in bancomat", 120, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            break;
        case 21:
            initSfText(&dialog, "Ati introdus un PIN incorect\n        OK | Cancel?", 110, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
            break;
        case 22:
            initSfText(&dialog, "3 incercari succesive eronate\n  Contul dvs este suspendat\n      Apasati tasta OK", 105, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
            window.draw(dialog);
        }
    }

    void addRunningAnimation(Animation* animation)
    {
        runningAnimations.push_back(animation);
    }

    void addCursorAnimation(Animation* animation)
    {
        if (cursorAnimation != nullptr)
        {
            delete cursorAnimation;
            cursorAnimation = nullptr;
        }
        cursorAnimation = animation;
    }

    void eventRoutine(unsigned short int routine, std::function<void()> callback = {})
    {
        //=======================
        //Routine Codes (routine)
        //=======================
        //1 = Card In
        //2 = Card Out
        //3 = Key Sound
        //4 = Menu Sound
        //5 = Cash Large Out
        //6 = Cash Small In
        //7 = Receipt Out
        //======================
        //======================

        if (!canAcceptInput()) return;

        switch (routine)
        {
        case RoutineCode::CARD_IN:
            accountSuspendedFlag = false;
            cardSnd.play();
            vibrate(VibrationDuration::MEDIUM);
            addRunningAnimation(new VerticalOffsetAnimation(
                    cardAnimationTime, cardSpritePosition,
                    VerticalOffsetAnimationType::ORIGIN_TO_TOP,
                    cardSprite.getLocalBounds().height,
                    [this](OffsetAnimationUpdate update) -> void {
                        handleOffsetAnimationUpdate(&cardSprite, &update);
                    },
                    [this, callback]() -> void {
                        oss << getTimeCli() << "The cardholder inserted a VISA Classic Card"; logMsg(oss.str());
                        cardVisible = false;
                        cardSprite.setPosition(cardSpritePosition);
                        vibrate(VibrationDuration::SHORT);
                        if (callback) callback();
                    }
            ));
            break;
        case RoutineCode::CARD_OUT:
            cardSnd.play();
            vibrate(VibrationDuration::MEDIUM);
            cardVisible = true;
            addRunningAnimation(new VerticalOffsetAnimation(
                    cardAnimationTime, cardSpritePosition,
                    VerticalOffsetAnimationType::TOP_TO_ORIGIN,
                    cardSprite.getLocalBounds().height,
                    [this](OffsetAnimationUpdate update) -> void {
                        handleOffsetAnimationUpdate(&cardSprite, &update);
                    },
                    [this, callback]() -> void {
                        oss << getTimeCli() << "The card was ejected"; logMsg(oss.str());
                        cardSprite.setPosition(cardSpritePosition);
                        vibrate(VibrationDuration::SHORT);
                        if (callback) callback();
                        signOut();
                    }
            ));
            break;
        case RoutineCode::KEY_SOUND:
            keySnd.play();
            vibrate(VibrationDuration::SHORT);
            break;
        case RoutineCode::MENU_SOUND:
            menuSnd.play();
            vibrate(VibrationDuration::SHORT);
            break;
        case RoutineCode::CASH_LARGE_OUT:
            cashSnd.play();
            vibrate(VibrationDuration::MEDIUM);
            cashLargeVisible = true;
            addRunningAnimation(new VerticalOffsetAnimation(
                    cashSndBuf.getDuration(), cashLargeSpritePosition,
                    VerticalOffsetAnimationType::TOP_TO_ORIGIN,
                    cashLargeSprite.getLocalBounds().height,
                    [this](OffsetAnimationUpdate update) -> void {
                        handleOffsetAnimationUpdate(&cashLargeSprite, &update);
                    },
                    [this, callback]() -> void {
                        vibrate(VibrationDuration::SHORT);
                        if (callback) callback();
                    }
            ));
            break;
        case RoutineCode::CASH_SMALL_IN:
            cashSnd.play();
            vibrate(VibrationDuration::MEDIUM);
            addRunningAnimation(new VerticalOffsetAnimation(
                    cashSndBuf.getDuration(), cashSmallSpritePosition,
                    VerticalOffsetAnimationType::ORIGIN_TO_TOP,
                    cashSmallSprite.getLocalBounds().height,
                    [this](OffsetAnimationUpdate update) -> void {
                        handleOffsetAnimationUpdate(&cashSmallSprite, &update);
                    },
                    [this, callback]() -> void {
                        cashSmallVisible = false;
                        cashSmallSprite.setPosition(cashSmallSpritePosition);
                        vibrate(VibrationDuration::SHORT);
                        if (callback) callback();
                    }
            ));
            break;
        case RoutineCode::RECEIPT_OUT:
            vibrate(VibrationDuration::MEDIUM);
            printReceiptSnd.play();
            receiptVisible = true;
            addRunningAnimation(new VerticalOffsetAnimation(
                    printReceiptSndBuf.getDuration(), receiptSpritePosition,
                    VerticalOffsetAnimationType::TOP_TO_ORIGIN,
                    receiptSprite.getLocalBounds().height,
                    [this](OffsetAnimationUpdate update) -> void {
                        handleOffsetAnimationUpdate(&receiptSprite, &update);
                    },
                    [this, callback]() -> void {
                        vibrate(VibrationDuration::SHORT);
                        if (callback) callback();
                    }
            ));
            break;
        }
    }

    void loadClients()
    {
        User u;
        int nr, i;
        database >> nr;
        for (i = 0; i < nr; i++)
        {
            database >> u.iban >> u.lastName >> u.firstName >> u.pin >> u.balance;
            users.push_back(u);
        }
    }

    User* findUserByPin(unsigned short pin)
    {
        for (int i = 0; i < users.size(); i++)
        {
            if (users[i].pin == pin)
                return &users[i];
        }
        return nullptr;
    }

    void signOut()
    {
        user = nullptr;
        usernameScrStr.str("");
        ibanScrStr.str("");
        initStates();
    }

    void signIn(User* user)
    {
        this->user = user;
        usernameScrStr << user->lastName << " " << user->firstName;
        ibanScrStr << user->iban;
    }

    void loadPlaceholderClient()
    {
        User u;
        u.iban = "RO-13-ABBK-0345-2342-0255-92";
        u.lastName = "Placeholder";
        u.firstName = "Client";
        u.pin = 0;
        u.balance = 100;
        users.push_back(u);
    }

    std::string programTitle()
    {
        std::ostringstream stream;
        stream << title << " | v" << ver;
        return stream.str();
    }

    std::string serializeTimePoint(const std::chrono::system_clock::time_point& time, const std::string& format)
    {
        std::time_t tt = std::chrono::system_clock::to_time_t(time);
//        std::tm tm = *std::gmtime(&tt); //GMT (UTC)
        std::tm tm = *std::localtime(&tt); //Locale time-zone
        std::stringstream ss;
        ss << std::put_time( &tm, format.c_str() );
        return ss.str();
    }

    std::string getTimeCli()
    {
        std::chrono::time_point<std::chrono::system_clock> current_time =
                std::chrono::system_clock::now();
        return serializeTimePoint(current_time, "%Y-%m-%d | %H:%M:%S --> ");
    }

    std::string getTimeGui()
    {
        std::chrono::time_point<std::chrono::system_clock> current_time =
                std::chrono::system_clock::now();
        return serializeTimePoint(current_time, "%H:%M:%S");
    }

    std::string getLogFileName()
    {
        std::chrono::time_point<std::chrono::system_clock> current_time =
                std::chrono::system_clock::now();
        return "log-" + serializeTimePoint(current_time, "%Y.%m.%d-%H.%M.%S") + ".txt";
    }

    void logMsg(std::string str)
    {
        std::cout << str << std::endl;
        log << str << std::endl;
        oss.str("");
        oss.clear();
    }

    void terminate()
    {
        oss << getTimeCli() << "The ATM is now powered off"; logMsg(oss.str());
        if (log.is_open())
            log.close();
#ifdef TARGET_ANDROID
        androidGlue.release();
#endif
        system("pause");
    }

    void initSfText(sf::Text *pText, const std::string msg, float posX, float posY, unsigned int charSize, const sf::Color colorFill, const sf::Color colorOutline, const sf::Uint32 style)
    {
        pText->setPosition(posX, posY);
        pText->setString(msg);
        pText->setCharacterSize(charSize);
        pText->setFillColor(colorFill);
        pText->setOutlineColor(colorOutline);
        pText->setStyle(style);
    }

    inline std::string res(std::string generalPath)
    {
        return getResFilePath(generalPath);
    }

    inline std::string getResFilePath(std::string generalPath)
    {
#ifdef TARGET_ANDROID
        return generalPath;
#endif
        return "res/" + generalPath;
    }

    void handleTimedAction(sf::Time duration, std::function<void()> action)
    {
        if (actionTimer == nullptr)
        {
            actionTimer = new ActionTimer(duration, [this, action]() -> void {
                action();
                delete actionTimer;
                actionTimer = nullptr;
            });
        }
    }

    void vibrate(VibrationDuration vibrationDuration)
    {
#ifdef TARGET_ANDROID
        androidGlue.vibrate(vibrationDuration);
#endif
    }

public:
    void run()
    {
        init();
        while (window.isOpen())
        {
            sf::Time deltaTime = frameDeltaClock.restart();
            handleEvents();
            handleActionTimer();
            if (windowHasFocus)
            {
                update(deltaTime);
                render(window);
            }
            else
                sf::sleep(sf::milliseconds(16));
        }
        terminate();
    }
};

int main()
{
    Atm atm;
    atm.run();
    return 0;
}
