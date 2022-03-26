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
	ANativeActivity* native_activity_handle;
	JavaVM* vm;
	JNIEnv* env;

	// Vibrate
	jobject vibrate_object;
	jmethodID vibrate_method;

	void init()
	{
		// First we'll need the native activity handle
		native_activity_handle = sf::getNativeActivity();

		// Retrieve the AssetManager so we can read resources from the APK
		asset_manager = native_activity_handle->assetManager;

		// Retrieve the JVM and JNI environment
		vm = native_activity_handle->vm;
		env = native_activity_handle->env;

		// Attach this thread to the main thread
		attach_to_main_thread();

		// Retrieve class information
		jclass native_activity = env->FindClass("android/app/NativeActivity");
		jclass context = env->FindClass("android/content/Context");

		init_vibration(context, native_activity);

		// Free references
		env->DeleteLocalRef(context);
		env->DeleteLocalRef(native_activity);
	}

	bool attach_to_main_thread()
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

	void init_vibration(jclass context, jclass native_activity)
	{
		// Get the value of a constant
		jfieldID vibrator_service_field_id = env->GetStaticFieldID(context, "VIBRATOR_SERVICE", "Ljava/lang/String;");
		jobject vibrator_service_object = env->GetStaticObjectField(context, vibrator_service_field_id);

		// Get the method 'getSystemService' and call it
		jmethodID get_system_service_method_id = env->GetMethodID(native_activity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
		vibrate_object = env->CallObjectMethod(native_activity_handle->clazz, get_system_service_method_id, vibrator_service_object);

		// Get the object's class and retrieve the member name
		jclass vibrate_class = env->GetObjectClass(vibrate_object);
		vibrate_method = env->GetMethodID(vibrate_class, "vibrate", "(J)V");

		// Free references
		env->DeleteLocalRef(vibrate_class);
		env->DeleteLocalRef(vibrator_service_object);
	}

public:
	// Asset Manager
	AAssetManager* asset_manager;

	AndroidGlue()
	{
		init();
	}

	void vibrate(int duration_millis)
	{
		jlong duration_millis_jlong = duration_millis;
		// Bzzz!
		env->CallVoidMethod(vibrate_object, vibrate_method, duration_millis_jlong);
	}

	void release()
	{
		// Free references
		env->DeleteLocalRef(vibrate_object);

		// Detach thread again
		vm->DetachCurrentThread();
	}
};

#endif

class Animation
{
protected:
	sf::Time duration;
	sf::Clock running_time_clock;

	Animation(sf::Time duration)
	{
		this->duration = duration;
	}

public:
	virtual ~Animation() = default;
	virtual void update(sf::Time delta_time) = 0;
};

class OffsetAnimation : public Animation
{
private:
	sf::Sprite* sprite;

protected:
	sf::Vector2f target_offset;

	OffsetAnimation(sf::Time duration, sf::Sprite* sprite) : Animation(duration)
	{
		this->sprite = sprite;
	}

public:
	OffsetAnimation(sf::Time duration, sf::Sprite* sprite,
					sf::Vector2f start_position, sf::Vector2f target_offset) :
					OffsetAnimation(duration, sprite)
	{
		setStartPosition(start_position);
		this->target_offset = target_offset;
	}

	virtual ~OffsetAnimation() = default;

	void update(sf::Time delta_time)
	{
		if (running_time_clock.getElapsedTime() <= duration)
		{
			float offset_x = (delta_time * target_offset.x) / duration;
			float offset_y = (delta_time * target_offset.y) / duration;
			sprite->move(offset_x, offset_y);
		}
	}

	void setStartPosition(sf::Vector2f start_position)
	{
		sprite->setPosition(start_position);
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
			sf::Sprite* sprite,
			sf::Vector2f origin_position,
			VerticalOffsetAnimationType type
	) : OffsetAnimation(duration, sprite)
	{
		switch (type) {
			case VerticalOffsetAnimationType::TOP_TO_ORIGIN:
				setStartPosition(sf::Vector2f(
						origin_position.x, origin_position.y - sprite->getLocalBounds().height));
				this->target_offset = sf::Vector2f(0, sprite->getLocalBounds().height);
				break;
			case VerticalOffsetAnimationType::ORIGIN_TO_TOP:
				setStartPosition(origin_position);
				this->target_offset = sf::Vector2f(0, -sprite->getLocalBounds().height);
				break;
		}
	}

	~VerticalOffsetAnimation() = default;
};

class ActionTimer
{
private:
	sf::Clock clock;
	sf::Time target_duration;
	std::function<void()> callback;

	void release()
	{
		delete this;
	}

public:
	ActionTimer(sf::Time target_duration, std::function<void()> callback)
	{
		this->target_duration = target_duration;
		this->callback = callback;
	}

	void startTimer(sf::Time target_duration, std::function<void()> callback)
	{
		this->target_duration = target_duration;
		this->callback = callback;
	}

	void update()
	{
		sf::Time elapsed_time = clock.getElapsedTime();
		if (elapsed_time <= target_duration) return;
		callback();
		release();
	}
};

class Atm
{
private:
	//- Title and Version
	std::string title = "ATM Software RELOADED";
	std::string ver = "1.0";

	//- Screen Size
	const int CANVAS_WIDTH = 960, CANVAS_HEIGHT = 620;
	sf::Vector2u currentWindowSize;

	//- States
	bool card_visible = true, cash_large_visible = false, cash_small_visible = false, receipt_visible = false;
	unsigned short int scr_state = 1;
	unsigned short int pin = 0; unsigned short int pin_count = 0; unsigned short int pin_retry = 0;
	int amount = 0; unsigned short int amount_count = 0;
	bool blocked = false;
	bool accountSuspendedFlag = false;
	bool windowHasFocus = true;

	//- User Data Structure
	struct User
	{
		std::string iban;
		std::string last_name;
		std::string first_name;
		unsigned short int pin;
		unsigned long long int balance;
	};

	//- General
	sf::View view;
	sf::VideoMode screen;
	sf::RenderWindow window;
	sf::Event event;
	sf::Font font;
	sf::Clock frame_clock;
	sf::Clock clock;
	sf::Time elapsed;

	//- Textures and Sprites
	sf::Texture backgnd_texture;           sf::Sprite backgnd_sprite;

	sf::Texture card_texture;              sf::Sprite card_sprite;
	sf::Vector2f card_sprite_position = sf::Vector2f(740, 198);
	sf::RectangleShape card_mask;

	sf::Texture cash_large_texture;        sf::Sprite cash_large_sprite;
	sf::Vector2f cash_large_sprite_position = sf::Vector2f(90, 370);
	sf::RectangleShape cash_large_mask;

	sf::Texture cash_small_texture;        sf::Sprite cash_small_sprite;
	sf::Vector2f cash_small_sprite_position = sf::Vector2f(695, 463);
	sf::RectangleShape cash_small_mask;

	sf::Texture receipt_texture;           sf::Sprite receipt_sprite;
	sf::Vector2f receipt_sprite_position = sf::Vector2f(740, 54);
	sf::RectangleShape receipt_mask;

	//- Sound Buffers and Sounds
	sf::SoundBuffer card_snd_buf;          sf::Sound card_snd;
	sf::SoundBuffer menu_snd_buf;          sf::Sound menu_snd;
	sf::SoundBuffer click_snd_buf;         sf::Sound click_snd;
	sf::SoundBuffer key_snd_buf;           sf::Sound key_snd;
	sf::SoundBuffer cash_snd_buf;          sf::Sound cash_snd;
	sf::SoundBuffer print_receipt_snd_buf; sf::Sound print_receipt_snd;

	//- Processing Time
	sf::Time processing_time = sf::seconds(2);

	//- Text
	sf::Text scr_clock;
	sf::Text username_scr;
	sf::Text iban_scr;
	sf::Text L1_txt;
	sf::Text R1_txt;
	sf::Text R3_txt;
	sf::Text dialog;
	sf::Text live_txt;

	//- Shapes
	sf::RectangleShape pin_border_shape;
	sf::RectangleShape amount_border_shape;

	//- Users
	std::vector<User> users;
	User* user;

	//- Text files
	const std::string database_path = "database/database.txt";
	std::stringstream database;
	std::ofstream log;

	//- Out String Stream
	std::ostringstream oss;

	//- Screen Info Strings
	std::ostringstream username_scr_str;
	std::ostringstream iban_scr_str;
	std::ostringstream convert;
	std::ostringstream balance;

	//- Chat arrays for live text
	std::string pin_live_txt = "****"; std::string amount_live_txt = "";

	//- Outstanding Click / Touch Event
	sf::Vector2i* outstanding_interaction_event;

	//- Cursor
	const int CURSOR_CIRCLE_RADIUS = 16;
    sf::CircleShape cursorCircle = sf::CircleShape(CURSOR_CIRCLE_RADIUS);

    //- Action Timer
    ActionTimer* actionTimer;

    //- Animations
    Animation* running_animation = nullptr;
    sf::Time card_animation_time = sf::milliseconds(1002);

    //- Frame Delta Clock
    sf::Clock frame_delta_clock;

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
	AndroidGlue android_glue;
#endif

	void init_win()
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
		window.create(screen, program_title(), sf::Style::Titlebar | sf::Style::Close);
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

	sf::Vector2i* get_scaled_pointer_coordinates(int originalX, int originalY)
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

	void load_database()
    {
#ifdef TARGET_ANDROID
        // In order to properly read asset files in android, we use the Asset NDK Module
        // Links:
        // https://developer.android.com/ndk/reference/group/asset
        // https://stackoverflow.com/a/33957074
		if (android_glue.asset_manager != nullptr)
		{
			// Open your file
			std::string file_path = res(database_path);
			AAsset* file = AAssetManager_open(android_glue.asset_manager, file_path.c_str(), AASSET_MODE_BUFFER);
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
        std::ifstream file_stream(res(database_path));
        if (file_stream.is_open())
        {
            database << file_stream.rdbuf();
            file_stream.close();
        }
#endif

        if (database.tellp() > std::streampos(0))
        {
            oss << get_time_cli() << "User database loaded"; log_msg(oss.str());
            load_clients();
        }
        else
        {
            oss << get_time_cli() << "User database not found"; log_msg(oss.str());
            load_placeholder_client();
        }
    }

	void init()
	{
		//- Create new log file
#ifndef TARGET_ANDROID
		log.open(get_name_log().c_str());
#endif

		//- Init States
		init_states();

		//- Initialize Window
		init_win();

		//- Initialize CLI
		system("color 0A");
		oss << "================================================================================"; log_msg(oss.str());
		oss << "==================================ATM Software=================================="; log_msg(oss.str());
		oss << "================================================================================"; log_msg(oss.str());
		oss << get_time_cli() << "ATM is now powered on"; log_msg(oss.str());

        //- Load database
		load_database();

		//- Load fonts
		if (font.loadFromFile(res("courier_new.ttf")))
		{
			oss << get_time_cli() << "Font loaded"; log_msg(oss.str());
		}
		else
		{
			oss << get_time_cli() << "Font not found"; log_msg(oss.str());
			window.close();
		}

		//- Load textures
		if (!backgnd_texture.loadFromFile(res("backgnd_texture.png")) ||
			!card_texture.loadFromFile(res("card_texture.png")) ||
			!cash_large_texture.loadFromFile(res("cash_large_texture.jpg")) ||
			!cash_small_texture.loadFromFile(res("cash_small_texture.jpg")) ||
			!receipt_texture.loadFromFile(res("receipt_texture.jpg")))
		{
			oss << get_time_cli() << "One or more textures not found"; log_msg(oss.str());
			window.close();
		}
		else
		{
			oss << get_time_cli() << "Textures loaded"; log_msg(oss.str());
		}

		//- Load sounds in the sound buffer
		std::vector<sf::SoundBuffer*> sound_ptr = { &card_snd_buf, &menu_snd_buf, &click_snd_buf, &key_snd_buf, &cash_snd_buf, &print_receipt_snd_buf };
		std::vector<std::string> sound_arr = {
		        res("card_snd.wav"),
		        res("menu_snd.wav"),
		        res("click_snd.wav"),
		        res("key_snd.wav"),
		        res("cash_snd.wav"),
		        res("print_receipt_snd.wav")
		};
		bool sound_ok = true;
		//if (sound_ptr.size() != sound_arr.size()) {  }
		for (int i = 0; i < sound_ptr.size(); ++i)
		{
			if (!sound_ptr[i]->loadFromFile(sound_arr[i]))
			{
				oss << get_time_cli() << "\"" << sound_arr[i] << "\" not found"; log_msg(oss.str());
				window.close();
				sound_ok = false;
				break;
			}
		}
		if(sound_ok)
			oss << get_time_cli() << "Sounds loaded"; log_msg(oss.str());

		//- Ready to go
		oss << get_time_cli() << "ATM is ready to use"; log_msg(oss.str());

		//- Assign font to text
		scr_clock.setFont(font);
		username_scr.setFont(font);
		iban_scr.setFont(font);
		L1_txt.setFont(font);
		R1_txt.setFont(font);
		R3_txt.setFont(font);
		dialog.setFont(font);
		live_txt.setFont(font);

		//- Assign texture to sprite
		backgnd_sprite.setTexture(backgnd_texture);

		sf::IntRect ir;

		ir = sf::IntRect(716, 0, 197, 198);
		card_sprite.setTexture(card_texture);             			 			card_sprite.setPosition(card_sprite_position);
		card_mask.setSize(sf::Vector2f(ir.width, ir.height));		 		card_mask.setPosition(ir.left, ir.top);
		card_mask.setTexture(&backgnd_texture);			  			 	card_mask.setTextureRect(ir);

		ir = sf::IntRect(80, 0, 484, 370);
		cash_large_sprite.setTexture(cash_large_texture); 			 			cash_large_sprite.setPosition(cash_large_sprite_position);
		cash_large_mask.setSize(sf::Vector2f(ir.width, ir.height));  		cash_large_mask.setPosition(ir.left, ir.top);
		cash_large_mask.setTexture(&backgnd_texture);	  			 	cash_large_mask.setTextureRect(ir);

		ir = sf::IntRect(688, 250, 250, 213);
		cash_small_sprite.setTexture(cash_small_texture); 			 			cash_small_sprite.setPosition(cash_small_sprite_position);
		cash_small_mask.setSize(sf::Vector2f(ir.width, ir.height));  		cash_small_mask.setPosition(ir.left, ir.top);
		cash_small_mask.setTexture(&backgnd_texture);				 	cash_small_mask.setTextureRect(ir);

		ir = sf::IntRect(716, 0, 197, 54);
		receipt_sprite.setTexture(receipt_texture);       			  			receipt_sprite.setPosition(receipt_sprite_position);
		receipt_mask.setSize(sf::Vector2f(ir.width, ir.height));      		receipt_mask.setPosition(ir.left, ir.top);
		receipt_mask.setTexture(&backgnd_texture);				      	receipt_mask.setTextureRect(ir);

		//- Assign buffer to sounds
		card_snd.setBuffer(card_snd_buf);
		menu_snd.setBuffer(menu_snd_buf);
		click_snd.setBuffer(click_snd_buf);
		key_snd.setBuffer(key_snd_buf);
		cash_snd.setBuffer(cash_snd_buf);
		print_receipt_snd.setBuffer(print_receipt_snd_buf);

		// Cursor
		cursorCircle.setFillColor(sf::Color(255, 0, 0, 127));
	}

	void init_states()
	{
		//- Initialize States
		card_visible = true; cash_large_visible = false; cash_small_visible = false; receipt_visible = false;
		scr_state = 1;
		pin = 0; pin_count = 0; pin_retry = 0;
		amount = 0; amount_count = 0;
		amount_live_txt = "";
		convert.str("");
		balance.str("");
		outstanding_interaction_event = nullptr;
		actionTimer = nullptr;
	}

	void handle_action_timer()
	{
		if (actionTimer != nullptr)
		{
			actionTimer->update();
		}
	}

	void handle_events()
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
					update_pointer_location(event.touch.x, event.touch.y);
				break;
			case sf::Event::MouseButtonPressed:
				update_pointer_location(event.mouseButton.x, event.mouseButton.y);
				break;
			}
		}
	}

	void update_pointer_location(int rawX, int rawY)
	{
		sf::Vector2i* position = get_scaled_pointer_coordinates(rawX, rawY);
		cursorCircle.setPosition(
				position->x - CURSOR_CIRCLE_RADIUS / (float) 2,
				position->y - CURSOR_CIRCLE_RADIUS / (float) 2
		);
		if (actionTimer != nullptr) {
			delete position;
			return; // We have an ongoing timed action, so don't process the event
		}
		if (outstanding_interaction_event != nullptr) delete outstanding_interaction_event;
		outstanding_interaction_event = position;
	}

	int get_clickable_object_code(int x, int y)
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
		//cash_large = 22
		//cash_small = 23
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
		if ((209 <= x) && (x <= 255) && !cash_large_visible) //- Column 1 Keypad
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
		if ((264 <= x) && (x <= 310) && !cash_large_visible) //- Column 2 Keypad
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
		if ((319 <= x) && (x <= 365) && !cash_large_visible) //- Column 3 Keypad
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
		if ((385 <= x) && (x <= 455) && !cash_large_visible) //- Column 4 Keypad
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
		if ((card_sprite.getGlobalBounds().left <= x) && (x <= (card_sprite.getGlobalBounds().left + card_sprite.getGlobalBounds().width))) //- Object: card (x axis)
		{
			if ((card_sprite.getGlobalBounds().top <= y) && (y <= (card_sprite.getGlobalBounds().top + card_sprite.getGlobalBounds().height)) && card_visible) //- Object: card (y axis) and visibility
			{
				return 21;
			}
		}
		if ((cash_large_sprite.getGlobalBounds().left <= x) && (x <= (cash_large_sprite.getGlobalBounds().left + cash_large_sprite.getGlobalBounds().width))) //- Object: cash_large (x axis)
		{
			if ((cash_large_sprite.getGlobalBounds().top <= y) && (y <= (cash_large_sprite.getGlobalBounds().top + cash_large_sprite.getGlobalBounds().height)) && cash_large_visible) //- Object: cash_large (y axis) and visibility
			{
				return 22;
			}
		}
		if ((cash_small_sprite.getGlobalBounds().left <= x) && (x <= (cash_small_sprite.getGlobalBounds().left + cash_small_sprite.getGlobalBounds().width))) //- Object: cash_small (x axis)
		{
			if ((cash_small_sprite.getGlobalBounds().top <= y) && (y <= (cash_small_sprite.getGlobalBounds().top + cash_small_sprite.getGlobalBounds().height)) && cash_small_visible) //- Object: cash_small (y axis) and visibility
			{
				return 23;
			}
		}
		if ((receipt_sprite.getGlobalBounds().left <= x) && (x <= (receipt_sprite.getGlobalBounds().left + receipt_sprite.getGlobalBounds().width))) //- Object: receipt (x axis)
		{
			if ((receipt_sprite.getGlobalBounds().top <= y) && (y <= (receipt_sprite.getGlobalBounds().top + receipt_sprite.getGlobalBounds().height)) && receipt_visible) //- Object: receipt (y axis) and visibility
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

	void update(sf::Time delta_time)
	{
		//======================================================================================================================================================================================================================
		//Screen States (scr_state)
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
		if (outstanding_interaction_event != nullptr)
		{
			clickableObjectCode = get_clickable_object_code(outstanding_interaction_event->x, outstanding_interaction_event->y);
			delete outstanding_interaction_event;
			outstanding_interaction_event = nullptr;
		}

		switch (scr_state)
		{
			case 1: //- (1) Insert Card
				if (clickableObjectCode == 21)
				{
					event_routine(RoutineCode::CARD_IN, [this]() -> void {
						scr_state = 23;
					});
				}
				break;
			case 2: //- (2) Insert PIN
				if (pin_count < 4)
				{
					switch (clickableObjectCode)
					{
						case 9: //- 1
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 1;
							pin_count++;
							break;
						case 10://- 4
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 4;
							pin_count++;
							break;
						case 11://- 7
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 7;
							pin_count++;
							break;
						case 12://- 2
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 2;
							pin_count++;
							break;
						case 13://- 5
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 5;
							pin_count++;
							break;
						case 14://- 8
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 8;
							pin_count++;
							break;
						case 15://- 0
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 0;
							pin_count++;
							break;
						case 16://- 3
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 3;
							pin_count++;
							break;
						case 17://- 6
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 6;
							pin_count++;
							break;
						case 18://- 9
							event_routine(RoutineCode::KEY_SOUND);
							pin = pin * 10 + 9;
							pin_count++;
							break;
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							pin_count = 0;
							pin = 0;
							break;
					}
				}
				if (pin_count == 4)
				{
					switch (clickableObjectCode)
					{
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							pin_count = 0;
							pin = 0;
							break;
						case 20://- OK
							event_routine(RoutineCode::MENU_SOUND);
							User* user_lookup_result = find_user_by_pin(pin);
							if (user_lookup_result != nullptr)
							{
								sign_in(user_lookup_result);
								oss << get_time_cli() << "Cardholder successfully authenticated:"; log_msg(oss.str());
								oss << "\t\t\t  Full Name: " << user->last_name << " " << user->first_name; log_msg(oss.str());
								oss << "\t\t\t  IBAN: " << user->iban; log_msg(oss.str());
								scr_state = 3;
							}
							else
							{
								pin_retry++;
								if (pin_retry == 3)
								{
									oss << get_time_cli() << "Cardholder entered a wrong PIN 3 times in a row"; log_msg(oss.str());
									scr_state = 22;
									blocked = true;
								}
								else
								{
									oss << get_time_cli() << "Cardholder entered a wrong PIN"; log_msg(oss.str());
									scr_state = 21;
								}
							}
							pin = 0;
							pin_count = 0;
							break;
					}
				}
				break;
			case 3: //- (3) MAIN MENU
				switch (clickableObjectCode)
				{
					case 1:
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 4;
						break;
					case 5:
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 11;
						break;
					case 7:
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 17;
						break;
				}
				break;
			case 4: //- (4) Enter amount (Withdraw)
				if (amount_count < 7)
				{
					switch (clickableObjectCode)
					{
						case 9: //- 1
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 1;
							amount_count++;
							break;
						case 10://- 4
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 4;
							amount_count++;
							break;
						case 11://- 7
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 7;
							amount_count++;
							break;
						case 12://- 2
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 2;
							amount_count++;
							break;
						case 13://- 5
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 5;
							amount_count++;
							break;
						case 14://- 8
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 8;
							amount_count++;
							break;
						case 15://- 0
							if (amount)
							{
								event_routine(RoutineCode::KEY_SOUND);
								amount = amount * 10 + 0;
								amount_count++;
							}
							break;
						case 16://- 3
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 3;
							amount_count++;
							break;
						case 17://- 6
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 6;
							amount_count++;
							break;
						case 18://- 9
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 9;
							amount_count++;
							break;
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							amount = 0;
							amount_count = 0;
							convert.str("");
							amount_live_txt = convert.str();
							break;
						case 20://- OK
							if (amount)
							{
								event_routine(RoutineCode::MENU_SOUND);
								amount_count = 0;
								if (amount <= user->balance)
									scr_state = 5;
								else
								{
									scr_state = 10;
									amount = 0;
								}
								amount_live_txt = "";
								convert.str("");
							}
							break;
					}
				}
				if (amount_count == 7)
				{
					switch (clickableObjectCode)
					{
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							amount = 0;
							amount_count = 0;
							convert.str("");
							amount_live_txt = convert.str();
							break;
						case 20://- OK
							event_routine(RoutineCode::MENU_SOUND);
							amount_count = 0;
							if (amount <= user->balance)
								scr_state = 5;
							else
							{
								scr_state = 10;
								amount = 0;
							}
							amount_live_txt = "";
							convert.str("");
							break;
					}
				}
				convert.str("");
				convert << amount;
				amount_live_txt = convert.str();
				break;
			case 5: //- (5) Confirm (Withdraw)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 6;
						break;
					case 7: //- No (R3)
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 4;
						amount = 0; amount_count = 0;
						amount_live_txt = "";
						convert.str("");
						break;
				}
				break;
			case 6: //- (6) Processing (Withdraw)
				event_routine(RoutineCode::CASH_LARGE_OUT, [this]() -> void {
					user->balance = user->balance - amount;
					oss << get_time_cli() << user->last_name << " " << user->first_name << " withdrew " << amount << " RON"; log_msg(oss.str());
					amount = 0; amount_count = 0;
					amount_live_txt = "";
					convert.str("");
					scr_state = 7;
				});
				break;
			case 7: //- (7) Receipt? (Withdraw)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						if (!cash_large_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							event_routine(RoutineCode::RECEIPT_OUT);
							scr_state = 8;
						}
						break;
					case 7: //- No (R3)
						if (!cash_large_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							scr_state = 8;
						}
						break;
					case 22: //- Cash Large
						vibrate(VibrationDuration::SHORT);
						cash_large_visible = false;
						break;
				}
				break;
			case 8: //- (8) Another transaction? (Withdraw)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						if (!receipt_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							scr_state = 3;
						}
						break;
					case 7: //- No (R3)
						if (!receipt_visible)
						{
							if (!card_visible)
							{
								event_routine(RoutineCode::MENU_SOUND);
								oss << get_time_cli() << user->last_name << " " << user->first_name << " finished the session"; log_msg(oss.str());
								event_routine(RoutineCode::CARD_OUT);
							}
						}
						break;
					case 24: //- Receipt
						vibrate(VibrationDuration::SHORT);
						receipt_visible = false;
						break;
				}
				break;
			case 10: //- (10) Not Enough Funds
				switch (clickableObjectCode)
				{
					case 7:
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 4;
						break;
				}
				break;
			case 11: //- (11) Enter Amount (Deposit)
				if (amount_count < 7)
				{
					switch (clickableObjectCode)
					{
						case 9: //- 1
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 1;
							amount_count++;
							break;
						case 10://- 4
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 4;
							amount_count++;
							break;
						case 11://- 7
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 7;
							amount_count++;
							break;
						case 12://- 2
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 2;
							amount_count++;
							break;
						case 13://- 5
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 5;
							amount_count++;
							break;
						case 14://- 8
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 8;
							amount_count++;
							break;
						case 15://- 0
							if (amount)
							{
								event_routine(RoutineCode::KEY_SOUND);
								amount = amount * 10 + 0;
								amount_count++;
							}
							break;
						case 16://- 3
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 3;
							amount_count++;
							break;
						case 17://- 6
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 6;
							amount_count++;
							break;
						case 18://- 9
							event_routine(RoutineCode::KEY_SOUND);
							amount = amount * 10 + 9;
							amount_count++;
							break;
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							amount = 0;
							amount_count = 0;
							convert.str("");
							amount_live_txt = convert.str();
							break;
						case 20://- OK
							if (amount)
							{
								event_routine(RoutineCode::MENU_SOUND);
								amount_count = 0;
								scr_state = 12;
								amount_live_txt = "";
								convert.str("");
							}
							break;
					}
				}
				if (amount_count == 7)
				{
					switch (clickableObjectCode)
					{
						case 19://- Clear
							event_routine(RoutineCode::MENU_SOUND);
							amount = 0;
							amount_count = 0;
							convert.str("");
							amount_live_txt = convert.str();
							break;
						case 20://- OK
							event_routine(RoutineCode::MENU_SOUND);
							amount_count = 0;
							scr_state = 12;
							amount_live_txt = "";
							convert.str("");
							break;
					}
				}
				convert.str("");
				convert << amount;
				amount_live_txt = convert.str();
				break;
			case 12: //- Confirm (Deposit)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						event_routine(RoutineCode::MENU_SOUND);
						cash_small_visible = true;
						scr_state = 13;
						break;
					case 7: //- No (R3)
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 11;
						amount = 0; amount_count = 0;
						amount_live_txt = "";
						convert.str("");
						break;
				}
				break;
			case 13: //- Insert Cash
				switch (clickableObjectCode)
				{
					case 23:
						event_routine(RoutineCode::CASH_SMALL_IN, [this]() -> void {
							scr_state = 24;
						});
						break;
				}
				break;
			case 14: //- Receipt? (Deposit)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						if (!cash_small_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							event_routine(RoutineCode::RECEIPT_OUT);
							scr_state = 15;
						}
						break;
					case 7: //- No (R3)
						if (!cash_small_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							scr_state = 15;
						}
						break;
				}
				break;
			case 15: //- Another transaction? (Deposit)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						if (!receipt_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							scr_state = 3;
						}
						break;
					case 7: //- No (R3)
						if (!receipt_visible)
						{
							if (!card_visible)
							{
								event_routine(RoutineCode::MENU_SOUND);
								oss << get_time_cli() << user->last_name << " " << user->first_name << " finished the session"; log_msg(oss.str());
								event_routine(RoutineCode::CARD_OUT);
							}
						}
						break;
					case 24: //- Receipt
						vibrate(VibrationDuration::SHORT);
						receipt_visible = false;
						break;
				}
				break;
			case 17: //- Processing (Account Balance)
				handle_timed_action(processing_time, [this]() -> void {
					oss << get_time_cli() << user->last_name << " " << user->first_name << "'s balance is: " << user->balance << " RON"; log_msg(oss.str());
					amount = 0; amount_count = 0;
					amount_live_txt = "";
					convert.str("");
					scr_state = 18;
				});
				break;
			case 18: //- Balance = ***. Receipt?
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						event_routine(RoutineCode::MENU_SOUND);
						event_routine(RoutineCode::RECEIPT_OUT);
						scr_state = 19;
						break;
					case 7: //- No (R3)
						event_routine(RoutineCode::MENU_SOUND);
						scr_state = 19;
						break;
				}
				balance.str("");
				balance << user->balance << " RON";
				amount_live_txt = balance.str();
				break;
			case 19: //- Another Transaction? (Account Balance)
				switch (clickableObjectCode)
				{
					case 1: //- Yes (L1)
						if (!receipt_visible)
						{
							event_routine(RoutineCode::MENU_SOUND);
							scr_state = 3;
						}
						break;
					case 7: //- No (R3)
						if (!receipt_visible)
						{
							if (!card_visible)
							{
								event_routine(RoutineCode::MENU_SOUND);
								oss << get_time_cli() << user->last_name << " " << user->first_name << " finished the session"; log_msg(oss.str());
								event_routine(RoutineCode::CARD_OUT);
							}
						}
						break;
					case 24: //- Receipt
						vibrate(VibrationDuration::SHORT);
						receipt_visible = false;
						break;
				}
				break;
			case 21: //- (21) Wrong PIN
				if (clickableObjectCode == 20)
				{
					event_routine(RoutineCode::MENU_SOUND);
					scr_state = 2;
				}
				break;
			case 22: //- (22) Account suspended
				if (!accountSuspendedFlag)
				{
					oss << get_time_cli() << "ACCOUNT SUSPENDED"; log_msg(oss.str());
					accountSuspendedFlag = true;
				}
				if (clickableObjectCode == 20)
				{
					event_routine(RoutineCode::MENU_SOUND);
					event_routine(RoutineCode::CARD_OUT);
				}
				break;
			case 23: //- (23) Processing (for card in)
				handle_timed_action(processing_time, [this]() -> void {
					if (!blocked)
						scr_state = 2;
					else
						scr_state = 22;
				});
				break;
			case 24: //- (24) Processing (deposit)
				handle_timed_action(processing_time, [this]() -> void {
					user->balance = user->balance + amount;
					oss << get_time_cli() << user->last_name << " " << user->first_name << " deposited " << amount << " RON"; log_msg(oss.str());
					amount = 0; amount_count = 0;
					amount_live_txt = "";
					convert.str("");
					scr_state = 14;
				});
				break;
		}

		if (clickableObjectCode == 25) //- Button: Cancel
		{
			if (!card_visible) {
				event_routine(RoutineCode::MENU_SOUND);
				if (scr_state != 1 && scr_state != 2 && scr_state != 21 && scr_state != 22 &&
					scr_state != 23) {
					oss << get_time_cli() << user->last_name << " "
						<< user->first_name << " canceled the session";
					log_msg(oss.str());
				}
				event_routine(RoutineCode::CARD_OUT);
			}
		}
		if (clickableObjectCode == 26) //- Button: Exit
		{
			vibrate(VibrationDuration::SHORT);
			click_snd.play();
			window.close();
		}

		// Update animations
		if (running_animation != nullptr)
		{
			running_animation->update(delta_time);
		}
	}

	void render(sf::RenderWindow& window)
	{
		window.clear();

		window.draw(backgnd_sprite);
		if (card_visible)
		{
			window.draw(card_sprite);
			window.draw(card_mask);
		}
		if (cash_large_visible)
		{
			window.draw(cash_large_sprite);
			window.draw(cash_large_mask);
		}
		if (cash_small_visible)
		{
			window.draw(cash_small_sprite);
			window.draw(cash_small_mask);
		}
		if (receipt_visible)
		{
			window.draw(receipt_sprite);
			window.draw(receipt_mask);
		}
		scr_render();

#ifdef SHOW_CURSOR
		window.draw(cursorCircle);
#endif

		window.display();
	}

	void scr_render()
	{
		//- Show Live "OK" Instruction
		if (pin_count == 4 || amount_count == 7)
		{
			init_sf_text(&R3_txt, "Apasati OK", 350, 200, 18, sf::Color::Yellow, sf::Color::Yellow, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Screen Clock Text Setup
		init_sf_text(&scr_clock, get_time_gui(), 490, 25, 13, sf::Color::Red, sf::Color::Red, sf::Text::Bold);
		window.draw(scr_clock);

		//- Client Name and IBAN Text Setup
		if (scr_state != 1 && scr_state != 2 && scr_state != 21 && scr_state != 22 && scr_state != 23)
		{
			init_sf_text(&username_scr, username_scr_str.str(), 85, 25, 13, sf::Color::Cyan, sf::Color::Cyan, sf::Text::Regular);
			window.draw(username_scr);

			init_sf_text(&iban_scr, iban_scr_str.str(), 85, 290, 13, sf::Color::White, sf::Color::White, sf::Text::Regular);
			window.draw(iban_scr);
		}

		//- Processing
		if (scr_state == 23 || scr_state == 17 || scr_state == 6 || scr_state == 24)
		{
			init_sf_text(&R3_txt, "In curs de procesare...", 250, 200, 20, sf::Color::Red, sf::Color::Red, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Receipt?
		if (scr_state == 7 || scr_state == 14 || scr_state == 18)
		{
			if (scr_state == 18)
			{
				init_sf_text(&live_txt, amount_live_txt, 280, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
				window.draw(live_txt);
			}
			init_sf_text(&dialog, "Doriti bonul aferent tranzactiei?", 90, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			init_sf_text(&L1_txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(L1_txt);
			init_sf_text(&R3_txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Confirm?
		if (scr_state == 5 || scr_state == 12)
		{
			init_sf_text(&dialog, "Confirmare", 255, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			init_sf_text(&L1_txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(L1_txt);
			init_sf_text(&R3_txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Another Transaction?
		if (scr_state == 8 || scr_state == 15 || scr_state == 19)
		{
			init_sf_text(&dialog, "Doriti sa efectuati\no noua tranzactie?", 200, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			init_sf_text(&L1_txt, "<--- Da", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(L1_txt);
			init_sf_text(&R3_txt, "Nu --->", 465, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Enter amount
		if (scr_state == 4 || scr_state == 11)
		{
			init_sf_text(&dialog, "Introduceti suma", 210, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			pin_border_shape.setPosition(230, 150);
			pin_border_shape.setSize(sf::Vector2f(180, 30));
			pin_border_shape.setFillColor(sf::Color::Black);
			pin_border_shape.setOutlineColor(sf::Color::White);
			pin_border_shape.setOutlineThickness(2);
			window.draw(pin_border_shape);
			init_sf_text(&live_txt, amount_live_txt, 270, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(live_txt);
			init_sf_text(&R3_txt, "RON", 425, 150, 23, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
		}

		//- Main Screen Setup
		switch (scr_state)
		{
		case 1:
			init_sf_text(&dialog, "    Bun venit!\nIntroduceti cardul", 180, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			break;
		case 2:
			init_sf_text(&dialog, "Introduceti codul PIN", 170, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			amount_border_shape.setPosition(230, 150);
			amount_border_shape.setSize(sf::Vector2f(180, 30));
			amount_border_shape.setFillColor(sf::Color::Black);
			amount_border_shape.setOutlineColor(sf::Color::White);
			amount_border_shape.setOutlineThickness(2);
			window.draw(amount_border_shape);
			pin_live_txt = std::string(pin_count, '*');
			init_sf_text(&live_txt, pin_live_txt, 290, 150, 25, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(live_txt);
			break;
		case 3:
			init_sf_text(&L1_txt, "<--- Retragere", 85, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(L1_txt);
			init_sf_text(&R1_txt, "Depunere --->", 390, 130, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R1_txt);
			init_sf_text(&R3_txt, "Interogare Sold --->", 300, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
			break;
		case 10:
			init_sf_text(&dialog, "Sold insuficient", 210, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			init_sf_text(&R3_txt, "Modificati suma --->", 300, 225, 20, sf::Color::White, sf::Color::White, sf::Text::Bold);
			window.draw(R3_txt);
			break;
		case 13:
			init_sf_text(&dialog, "Plasati numerarul in bancomat", 120, 50, 22, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			break;
		case 21:
			init_sf_text(&dialog, "Ati introdus un PIN incorect\n        OK | Cancel?", 110, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
			break;
		case 22:
			init_sf_text(&dialog, "3 incercari succesive eronate\n  Contul dvs este suspendat\n      Apasati tasta OK", 105, 50, 24, sf::Color::Green, sf::Color::Green, sf::Text::Bold);
			window.draw(dialog);
		}
	}

	void addRunningAnimation(Animation* animation)
	{
		releaseRunningAnimation();
		this->running_animation = animation;
	}

	void releaseRunningAnimation()
	{
		if (running_animation != nullptr)
		{
			delete running_animation;
			running_animation = nullptr;
		}
	}

	void event_routine(unsigned short int routine, std::function<void()> callback = {})
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

		if (actionTimer != nullptr) return;

		switch (routine)
		{
		case RoutineCode::CARD_IN:
			accountSuspendedFlag = false;
			card_snd.play();
			vibrate(VibrationDuration::MEDIUM);
			addRunningAnimation(new VerticalOffsetAnimation(
					card_animation_time, &card_sprite,
					card_sprite_position, VerticalOffsetAnimationType::ORIGIN_TO_TOP));
			handle_timed_action(card_snd_buf.getDuration(), [this, callback]() -> void {
				oss << get_time_cli() << "The cardholder inserted a VISA Classic Card"; log_msg(oss.str());
				releaseRunningAnimation();
				card_visible = false;
				card_sprite.setPosition(card_sprite_position);
				vibrate(VibrationDuration::SHORT);
				if (callback) callback();
			});
			break;
		case RoutineCode::CARD_OUT:
			card_snd.play();
			vibrate(VibrationDuration::MEDIUM);
			card_visible = true;
			addRunningAnimation(new VerticalOffsetAnimation(
					card_animation_time, &card_sprite,
					card_sprite_position, VerticalOffsetAnimationType::TOP_TO_ORIGIN));
			handle_timed_action(card_snd_buf.getDuration(), [this, callback]() -> void {
				oss << get_time_cli() << "The card was ejected"; log_msg(oss.str());
				releaseRunningAnimation();
				card_sprite.setPosition(card_sprite_position);
				vibrate(VibrationDuration::SHORT);
				if (callback) callback();
				sign_out();
			});
			break;
		case RoutineCode::KEY_SOUND:
			key_snd.play();
			vibrate(VibrationDuration::SHORT);
			break;
		case RoutineCode::MENU_SOUND:
			menu_snd.play();
			vibrate(VibrationDuration::SHORT);
			break;
		case RoutineCode::CASH_LARGE_OUT:
			cash_snd.play();
			vibrate(VibrationDuration::MEDIUM);
			cash_large_visible = true;
			addRunningAnimation(new VerticalOffsetAnimation(
					cash_snd_buf.getDuration(), &cash_large_sprite,
					cash_large_sprite_position, VerticalOffsetAnimationType::TOP_TO_ORIGIN));
			handle_timed_action(cash_snd_buf.getDuration(), [this, callback]() -> void {
				releaseRunningAnimation();
				vibrate(VibrationDuration::SHORT);
				if (callback) callback();
			});
			break;
		case RoutineCode::CASH_SMALL_IN:
			cash_snd.play();
			vibrate(VibrationDuration::MEDIUM);
			addRunningAnimation(new VerticalOffsetAnimation(
					cash_snd_buf.getDuration(), &cash_small_sprite,
					cash_small_sprite_position, VerticalOffsetAnimationType::ORIGIN_TO_TOP));
			handle_timed_action(cash_snd_buf.getDuration(), [this, callback]() -> void {
				releaseRunningAnimation();
				cash_small_visible = false;
				cash_small_sprite.setPosition(cash_small_sprite_position);
				vibrate(VibrationDuration::SHORT);
				if (callback) callback();
			});
			break;
		case RoutineCode::RECEIPT_OUT:
			vibrate(VibrationDuration::MEDIUM);
			print_receipt_snd.play();
			receipt_visible = true;
			addRunningAnimation(new VerticalOffsetAnimation(
					print_receipt_snd_buf.getDuration(), &receipt_sprite,
					receipt_sprite_position, VerticalOffsetAnimationType::TOP_TO_ORIGIN));
			handle_timed_action(print_receipt_snd_buf.getDuration(), [this, callback]() -> void {
				releaseRunningAnimation();
				vibrate(VibrationDuration::SHORT);
				if (callback) callback();
			});
			break;
		}
	}

	void load_clients()
	{
		User u;
		int nr, i;
		database >> nr;
		for (i = 0; i < nr; i++)
		{
			database >> u.iban >> u.last_name >> u.first_name >> u.pin >> u.balance;
			users.push_back(u);
		}
	}

	User* find_user_by_pin(unsigned short pin)
	{
		for (int i = 0; i < users.size(); i++)
		{
			if (users[i].pin == pin)
				return &users[i];
		}
		return nullptr;
	}

	void sign_out()
	{
		user = nullptr;
		username_scr_str.str("");
		iban_scr_str.str("");
		init_states();
	}

	void sign_in(User* user)
	{
		this->user = user;
		username_scr_str << user->last_name << " " << user->first_name;
		iban_scr_str << user->iban;
	}

	void load_placeholder_client()
	{
		User u;
		u.iban = "RO-13-ABBK-0345-2342-0255-92";
		u.last_name = "Placeholder";
		u.first_name = "Client";
		u.pin = 0;
		u.balance = 100;
		users.push_back(u);
	}

	std::string program_title()
	{
		std::ostringstream stream;
		stream << title << " | v" << ver;
		return stream.str();
	}

    std::string serializeTimePoint(const std::chrono::system_clock::time_point& time, const std::string& format)
    {
        std::time_t tt = std::chrono::system_clock::to_time_t(time);
//		std::tm tm = *std::gmtime(&tt); //GMT (UTC)
        std::tm tm = *std::localtime(&tt); //Locale time-zone
        std::stringstream ss;
        ss << std::put_time( &tm, format.c_str() );
        return ss.str();
    }

	std::string get_time_cli()
	{
        std::chrono::time_point<std::chrono::system_clock> current_time =
                std::chrono::system_clock::now();
        return serializeTimePoint(current_time, "%Y-%m-%d | %H:%M:%S --> ");
	}

	std::string get_time_gui()
	{
		std::chrono::time_point<std::chrono::system_clock> current_time =
		        std::chrono::system_clock::now();
		return serializeTimePoint(current_time, "%H:%M:%S");
	}

	std::string get_name_log()
	{
        std::chrono::time_point<std::chrono::system_clock> current_time =
                std::chrono::system_clock::now();
        return "log-" + serializeTimePoint(current_time, "%Y.%m.%d-%H.%M.%S") + ".txt";
    }

	void log_msg(std::string str)
	{
		std::cout << str << std::endl;
		log << str << std::endl;
		oss.str("");
		oss.clear();
	}

	void terminate()
	{
		oss << get_time_cli() << "The ATM is now powered off"; log_msg(oss.str());
		if (log.is_open())
			log.close();
#ifdef TARGET_ANDROID
		android_glue.release();
#endif
		system("pause");
	}

	void init_sf_text(sf::Text *pText, const std::string msg, float pos_x, float pos_y, unsigned int char_size, const sf::Color color_fill, const sf::Color color_outline, const sf::Uint32 style) 
	{
		pText->setPosition(pos_x, pos_y);
		pText->setString(msg);
		pText->setCharacterSize(char_size);
		pText->setFillColor(color_fill);
		pText->setOutlineColor(color_outline);
		pText->setStyle(style);
	}

	void set_text_color(sf::Text *pText, const sf::Color color)
	{
		pText->setOutlineColor(color);
		pText->setFillColor(color);
	}

	inline std::string res(std::string general_path)
    {
	    return get_res_file_path(general_path);
    }

	inline std::string get_res_file_path(std::string general_path)
    {
#ifdef TARGET_ANDROID
	    return general_path;
#endif
	    return "res/" + general_path;
    }

    void handle_timed_action(sf::Time duration, std::function<void()> action)
	{
		if (actionTimer == nullptr)
		{
			actionTimer = new ActionTimer(duration, [this, action]() -> void {
				action();
				actionTimer = nullptr;
			});
		}
	}

	void vibrate(VibrationDuration vibration_duration)
	{
#ifdef TARGET_ANDROID
		android_glue.vibrate(vibration_duration);
#endif
	}

public:
	void run()
	{
		init();
		while (window.isOpen())
		{
			sf::Time delta_time = frame_delta_clock.restart();
			handle_events();
			handle_action_timer();
			if (windowHasFocus)
			{
				update(delta_time);
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
