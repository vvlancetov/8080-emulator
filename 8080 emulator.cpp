/*
Emulator of i8080 processor.


Keys for command string:
8080_emulator.exe -f BusiCom.txt -ru -list -step -log
 -f <filename>   - txt file with program
 -ru             - russian localization
 -list           - list program before run
 -step           - step by step execution (<Space> to run next command, press <P> to disable/enable it, <TAB> to list all registers)
 -log            - log commands to console


Program file format : <command in decimal> # <comment>
Example:
>1  64 # jump to start
>2  0
>3  209 # ld 1

*/

//#define DEBUG

//#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <conio.h>
#include <bitset>
#include <cmath>

template< typename T >
std::string int_to_hex(T i)
{
	std::stringstream stream;
	stream << ""
		<< std::setfill('0') << std::setw(2)
		<< std::hex << i;
	return stream.str();
}
using namespace std;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
__int8 global_error = 0; //глобальный код ошибки
bool RUSLAT_LED = false; //светодиод V4
bool restart = false; //нажатие рестарта

string path = ""; //текущий каталог
//текстура шрифта
sf::Texture font_texture;
sf::Sprite font_sprite(font_texture);

//таймеры
sf::Clock myclock;
sf::Clock video_clock;

//счетчики
unsigned int op_counter = 0;

class Mem_Ctrl // контроллер памяти
{
private:
	unsigned __int8 pc_model = 0; // модель ПК: 0 - Радио-86РК, 1 - Корвет 8020
	unsigned __int8 mem_array[384 * 1024] = { 0 };

public:
	Mem_Ctrl(unsigned __int8 model)
	{
		pc_model = model;

		if (pc_model == 0) // если эмулируем 86РК
		{
			cout << "memory init 86PK" << endl;
		}
	};

	void write(unsigned __int16 address, unsigned __int8 data); //запись значений в ячейки
	unsigned __int8 read(unsigned __int16 address); //чтение данных из памяти
};

Mem_Ctrl memory(0); //создаем контроллер памяти

class HDD_Ctrl // контроллер виртуального диска
{
private:
	unsigned __int8 data_array[64 * 1024] = { 0 };
	unsigned __int16 byte_pointer = 0;		//указатель на считываемый байт

public:
	HDD_Ctrl()
	{
		cout << "HDD created ";
		cout << size(data_array) << endl;
	};

	void set_addr_low(unsigned __int8 data);
	void set_addr_high(unsigned __int8 data);
	void write_byte(unsigned __int8 data); //запись значений на диск по байтам при старте эмулятора
	unsigned __int8 read_byte();//чтение байта с виртуального диска (ПЗУ на D14)
};

HDD_Ctrl HDD; //создаем контроллер виртуального HDD

struct comment
{
	int address;
	string text;
};

class Video_device
{
private:
	sf::RenderWindow main_window;
	int my_display_H;
	int my_display_W;
	__int16 GAME_WINDOW_X_RES;
	__int16 GAME_WINDOW_Y_RES;
	sf::Font font;
	int counter = 0;
	sf::Clock cursor_clock;				//таймер мигания курсора
	bool cursor_flipflop = false;		//переменная для мигания
	int speed_history[11] = { 50000 };
	unsigned __int8 command_reg = 0;	//регистр команд
	unsigned __int8 count_param = 0;	//количество параметров для обработки
	unsigned __int8 status = 0;         //регистр статуса
										// 0 - FO, FIFO Overrun - переполнение буфера FIFO
										// 1 - DU, DMA Underrun - потеря данных во время процесса ПДП
										// 2 - VE, Video Enable - видеооперации с экраном разрешены
										// 3 - IC, Improper Command - ошибочное количество параметров
										// 4 - LP - если на входе светового пера присутствует активный уровень и загружен регистр светового пера
										// 5 - IR, Interrupt Request - устанавливается в начале последней строки на экране если установлен флаг разрешения прерывания
										// 6 - IE, Interrupt Enable - устанавливается/сбрасывается после командами
										// 7 - всегда 0
	bool video_enable = true;			//разрешение работы (отключить по умолчанию)
	bool int_enable = false;			//разрешение прерываний
	bool int_request = false;			//устанавливается каждый раз после отображения экрана
	bool improper_command = false;		//ошибка в параметрах
	unsigned __int8 cursor_x = 1;		//позиция курсора
	unsigned __int8 cursor_y = 1;
	unsigned __int8 display_lines = 16;  //кол-во строк на экране
	unsigned __int8 display_columns = 64;//кол-во столбцов на экране
	unsigned __int8 under_line_pos = 9;	 //позиция линии подчеркивания (по высоте)
	unsigned __int8 line_height = 10;	 //высота строки в пикселях
	unsigned __int8 cursor_format = 1;	 //формат курсора: 0 - мигающий блок, 1 - мигающий штрих, 2 - инверсный блок, 3 - немигающий штрих
	bool transp_attr = true;			 //невидимый атрибут поля (при установке специальных атрибутов) 0 - невидимый, 1 - обычный (с разрывами)

public:
	Video_device();							// конструктор класса
	void sync(int elapsed_ms);				//импульс синхронизации
	void set_command(unsigned __int8 data); //команда контроллеру
	unsigned __int8 get_status();			//запрос регистра статуса
	void set_param(unsigned __int8 data);	//параметры команды
	unsigned __int8 get_params();			//чтение параметров
	string comm1 = "";
	string comm2 = "";
};

//создаем монитор
Video_device monitor;

class KBD //класс клавиатуры
{
private:

	bool key_pressed = false;     // индикатор нажатия клавиши
	unsigned __int8 input_data = 0;        // входной байт с порта А

public:
	KBD()  // конструктор класса
	{

	};

	void port_A_input(__int8 data);      //порт A для клавиатуры (8x8 keys)
	int get_key_C();                    //порт С для клавиатуры (3 key)
	int get_key_B();                     //порт B для клавиатуры (8x8 keys)
};

// создаем клавиатуру
KBD keyboard;

class MyAudioStream : public sf::SoundStream
{
	bool onGetData(Chunk& data) override;
	void onSeek(sf::Time timeOffset) override;
	

public:
	MyAudioStream()
	{
		initialize(2, 8000, { sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight });
		setLooping(0);
	}
	
	std::int16_t* s_buffer;			//ссылка буфер для звука
	bool buffer_ready;				//
	int sample_size;				//
};

class SoundMaker //класс звуковой карты
{
private:

	int signal_on = -1;					//наличие сигнала на входе
	unsigned int waves[14] = { 0 };     //подсчет импульсов
	int pointer = 0;					//указатель массива
	unsigned int silense_dur = 0;		//счетчик тишины
	bool empty = true;					//буфер пуст (для включения очистки буфера)
	bool sample_complete = false;		//если семпл подготовлен
	int sample_size = 160;				//длина звукового сэмпла
	int16_t sound_sample[160];			//массив для сэмпла
	MyAudioStream audio_stream;
	//sf::SoundBuffer s_buffer;			//буфер для звука
	sf::Clock sound_timer;

public:

	SoundMaker()  // конструктор класса
	{
		//sound_sample.resize(sample_size);
		audio_stream.buffer_ready = false;
		audio_stream.sample_size = sample_size;
		audio_stream.s_buffer = sound_sample;
		sound_timer.start();
	};

	void sync();		//счетчик тактов
	void beep_on();     //сигнал ВКЛ
	void beep_off();    //сигнал ВЫКЛ
	int get_frequancy();  //рассчет частоты звука
};

// создаем динамик
SoundMaker speaker;

vector<comment> comments; //комментарии к программе
//string filename_ROM = "test86rk.txt"; //программа проверки из журнала
//string filename_ROM = "test.txt"; //отладка команд
//string filename_ROM = "memtest32.txt"; //типовой тест памяти
string filename_ROM = "86RK32.txt"; //типовая прошивка
//string filename_ROM = "bios16.txt"; //прошивка с сайта rk86.ru

// HDD
//string filename_HDD = "1_CHERV.txt";  //  0000 - норм
//string filename_HDD = "TETRIS4.txt";  //  3000 - норм
//string filename_HDD = "klad.txt";     //  0000 - норм
//string filename_HDD = "glass1.txt";   //  0000 - норм
string filename_HDD = "diverse.txt";	//  0000 - норм
//string filename_HDD = "vmemtest.txt"; //  0000 - тест видеопамяти
//string filename_HDD = "formula.txt";  //  0000 - гонки
//string filename_HDD = "sirius.txt";     //  0000  - не видно врагов, что-то с атрибутами
//string filename_HDD = "xonix.txt";    //  0000 - норм
//string filename_HDD = "test.txt";
//string filename_HDD = "pack.txt";     //  1800 вылетает
//string filename_HDD = "pacman.txt";   //  0000 - норм
//string filename_HDD = "music.txt";    //  0000
//string filename_HDD = "rk86_basic.txt"; //0000 - глючит

unsigned __int16 program_counter = 0xf800; //первая команда при старте ПК
int first_address_ROM = 0xF800;			   // адрес загрузки ROM (прошивки)
#ifdef DEBUG
vector<int> breakpoints;                   //точки останова
string tmp_s;
#endif


//количество памяти в ПК
int RAM_amount = 32;

//регистры процессора

bool Flag_Zero = false; //Flags
bool Flag_Sign = false;
bool Flag_Parity = false;
bool Flag_Carry = false;
bool Flag_A_Carry = false;
bool Interrupts_enabled = true;//разрешение прерываний

unsigned __int16 registers[8] = { 0 };  //внутренние регистры, включаяя аккумулятор
										// 111(7) - A
										// 000(0) - B
										// 001(1) - C
										// 010(2) - D
										// 011(3) - E
										// 100(4) - H
										// 101(5) - L
										// 110(6) - M память

string regnames[8] = { "B","C","D","E","H","L","-","A" };
string pairnames[4] = { "BC","DE","HL","SP" };

//временные регистры
unsigned __int16 temp_ACC_16 = 0;
unsigned __int8 temp_ACC_8 = 0;
unsigned __int16 temp_Addr = 0;

unsigned __int16 debug[4]; //накопитель команд для отладки УДАЛИТЬ
unsigned __int16 stack_pointer = 0; //указатель стека

// флаги для изменения работы эмулятора
bool step_mode = false; //ждать ли нажатия пробела для выполнения команд
bool go_forward; //переменная для выхода из цикла обработки нажатий
bool RU_lang = true; //локализация
bool list_at_start = false; //вывод листинга на старте
bool log_to_console = false; //логирование команд на консоль
bool short_print = false; //сокращенный набор регистров для вывода

void print_all();
string get_sym(int code);

//void disassemble(unsigned __int16 start, unsigned __int16 end);

int main(int argc, char* argv[]) {
#ifdef DEBUG
	//breakpoints.push_back(0x0);
	//breakpoints.push_back(0x0161);
	//breakpoints.push_back(0x0160);
	//breakpoints.push_back(0xFCBA);
	//breakpoints.push_back(0xA29);   // 1339  0132 0168 0A29
#endif
	//путь к текущему каталогу
	path = argv[0];
	int l_symb = (int)path.find_last_of('\\');
	path.resize(++l_symb);

	if (font_texture.loadFromFile(path + "videorom_trans2.png")) cout << "Font ROM loaded" << endl;
	font_sprite.setTexture(font_texture);
	font_texture.setSmooth(0);
	//setlocale(LC_ALL, "Russian");

	//проверяем аргументы командной строки
	cout << "Checking command string parameters..." << endl;

	for (int i = 1; i < argc; i++)
	{
		string s = argv[i];
		if (s.substr(0, 2) == "-f")
		{
			//найдено имя файла
			if (argc >= i + 2)
			{
				filename_ROM = argv[i + 1];// "Prog.txt";
				cout << "new filename = " << filename_ROM << endl;
			}
			else
			{
				filename_ROM = "Prog.txt";
				cout << "filename = " << filename_ROM << " (default)" << endl;
			}
		}
		if (s.substr(0, 3) == "-ru")
		{
			//использование русского языка
			RU_lang = true;
			cout << "set RU lang" << endl;
		}
		if (s.substr(0, 5) == "-list")
		{
			//листинг в начале
			list_at_start = true;
			cout << "set listing after loadin ON " << endl;
		}
		if (s.substr(0, 5) == "-step")
		{
			//пошаговое выполнение
			step_mode = true;
			cout << "set step mode ON" << endl;
		}
		if (s.substr(0, 4) == "-log")
		{
			//пошаговое выполнение
			log_to_console = true;
			cout << "set log to console ON" << endl;
		}
	}

	if (RU_lang) setlocale(LC_ALL, "Russian");

	if (!log_to_console)
	{
		step_mode = false; //отключаем пошаговый режим, если не задан вывод логов на экран
		cout << "set step mode ";
		SetConsoleTextAttribute(hConsole, 12);
		cout << "OFF";
		SetConsoleTextAttribute(hConsole, 7);
		cout << " because log to console is OFF" << endl;
	}

	ifstream file(filename_ROM);
	if (!file.is_open()) {
		if (RU_lang)
		{
			cout << "Файл ROM";
			SetConsoleTextAttribute(hConsole, 4);
			cout << filename_ROM;
			SetConsoleTextAttribute(hConsole, 7);
			cout << " не найден!" << endl;
		}
		else
		{
			cout << "File " << filename_ROM << " not found!" << endl;
		}
		return 1;
	}

	string line;
	int number;
	string text;
	int line_count = 0;

	//считываем данные из файла
	int line_number = first_address_ROM;
	while (getline(file, line)) {
		stringstream ss(line);
		if (ss >> number) { // Проверяем, есть ли число в начале строки
			memory.write(line_number, number); //пишем в память
			line_number++;
			line_count++;
		}
		text = "";
		ss.ignore(1, ' '); // Пропускаем символ ' '
		ss.ignore(1, '#'); // Пропускаем символ '#'
		ss.ignore(1, ' '); // Пропускаем символ ' '
		getline(ss, text); // Считываем текст после '#'
		if (!text.empty()) comments.push_back({ line_number - 1, text }); // Добавляем комментарий в массив комментариев
	}
	file.close();

	//файл загружен
	if (RU_lang)
	{
		cout << "Загружено " << (int)line_count << " команд(ы) из файла" << endl;
	}
	else
	{
		cout << "Loaded " << (int)line_count << " commands from file" << endl;
	}

	//открываем файл HDD
	file.open(filename_HDD);
	if (!file.is_open()) {
		if (RU_lang)
		{
			cout << "Файл HDD";
			SetConsoleTextAttribute(hConsole, 4);
			cout << filename_HDD;
			SetConsoleTextAttribute(hConsole, 7);
			cout << " не найден!" << endl;
		}
		else
		{
			cout << "File " << filename_HDD << " not found!" << endl;
		}
		//return 1;
	}
	else
	{
		line_count = 0;
		//считываем данные из файла HDD
		while (getline(file, line)) {
			stringstream ss(line);
			if (ss >> number) { // Проверяем, есть ли число в начале строки
				HDD.write_byte(number); //пишем в память
				memory.write(line_count, number);//пишем сразу в RAm
					
				line_count++;
			}
		}
		file.close();
		cout << "Записано " << hex << (int)line_count << "(H) байт в виртуальный HDD" << endl;
	}

	if (RU_lang)
	{
		cout << endl;
		cout << "Для отключения/включения пошагового режима нажмите <F9>" << endl;
		cout << "Для выполнения следующей команды нажимайте <F8>" << endl;
		cout << "Для вывода на экран содержимого регистров и памяти нажмите <F12>" << endl;
		cout << "Для отключения/включения вывода команд на экран нажмите <F10>" << endl;
		cout << "Команда процессора РЕСТАРТ <F7>" << endl;

		SetConsoleTextAttribute(hConsole, 10);
		cout << "Начинаем выполнение... нажмите любую клавишу" << endl << endl;
		SetConsoleTextAttribute(hConsole, 7);
	}
	else
	{
		cout << endl;
		cout << "To turn ON/OFF step mode press <F9>" << endl;
		cout << "Press <F8> for next command" << endl;
		cout << "To display the contents of registers and memory, press <F12>" << endl;
		cout << "To turn ON/OFF printing commands press <F10>" << endl;
		cout << "Processor command RESTART <F7>" << endl;

		cout << endl << "Starting programm... ";
		SetConsoleTextAttribute(hConsole, 10);
		cout << "press a key" << endl << endl;
		SetConsoleTextAttribute(hConsole, 7);
	}

	//ждем нажатия клавиши
	while (!_kbhit()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

	//запускаем таймеры
	myclock.restart();
	video_clock.start();

	//предотвращение дребезга клавиш управления
	bool keys_up = true;

	cout << "Running..." << hex << endl;
	//основной цикл программы
	while (1)
	{
		//счетчик операций
		op_counter++;

		//перехват системных вызовов

		if (program_counter == 0xFCBA) void syscallF809();
#ifdef DEBUG
		//переход в пошаговый режим при попадании в точку останова
		for (int b = 0; b < breakpoints.capacity(); b++)
		{
			if (program_counter == breakpoints.at(b))   //breakpoints.at(b)
			{
				step_mode = true;
				cout << "Breakpoint at " << (int)program_counter << endl;
				log_to_console = true;
			}
		}

		//отображение ячеек на экран
		//monitor.comm1 = int_to_hex((int)memory.read(0x2b + 1)) + "   " + int_to_hex((int)memory.read(0x2b)) + "  " + int_to_hex((int)memory.read(0x38)) + "  " + int_to_hex((int)memory.read(0x37));
		//monitor.comm2 = to_string(memory.read(0x37)) + " " + to_string(memory.read(0x37 + 1));
#endif

		go_forward = false;
		if (!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) &&
			!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) &&
			!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F8) &&
			!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) &&
			!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F7) &&
			!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F6)) keys_up = true;

		//вызываем видеоадаптер по таймеру
		
		if(video_clock.getElapsedTime().asMicroseconds() > 20000)
		{
			video_clock.stop();
			monitor.sync(video_clock.getElapsedTime().asMicroseconds()); //синхроимпульс для монитора
			video_clock.restart();
			op_counter = 0;
		}

		//синхронизация звука
		speaker.sync();

		//мониторинг нажатия клавиш в обычном режиме

		//проверяем нажатие кнопки P
		//if (pressed_key == 112 || pressed_key == 167 || pressed_key == 80 || pressed_key == 135) step_mode = !step_mode;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) && keys_up) { step_mode = !step_mode; keys_up = false; }

		//проверяем нажатие кнопки C
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) && keys_up) { log_to_console = !log_to_console; keys_up = false; }
		//if (pressed_key == 99 || pressed_key == 67 || pressed_key == 225 || pressed_key == 145) log_to_console = !log_to_console;

		//выводим содержимое регистров если эмулятор работает в обычном режиме
		if (!step_mode && !log_to_console && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) && keys_up) { print_all();  keys_up = false; }

		//restart
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F7) && keys_up) restart = true;

		//смена видеоережима для разных систем 32/64 КБ
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F6) && keys_up) {
			if (RAM_amount == 16) RAM_amount = 32;
			else RAM_amount = 16;
			keys_up = false;
		}

		//задержка вывода по нажатию кнопки в пошаговом режиме
		while (!go_forward && step_mode)
		{

			if (!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F6) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F8) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F7) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space)) keys_up = true;


			myclock.stop();
			//засыпаем, чтобы не загружать процессор
			//while (!_kbhit()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) && keys_up) { step_mode = !step_mode; keys_up = false; myclock.start(); }
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) && keys_up) { print_all(); keys_up = false; }
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F8) && keys_up) { go_forward = true; }
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F6) && keys_up) {
				if (RAM_amount == 16) RAM_amount = 32;
				else RAM_amount = 16;
				keys_up = false;
			}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) && keys_up) { log_to_console = !log_to_console; keys_up = false; }
			//if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space) && keys_up) { go_forward = true;}
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F7)) restart = true;

			if (!step_mode) {
				go_forward = true;
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				break;
			}
			else monitor.sync(0); //синхроимпульс для монитора
		};

		//основной цикл 
#ifdef DEBUG
		//выводим текущую команду
		if (log_to_console)   //    ||program_counter < 0xf800
		{
			//ищем комментарий к команде и печатаем
			for (auto comm : comments)
			{
				if (comm.address == program_counter)  // && program_counter != 0xFE63 && program_counter != 0xFE01 && program_counter != 0xFE72 && program_counter != 0xFD38
				{
					SetConsoleTextAttribute(hConsole, 6);
					cout << "REM " << comm.text;
					if (program_counter == 0xFCBA) cout << " код #" << registers[1] << "[" << get_sym(registers[1]) << "] pos(" + to_string((memory.read(0x7602) - 8)) + " " + to_string((memory.read(0x7603) - 3)) + ")";
					SetConsoleTextAttribute(hConsole, 7);
					cout << endl;
				}
			}
		}
		if (log_to_console) {
			string fl = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
			cout << fl << "\t" << hex << program_counter << "\t" << (int)memory.read(program_counter) << "\t";
		}
#endif
		//декодер комманд

		// NOP
		if (memory.read(program_counter) == 0)
		{
			program_counter++;
#ifdef DEBUG
			if (log_to_console) cout << "NOP" << endl;
#endif
			continue;
		}

		//Restart RST

		if ((memory.read(program_counter) & 199) == 199 || (restart && keys_up))
		{
			int rstN = (memory.read(program_counter) >> 3) & 7;
			if (restart && keys_up) {
#ifdef DEBUG
				cout << endl << program_counter  << "\t" << memory.read(program_counter + 1) << "\t" << "Restart from F800" << endl;
#endif
				restart = false;
				program_counter = 0xF800;
				keys_up = false;
				continue;
			}

			memory.write(stack_pointer - 1, (program_counter + 1) >> 8);
			memory.write(stack_pointer - 2, (program_counter + 1) & 255);
			stack_pointer -= 2;
			program_counter = rstN << 3;
#ifdef DEBUG
			if (log_to_console) cout << "\t\tRST" << dec << (int)rstN << hex << " jump to " << (int)program_counter << endl;
#endif
			continue;
		}
		// ===== jump===============
		//Conditional jump
		if ((memory.read(program_counter) & 199) == 194)
		{
#ifdef DEBUG
			string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
			__int8 cond = (memory.read(program_counter) >> 3) & 7;
#ifdef DEBUG
			if (cond == 0) flags = "[NOT Zero]" + flags;
			if (cond == 1) flags = "[Zero]" + flags;
			if (cond == 2) flags = "[No Carry]" + flags;
			if (cond == 3) flags = "[Carry]" + flags;
			if (cond == 4) flags = "[Not Parity]" + flags;
			if (cond == 5) flags = "[Parity]" + flags;
			if (cond == 6) flags = "[Plus]" + flags;
			if (cond == 7) flags = "[Minus]" + flags;
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
			if (cond == 0 && !Flag_Zero)  //jump if NOT zero
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 1 && Flag_Zero)  //jump if Zero
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 2 && !Flag_Carry)  //jump if CY = 0
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 3 && Flag_Carry)  //jump if CY = 1
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 4 && !Flag_Parity)  //jump if Not Parity (odd)
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG				
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 5 && Flag_Parity)  //jump if Parity (even)
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 6 && !Flag_Sign)  //jump if +
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
			if (cond == 7 && Flag_Sign)  //jump if -
			{
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
				if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
				continue;
			}
#ifdef DEBUG
			if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
			program_counter += 3;
			continue;
		}
		//==== Data Transfer Group ===========
	    // Move Register/Memory
		if ((memory.read(program_counter) >> 6) == 1)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (Dest != 6 && Src != 6)
			{
				//копирование между регистрами
				registers[Dest] = registers[Src];
#ifdef DEBUG
				if (log_to_console) cout << "\t\tMove " << regnames[Src] << " -> " << regnames[Dest] << "[" << registers[Dest] << "]" << endl;
#endif
				program_counter++;
				continue;
			}
			else
			{
				if (Src == 6 && Dest != 6) //источник - память по адресу HL
				{
					temp_Addr = registers[4] * 256 + registers[5]; //адрес ячейки в HL
					registers[Dest] = memory.read(temp_Addr);
#ifdef DEBUG
					if (log_to_console) {
						cout << "\t\t\Load " << regnames[Dest] << "(" << registers[Dest] << ") from address [";
						SetConsoleTextAttribute(hConsole, 10);
						cout << temp_Addr; SetConsoleTextAttribute(hConsole, 7);
						cout << "]" << endl;
					}
#endif
					program_counter++;
					continue;
				}
				else
				{
					if (Dest == 6 && Src != 6) //адресат - память по адресу HL
					{
						temp_Addr = registers[4] * 256 + registers[5]; //адрес ячейки в HL
						memory.write(temp_Addr, registers[Src]);
#ifdef DEBUG
						if (log_to_console) {
							cout << "\t\t\Copy " << regnames[Src] << "(" << registers[Src] << ") to address [";
							SetConsoleTextAttribute(hConsole, 10);
							cout << temp_Addr;
							SetConsoleTextAttribute(hConsole, 7);
							cout << "]" << endl;
						}
#endif
						program_counter++;
						continue;
					}
				}
			}
		}
		// Move immediate
		if ((memory.read(program_counter) & 199) == 6)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\t";
			if (Dest != 6)
			{
				//загружаем непосредственные  данные из памяти в регистр
				registers[Dest] = memory.read(program_counter + 1);
#ifdef DEBUG
				if (log_to_console) cout << "Load immediate [" << (int)memory.read(program_counter + 1) << "] to " << regnames[Dest] << "(" << registers[Dest] << ")" << endl;
#endif
				program_counter += 2;
				continue;
			}
			else
			{
				//загружаем непосредственные данные из памяти в адрес из [HL]
				int addr = registers[4] * 256 + registers[5];
				memory.write(addr, memory.read(program_counter + 1));
#ifdef DEBUG
				if (log_to_console) {
					cout << "Load immediate [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)memory.read(program_counter + 1);
					SetConsoleTextAttribute(hConsole, 7);
					cout << "] to address " << (__int16)addr << endl;
				}
#endif
				program_counter += 2;
				continue;
			}
		}
		// Load register pair immediate
		if ((memory.read(program_counter) & 207) == 1)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 4) & 3;

			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";

			if (Dest == 0)
			{
				//загружаем непосредственные данные в ВС
				registers[0] = memory.read(program_counter + 2);
				registers[1] = memory.read(program_counter + 1);
#ifdef DEBUG
				if (log_to_console) cout << "Load immediate [" << registers[1] + registers[0] * 256 << "] to " << pairnames[0] << endl;
#endif
				program_counter += 3;
				continue;
			}
			if (Dest == 1)
			{
				//загружаем непосредственные данные в ВС
				registers[2] = memory.read(program_counter + 2);
				registers[3] = memory.read(program_counter + 1);
#ifdef DEBUG
				if (log_to_console) cout << "Load immediate [" << registers[3] + registers[2] * 256 << "] to " << pairnames[1] << endl;
#endif
				program_counter += 3;
				continue;
			}
			if (Dest == 2)
			{
				//загружаем непосредственные данные в HL
				registers[4] = memory.read(program_counter + 2);
				registers[5] = memory.read(program_counter + 1);
#ifdef DEBUG
				if (log_to_console) cout << "Load immediate [" << registers[5] + registers[4] * 256 << "] to " << pairnames[2] << endl;
#endif
				program_counter += 3;
				continue;
			}
			if (Dest == 3)
			{
				//загружаем непосредственные данные в SP
				stack_pointer = memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1);
#ifdef DEBUG
				if (log_to_console) cout << "Load immediate [" << memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1) << "] to SP" << endl;
#endif
				program_counter += 3;
				continue;
			}
		}
		//Load ACC direct (LDA)
		if (memory.read(program_counter) == 58)
		{
			unsigned __int16 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			registers[7] = memory.read(addr);
#ifdef DEBUG
			if (log_to_console) {
				cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\tLoad ACC(" << registers[7] << ") from address ";
				SetConsoleTextAttribute(hConsole, 10);
				cout << (int)addr; SetConsoleTextAttribute(hConsole, 7);  cout << endl;
			}
#endif
			program_counter += 3;
			continue;
		}
		//Store ACC direct (STA)
		if (memory.read(program_counter) == 50)
		{
			unsigned __int16 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			memory.write(addr, registers[7]);
#ifdef DEBUG
			if (log_to_console) {
				cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\tStore ACC(" << (int)registers[7] << ") to address ";
				SetConsoleTextAttribute(hConsole, 10);
				cout << (int)addr; SetConsoleTextAttribute(hConsole, 7);  cout << endl;
			}
#endif
			program_counter += 3;
			continue;
		}
		//Load HL direct (LHDL)
		if (memory.read(program_counter) == 42)
		{
			unsigned __int16 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			registers[5] = memory.read(addr);
			registers[4] = memory.read(addr + 1);
#ifdef DEBUG
			if (log_to_console) {
				cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\tLoad HL(" << (int)(registers[5] + registers[4] * 256) << ") from address ";
				SetConsoleTextAttribute(hConsole, 10);
				cout << (int)addr; SetConsoleTextAttribute(hConsole, 7);  cout << endl;
			}
#endif
			program_counter += 3;
			continue;
		}
		//Store HL direct (SHLD)
		if (memory.read(program_counter) == 34)
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
			unsigned __int16 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			memory.write(addr, registers[5]);
			memory.write(addr + 1, registers[4]);
#ifdef DEBUG
			if (log_to_console) {
				cout << "Save HL(" << (int)(registers[5] + registers[4] * 256) << ") to address ";
				SetConsoleTextAttribute(hConsole, 10);
				cout << (int)addr; SetConsoleTextAttribute(hConsole, 7);  cout << endl;
			}
#endif
			program_counter += 3;
			continue;
		}
		//Load ACC indirect (LDAX)
		if ((memory.read(program_counter) & 207) == 10)
		{
			unsigned __int8 pair = (memory.read(program_counter) >> 4) & 3;
			if (pair == 0)
			{
				//пара BC
				unsigned __int16 addr = registers[0] * 256 + registers[1];
				registers[7] = memory.read(addr);
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tLoad ACC(" << (int)registers[7] << ") from address ";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << " in BC" << endl;
				}
#endif
				program_counter++;
				continue;
			}
			if (pair == 1)
			{
				//пара DE
				unsigned __int16 addr = registers[2] * 256 + registers[3];
				registers[7] = memory.read(addr);
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tLoad ACC(" << (int)registers[7] << ") from address ";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << " in DE" << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Store ACC indirect (STAX)
		if ((memory.read(program_counter) & 207) == 2)
		{
			unsigned __int8 pair = (memory.read(program_counter) >> 4) & 3;
			if (pair == 0)
			{
				//пара BC
				unsigned __int16 addr = registers[0] * 256 + registers[1];
				memory.write(addr, registers[7]);
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tSave ACC(" << (int)registers[7] << ") to address ";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << "(BC)" << endl;
				}
#endif
				program_counter++;
				continue;
			}
			if (pair == 1)
			{
				//пара DE
				unsigned __int16 addr = registers[2] * 256 + registers[3];
				memory.write(addr, registers[7]);
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tSave ACC(" << (int)registers[7] << ") to address ";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << "(DE)" << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Exchange HL <-> DE
		if (memory.read(program_counter) == 235)
		{
			unsigned __int16 tmp_D = registers[2];
			unsigned __int16 tmp_E = registers[3];
			registers[2] = registers[4];
			registers[3] = registers[5];
			registers[4] = tmp_D;
			registers[5] = tmp_E;
#ifdef DEBUG
			if (log_to_console) cout << "\t\tExchange DE(" << (int)(registers[3] + registers[2] * 256) << ") <-> HL(" << (int)(registers[5] + registers[4] * 256) << ")" << endl;
#endif
			program_counter++;
			continue;
		}
		//================ Arithmetic group ====================
		//Add Register  ACC + R = ACC AND ACC + M[HL] = ACC
		if ((memory.read(program_counter) >> 3) == 16)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (Src != 6) // ADD R
			{
				temp_ACC_16 = registers[7] + registers[Src];
				temp_ACC_8 = (registers[7] & 15) + (registers[Src] & 15);
				Flag_Carry = temp_ACC_16 >> 8;
				Flag_A_Carry = temp_ACC_8 >> 4;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = registers[7] >> 7;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) cout << "\t\tADD A + " << regnames[Src] << "(" << registers[Src] << ") = " << registers[7] << endl;
#endif
				program_counter++;
				continue;
			}
			else //add M[HL]
			{
				temp_Addr = (registers[4] << 8) + registers[5];
				temp_ACC_16 = registers[7] + memory.read(temp_Addr);
				temp_ACC_8 = (registers[7] & 15) + (memory.read(temp_Addr) & 15);
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = registers[7] >> 7;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tADD A + M(" << (int)memory.read(temp_Addr) << ") at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << temp_Addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << "] = " << registers[7] << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Add immediate (ACC + byte2)
		if (memory.read(program_counter) == 198)
		{
			temp_ACC_16 = registers[7] + memory.read(program_counter + 1);
			temp_ACC_8 = (registers[7] & 15) + (memory.read(program_counter + 1) & 15);
			Flag_Carry = temp_ACC_16 >> 8;
			Flag_A_Carry = temp_ACC_8 >> 4;
			registers[7] = temp_ACC_16 & 255;
			if (registers[7]) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = registers[7] >> 7;
			Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\tADD A + IMM(" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
#endif
			program_counter += 2;
			continue;
		}
		//Add Register OR M with Carry    ACC + R + CY = ACC OR ACC + M[HL] + CY = ACC
		if ((memory.read(program_counter) >> 3) == 17)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (Src != 6) // ADD R
			{
				temp_ACC_16 = registers[7] + registers[Src] + Flag_Carry;
				temp_ACC_8 = (registers[7] & 15) + (registers[Src] & 15) + Flag_A_Carry;
				Flag_Carry = temp_ACC_16 >> 8;
				Flag_A_Carry = temp_ACC_8 >> 4;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = registers[7] >> 7;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) cout << "\t\tADD A + " << regnames[Src] << " + CY = " << registers[7] << endl;
#endif
				program_counter++;
				continue;
			}
			else //add M[HL]
			{
				temp_Addr = (registers[4] << 8) + registers[5];
				temp_ACC_16 = registers[7] + memory.read(temp_Addr) + Flag_Carry;
				temp_ACC_8 = (registers[7] & 15) + (memory.read(temp_Addr) & 15) + Flag_A_Carry;
				Flag_Carry = temp_ACC_16 >> 8;
				Flag_A_Carry = temp_ACC_8 >> 4;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = registers[7] >> 7;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) {
					cout << "\t\tADD A + M(" << (int)memory.read(temp_Addr) << ") at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)temp_Addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << "] + CY = " << registers[7] << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Add immediate with carry (ACC + byte + CY)
		if (memory.read(program_counter) == 206)
		{
			temp_ACC_16 = registers[7] + memory.read(program_counter + 1) + Flag_Carry;
			temp_ACC_8 = (registers[7] & 15) + (memory.read(program_counter + 1) & 15) + Flag_A_Carry;
			Flag_Carry = temp_ACC_16 >> 8;
			Flag_A_Carry = temp_ACC_8 >> 4;
			registers[7] = temp_ACC_16 & 255;
			if (registers[7]) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = registers[7] >> 7;
			Flag_Parity = ~registers[7] & 1;
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\tADD A + IMM(" << (int)memory.read(program_counter + 1) << ") + CY = " << registers[7] << endl;
			program_counter += 2;
			continue;
		}
		//==============SUB==================================
		//SUB Register OR M from ACC  (ACC-R=ACC  ACC-M[HL] = ACC)
		if ((memory.read(program_counter) >> 3) == 18)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;

			if (Src != 6) // SUB R
			{
				temp_ACC_16 = registers[7] - registers[Src];
				temp_ACC_8 = (registers[7] & 15) - (registers[Src] & 15);
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[7] >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) cout << "\t\tSUB A - " << regnames[Src] << " = " << registers[7] << endl;
#endif
				program_counter++;
				continue;
			}
			else //SUB M[HL]
			{
				temp_Addr = registers[4] * 256 + registers[5];
				temp_ACC_16 = registers[7] - memory.read(temp_Addr);
				temp_ACC_8 = (registers[7] & 15) - (memory.read(temp_Addr) & 15);
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[7] >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) {
					cout << (int)memory.read(program_counter + 1) << "\t\tSUB A - M(" << (int)memory.read(temp_Addr) << ") at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)temp_Addr;
					SetConsoleTextAttribute(hConsole, 7);
					cout << "] = " << registers[7] << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Subtract immediate
		if (memory.read(program_counter) == 214)
		{

			temp_ACC_16 = registers[7] - memory.read(program_counter + 1);
			temp_ACC_8 = (registers[7] & 15) - (memory.read(program_counter + 1) & 15);
			Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
			Flag_Carry = (temp_ACC_16 >> 8) & 1;
			registers[7] = temp_ACC_16 & 255;
			if (registers[7]) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (registers[7] >> 7) & 1;
			Flag_Parity = ~registers[7] & 1;
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\tSUB A - IMM(" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
			program_counter += 2;
			continue;
		}
		//SUB Register OR M from ACC with borrow  (ACC-R-CY=ACC  ACC-M[HL]-CY = ACC)
		if ((memory.read(program_counter) >> 3) == 19)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (Src != 6) // SUB R
			{
				temp_ACC_16 = registers[7] - registers[Src] - Flag_Carry;
				temp_ACC_8 = (registers[7] & 15) - (registers[Src] & 15) - Flag_A_Carry;
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[7] >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) cout << "\t\tSUB A - " << regnames[Src] << " - CY = " << registers[7] << endl;
#endif
				program_counter++;
				continue;
			}
			else //SUB M[HL]
			{
				temp_Addr = registers[4] * 256 + registers[5];
				temp_ACC_16 = registers[7] - memory.read(temp_Addr) - Flag_Carry;
				temp_ACC_8 = (registers[7] & 15) - (memory.read(temp_Addr) & 15) - Flag_A_Carry;
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				registers[7] = temp_ACC_16 & 255;
				if (registers[7]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[7] >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
				if (log_to_console) {
					cout << "SUB A - M(" << (int)memory.read(temp_Addr) << ") at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)temp_Addr;
					SetConsoleTextAttribute(hConsole, 7); 
					cout << "] - CY = " << registers[7] << endl;
				}
#endif
				program_counter++;
				continue;
			}
		}
		//Subtract immediate with borrow
		if (memory.read(program_counter) == 222)
		{
			temp_ACC_16 = registers[7] - memory.read(program_counter + 1) - Flag_Carry;
			temp_ACC_8 = (registers[7] & 15) - (memory.read(program_counter + 1) & 15) - Flag_A_Carry;
			Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
			Flag_Carry = (temp_ACC_16 >> 8) & 1;
			registers[7] = temp_ACC_16 & 255;
			if (registers[7]) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (registers[7] >> 7) & 1;
			Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
			if (log_to_console) cout << "\t\tSUB A - IMM(" << (int)memory.read(program_counter + 1) << ") - CY = " << registers[7] << endl;
#endif
			program_counter += 2;
			continue;
		}
		//==============INC/DEC================================
		//Increment Register OR Memory
		if ((memory.read(program_counter) & 199) == 4)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
			if (Dest != 6) // INC R
			{
				Flag_A_Carry = ((registers[Dest] & 15) + 1) >> 4;
				registers[Dest] = (registers[Dest] + 1) & 255;
				if (registers[Dest]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[Dest] >> 7) & 1;
				Flag_Parity = ~registers[Dest] & 1;
#ifdef DEBUG
				if (log_to_console) cout << "\t\tINC " << regnames[Dest] << " = " << registers[Dest] << endl;
#endif
				program_counter++;
				continue;
			}
			else // INC M[HL]
			{
				temp_Addr = registers[4] * 256 + registers[5];
				Flag_A_Carry = (memory.read(temp_Addr) & 15 + 1) >> 4;
				temp_ACC_16 = (memory.read(temp_Addr) + 1) & 255;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = ~temp_ACC_16 & 1;
				memory.write(temp_Addr, (temp_ACC_16 & 255));

				if (log_to_console) {
					cout << "\t\tINC Mem at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)temp_Addr;
					SetConsoleTextAttribute(hConsole, 7); cout << "] = " << (int)temp_ACC_16 << endl;
				}
				program_counter++;
				continue;
			}
		}
		//Decrement Register OR MEM[HL]
		if ((memory.read(program_counter) & 199) == 5)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
			if (Dest != 6) // DEC R
			{
				temp_ACC_8 = (registers[Dest] & 15) - 1;
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				temp_ACC_8 = registers[Dest]; //старое значение
				registers[Dest]--;
				registers[Dest] = registers[Dest] & 255;
				if (registers[Dest]) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (registers[Dest] >> 7) & 1;
				Flag_Parity = ~registers[Dest] & 1;
				string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
				if (log_to_console) cout << "\t\tDEC " << regnames[Dest] << "(" << (int)temp_ACC_8 << ") = " << (int)registers[Dest] << " " << flags << endl;
				program_counter++;
				continue;
			}
			else // DEC M[HL]
			{
				temp_Addr = registers[4] * 256 + registers[5];
				temp_ACC_8 = (memory.read(temp_Addr) & 15) - 1;
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				temp_ACC_8 = memory.read(temp_Addr); //старое значение
				temp_ACC_16 = temp_ACC_8 - 1;
				temp_ACC_16 = temp_ACC_16 & 255;
				memory.write(temp_Addr, temp_ACC_16);
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = ~temp_ACC_16 & 1;
				string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
				if (log_to_console) cout << "\t\tDEC Mem(" << temp_ACC_8 << ") at " << (int)temp_Addr << " = " << (int)(temp_ACC_16 & 255) << " " << flags << endl;
				program_counter++;
				continue;
			}
		}
		//Increment register pair
		if ((memory.read(program_counter) & 207) == 3)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 4) & 3;

			if (Dest == 0) //BC
			{
				//увеличиваем ВС на 1
				temp_ACC_16 = registers[0];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[1] + 1;
				registers[1] = temp_ACC_16 & 255;
				registers[0] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tINC BC = " << (int)temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 1) //DE
			{
				//увеличиваем DE на 1
				temp_ACC_16 = registers[2];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[3] + 1;
				registers[3] = temp_ACC_16 & 255;
				registers[2] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tINC DE = " << (int)temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 2) //HL
			{
				//увеличиваем HL на 1
				temp_ACC_16 = registers[4];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[5] + 1;
				registers[5] = temp_ACC_16 & 255;
				registers[4] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tINC HL = " << (int)temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 3) //SP
			{
				//увеличиваем SP на 1
				stack_pointer++;
				if (log_to_console) cout << "\t\tINC SP = " << (int)stack_pointer << endl;
				program_counter += 1;
				continue;
			}

		}
		//Decrement register pair
		if ((memory.read(program_counter) & 207) == 11)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 4) & 3;

			if (Dest == 0) //BC
			{
				//уменьшаем ВС на 1
				temp_ACC_16 = registers[0];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[1] - 1;
				registers[1] = temp_ACC_16 & 255;
				registers[0] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tDEC BC = " << temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 1) //DE
			{
				//уменьшаем DE на 1
				temp_ACC_16 = registers[2];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[3] - 1;
				registers[3] = temp_ACC_16 & 255;
				registers[2] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tDEC DE = " << temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 2) //HL
			{
				//уменьшаем HL на 1
				temp_ACC_16 = registers[4];
				temp_ACC_16 = (temp_ACC_16 << 8) + registers[5] - 1;
				registers[5] = temp_ACC_16 & 255;
				registers[4] = temp_ACC_16 >> 8;
				if (log_to_console) cout << "\t\tDEC HL = " << temp_ACC_16 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 3) //SP
			{
				//уменьшаем SP на 1
				stack_pointer--;
				if (log_to_console) cout << "\t\tDEC SP = " << stack_pointer << endl;
				program_counter += 1;
				continue;
			}

		}

		//Add register pair to H and L
		if ((memory.read(program_counter) & 207) == 9)
		{
			unsigned __int8 Dest = (memory.read(program_counter) >> 4) & 3;

			if (Dest == 0) //BC
			{
				//HL + ВС = HL 
				unsigned int new_reg = registers[0] * 256 + registers[1] + registers[4] * 256 + registers[5];
				Flag_Carry = (new_reg >> 16) & 1;
				registers[4] = (new_reg >> 8) & 255;
				registers[5] = new_reg & 255;
				if (log_to_console) cout << "\t\tHL + BC = " << registers[5] + registers[4] * 256 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 1) //DE
			{
				//HL + DE = HL 
				unsigned int new_reg = registers[2] * 256 + registers[3] + registers[4] * 256 + registers[5];
				Flag_Carry = (new_reg >> 16) & 1;
				registers[4] = (new_reg >> 8) & 255;
				registers[5] = new_reg & 255;

				if (log_to_console) cout << "\t\tHL + DE = " << registers[5] + registers[4] * 256 << endl;
				program_counter += 1;
				continue;
			}
			if (Dest == 2) //HL
			{
				//HL + HL = HL 
				unsigned int new_reg = registers[4] * 256 + registers[5] + registers[4] * 256 + registers[5];
				Flag_Carry = (new_reg >> 16) & 1;
				registers[4] = (new_reg >> 8) & 255;
				registers[5] = new_reg & 255;
				if (log_to_console) cout << "\t\tHL + HL = " << registers[5] + registers[4] * 256 << endl;
				program_counter += 1;
				continue;

			}
			if (Dest == 3) //SP
			{
				//HL + SP = HL 
		
				unsigned int new_reg = stack_pointer + registers[4] * 256 + registers[5];
				Flag_Carry = (new_reg >> 16) & 1;
				registers[4] = (new_reg >> 8) & 255;
				registers[5] = new_reg & 255;
				if (log_to_console) cout << "\t\tHL + SP = " << registers[5] + registers[4] * 256 << endl;
				program_counter += 1;
				continue;
			}

		}

		// DAA Decimal Adjust Accumulator
		if (memory.read(program_counter) == 39)
		{
			temp_ACC_8 = registers[7] & 15;   
			temp_ACC_16 = registers[7];
			if (temp_ACC_8 > 9 || Flag_A_Carry)
			{
				temp_ACC_16 = temp_ACC_16 + 6;
				Flag_A_Carry = true;
			}
			temp_ACC_8 = (temp_ACC_16 >> 4) & 15;

			if (temp_ACC_8 > 9 || Flag_Carry)
			{
				temp_ACC_16 += 96; // +6 к старшим битам
				Flag_Carry = true;
				//Flag_Carry = (temp_ACC_16 >> 8) & 1;
			}
			registers[7] = temp_ACC_16 & 255;

			if (registers[7]) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (registers[7] >> 7) & 1;
			Flag_Parity = (~registers[7]) & 1;
			if (log_to_console) cout << "\t\tDAA (" << (int)(registers[7] >> 4) << ")(" << (int)(registers[7] & 15) << ") CY= " << Flag_Carry << " CA= " << Flag_A_Carry << endl;
			program_counter += 1;
			continue;
		}

		// ============ Logical Group ===================

		//AND Register OR M[HL]
		if ((memory.read(program_counter) >> 3) == 20)
		{
			__int8 Src = memory.read(program_counter) & 7;
			
			if (Src != 6) // AND R
			{
				temp_ACC_16 = (registers[7] & registers[Src]) & 255;
				Flag_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;
				
				if (log_to_console) cout << "\tACC AND " << registers[Src] << " = " << registers[7] << endl;
				program_counter += 1;
				continue;
			}
			else
			{
				temp_ACC_16 = (registers[7] & memory.read(registers[4] * 256 + registers[5])) & 255;
				Flag_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;

				if (log_to_console) {
					cout << "\tACC AND M at [";
					SetConsoleTextAttribute(hConsole, 10);
					cout << (int)registers[4] * 256 + registers[5];
					SetConsoleTextAttribute(hConsole, 7);
					cout << "] = " << registers[7] << endl;
				}
				program_counter += 1;
				continue;
			}
		}
		//AND immediate
		if (memory.read(program_counter) == 230)
		{
			
			
			temp_ACC_16 = (registers[7] & memory.read(program_counter + 1)) & 255;
			Flag_Carry = false;
			if (temp_ACC_16) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (temp_ACC_16 >> 7) & 1;
			Flag_Parity = (~temp_ACC_16) & 1;
			registers[7] = temp_ACC_16;

			if (log_to_console) cout << "\t\tACC AND IMM(" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
			program_counter += 2;
			continue;
		}
		//XOR Register OR M[HL]
		if ((memory.read(program_counter) >> 3) == 21)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (log_to_console) cout << "\t\tACC(" << (bitset<8>)registers[7] << ") ";

			if (Src != 6) // XOR R
			{
				
				temp_ACC_16 = (registers[7] ^ registers[Src]) & 255;
				Flag_Carry = false;
				Flag_A_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;
				
				if (log_to_console) cout << "XOR " << regnames[Src] << "(" << (bitset<8>)registers[Src] << ") = " << (bitset<8>)registers[7] << endl;
				program_counter += 1;
				continue;
			}
			else
			{
				
				temp_ACC_16 = (registers[7] ^ memory.read(registers[4] * 256 + registers[5])) & 255;
				Flag_Carry = false;
				Flag_A_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;

				if (log_to_console) cout << "XOR M[HL] at " << (bitset<8>)(registers[4] * 256 + registers[5]) << " = " << (bitset<8>)registers[7] << endl;
				program_counter += 1;
				continue;
			}
		}
		//XOR immediate
		if (memory.read(program_counter) == 238)
		{

			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\tACC(" << (bitset<8>)registers[7] << ") ";
			
			temp_ACC_16 = (registers[7] ^ memory.read(program_counter + 1)) & 255;
			Flag_Carry = false;
			Flag_A_Carry = false;
			if (temp_ACC_16) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (temp_ACC_16 >> 7) & 1;
			Flag_Parity = (~temp_ACC_16) & 1;
			registers[7] = temp_ACC_16;

			if (log_to_console) cout << "\tXOR IMM (" << (bitset<8>)memory.read(program_counter + 1) << ") = " << (bitset<8>)registers[7] << endl;
			program_counter += 2;
			continue;
		}
		//OR Register OR M[HL]
		if ((memory.read(program_counter) >> 3) == 22)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;
			if (log_to_console) cout <<  "\t\tACC(" << registers[7] << ") ";

			if (Src != 6) // OR R
			{
				temp_ACC_16 = (registers[7] | registers[Src]) & 255;
				Flag_Carry = false;
				Flag_A_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;

				if (log_to_console) cout << " OR " << regnames[Src] << "(" << registers[Src] << ") = " << registers[7] << endl;
				program_counter += 1;
				continue;
			}
			else // Or M[HL]
			{
				temp_ACC_16 = (registers[7] | memory.read(registers[4] * 256 + registers[5])) & 255;
				Flag_Carry = false;
				Flag_A_Carry = false;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = (~temp_ACC_16) & 1;
				registers[7] = temp_ACC_16;
				if (log_to_console) cout << "OR M[HL] at " << registers[4] * 256 + registers[5] << " = " << registers[7] << endl;
				program_counter += 1;
				continue;
			}
		}
		//OR immediate
		if (memory.read(program_counter) == 246)
		{

			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\tACC(" << registers[7] << ") ";

			temp_ACC_16 = (registers[7] | memory.read(program_counter + 1)) & 255;
			Flag_Carry = false;
			Flag_A_Carry = false;
			if (temp_ACC_16) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (temp_ACC_16 >> 7) & 1;
			Flag_Parity = (~temp_ACC_16) & 1;
			registers[7] = temp_ACC_16;

			if (log_to_console) cout << " OR IMM (" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
			program_counter += 2;
			continue;
		}
		//Compare Register OR M[HL]
		if ((memory.read(program_counter) >> 3) == 23)
		{
			unsigned __int8 Src = memory.read(program_counter) & 7;

			if (Src != 6) // Compare R
			{
				temp_ACC_16 = registers[7] - registers[Src];
				temp_ACC_8 = (registers[7] & 15) - (registers[Src] & 15);
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				temp_ACC_16 = temp_ACC_16 & 255;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;
				
				if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with " << regnames[Src] << "(" << registers[Src] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
				program_counter += 1;
				continue;
			}
			else //Compare M[HL]
			{
				
				temp_ACC_16 = registers[7] - memory.read(registers[4] * 256 + registers[5]);
				temp_ACC_8 = (registers[7] & 15) - (memory.read(registers[4] * 256 + registers[5]) & 15);
				Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
				Flag_Carry = (temp_ACC_16 >> 8) & 1;
				temp_ACC_16 = temp_ACC_16 & 255;
				if (temp_ACC_16) Flag_Zero = false;
				else Flag_Zero = true;
				Flag_Sign = (temp_ACC_16 >> 7) & 1;
				Flag_Parity = ~registers[7] & 1;

				if (log_to_console) cout << "\t\tACC Comp with M at " << (int)(registers[4] * 256 + registers[5]) << " Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
				program_counter += 1;
				continue;
			}
		}
		//Compare immediate
		if (memory.read(program_counter) == 254)
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t";
			
			temp_ACC_16 = registers[7] - memory.read(program_counter + 1);
			temp_ACC_8 = (registers[7] & 15) - (memory.read(program_counter + 1) & 15);
			Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
			Flag_Carry = (temp_ACC_16 >> 8) & 1;
			temp_ACC_16 = temp_ACC_16 & 255;
			if (temp_ACC_16) Flag_Zero = false;
			else Flag_Zero = true;
			Flag_Sign = (temp_ACC_16 >> 7) & 1;
			Flag_Parity = ~registers[7] & 1;
			
			if (log_to_console) cout << "\tACC(" << registers[7] << ") Comp with IMM(" << (int)memory.read(program_counter + 1) << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
			program_counter += 2;
			continue;
		}
		//Rotate left
		if (memory.read(program_counter) == 7)
		{

			Flag_Carry = (registers[7] >> 7) & 1;
			registers[7] = (registers[7] << 1) & 255;
			registers[7] = registers[7] | Flag_Carry;
			if (log_to_console) cout << "\t\tRotate left ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		//Rotate right
		if (memory.read(program_counter) == 15)
		{

			Flag_Carry = registers[7] & 1;
			registers[7] = registers[7] >> 1;
			registers[7] = registers[7] | 128 * Flag_Carry;
			if (log_to_console) cout << "\t\tRotate right ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		//Rotate left through carry
		if (memory.read(program_counter) == 23)
		{
			bool tmp = (registers[7] >> 7) & 1; //старший бит
			registers[7] = (registers[7] << 1) & 255;
			registers[7] = registers[7] | Flag_Carry;
			Flag_Carry = tmp;
			if (log_to_console) cout << "\t\tRotate left through CY ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		//Rotate right through carry
		if (memory.read(program_counter) == 31)
		{
			bool tmp = registers[7] & 1;
			registers[7] = registers[7] >> 1;
			registers[7] = registers[7] + Flag_Carry * 128;
			Flag_Carry = tmp;
			if (log_to_console) cout << "\t\tRotate right through CY ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		//Complement accumulator
		if (memory.read(program_counter) == 47)
		{
			if (log_to_console) cout << "\t\tInvert ACC(" << (bitset<8>)registers[7] << ")";
			registers[7] = (~registers[7]) & 255;
			if (log_to_console) cout << " = " << (int)(registers[7]) << endl;
			program_counter += 1;
			continue;
		}
		//Complement carry
		if (memory.read(program_counter) == 63)
		{
			Flag_Carry = !Flag_Carry;
			if (log_to_console) cout << "\t\tInvert CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		//Set carry
		if (memory.read(program_counter) == 55)
		{
			Flag_Carry = true;
			if (log_to_console) cout << "\t\tset CY = " << Flag_Carry << endl;
			program_counter += 1;
			continue;
		}
		// ============= Branch Group ================
		//Jump
		if (memory.read(program_counter) == 195 || memory.read(program_counter) == 203) // 203 - недокументированная
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
			program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			if (log_to_console) cout << "jump to " << (int)program_counter << endl;
			continue;
		}
		//Call
		if (memory.read(program_counter) == 205 || memory.read(program_counter) == 221 || memory.read(program_counter) == 237 || memory.read(program_counter) == 253) // три последние недокументированные
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";

			memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
			memory.write(stack_pointer - 2, (program_counter + 3) & 255);
			stack_pointer -= 2;
			program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
			if (log_to_console) cout << "CALL -> " << (int)program_counter << " (return to " << int(memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256) << ")" << endl;
			continue;
		}
		//Conditional call
		if ((memory.read(program_counter) & 199) == 196)
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";

			string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";

			__int8 cond = (memory.read(program_counter) >> 3) & 7;

			if (cond == 0) flags = "[NOT Zero]" + flags;
			if (cond == 1) flags = "[Zero]" + flags;
			if (cond == 2) flags = "[No Carry]" + flags;
			if (cond == 3) flags = "[Carry]" + flags;
			if (cond == 4) flags = "[Not Parity]" + flags;
			if (cond == 5) flags = "[Parity]" + flags;
			if (cond == 6) flags = "[Plus]" + flags;
			if (cond == 7) flags = "[Minus]" + flags;

			if (cond == 0 && !Flag_Zero)  //call if NOT zero
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 1 && Flag_Zero)  //call if Zero
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 2 && !Flag_Carry)  //call if CY = 0
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 3 && Flag_Carry)  //call if CY = 1
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 4 && !Flag_Parity)  //call if Not Parity (odd)
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 5 && Flag_Parity)  //call if Parity (even)
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 6 && !Flag_Sign)  //call if +
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 7 && Flag_Sign)  //call if -
			{
				memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
				memory.write(stack_pointer - 2, (program_counter + 3) & 255);
				stack_pointer -= 2;
				program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
				if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
				continue;
			}
			if (log_to_console) cout << "cond Call [deny]" << flags << endl;
			program_counter += 3;
			continue;
		}
		//Return
		if (memory.read(program_counter) == 201 || memory.read(program_counter) == 217) // 217 - недокументированная
		{
			program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
			stack_pointer += 2;
			SetConsoleTextAttribute(hConsole, 0x17);
			if (log_to_console) cout << "\t\tRET to " << (int)program_counter << endl;
			SetConsoleTextAttribute(hConsole, 7);
			continue;
		}
		//Conditional return
		if ((memory.read(program_counter) & 199) == 192)
		{
			string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";

			__int8 cond = (memory.read(program_counter) >> 3) & 7;
			if (cond == 0) flags = "[NOT Zero]" + flags;
			if (cond == 1) flags = "[Zero]" + flags;
			if (cond == 2) flags = "[No Carry]" + flags;
			if (cond == 3) flags = "[Carry]" + flags;
			if (cond == 4) flags = "[Not Parity]" + flags;
			if (cond == 5) flags = "[Parity]" + flags;
			if (cond == 6) flags = "[Plus]" + flags;
			if (cond == 7) flags = "[Minus]" + flags;

			if (cond == 0 && !Flag_Zero)  //RET if NOT zero
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 1 && Flag_Zero)  //RET if Zero
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 2 && !Flag_Carry)  //RET if CY = 0
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 3 && Flag_Carry)  //RET if CY = 1
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 4 && !Flag_Parity)  //RET if Not Parity (odd)
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 5 && Flag_Parity)  //RET if Parity (even)
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 6 && !Flag_Sign)  //RET if +
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (cond == 7 && Flag_Sign)  //RET if -
			{
				program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
				continue;
			}
			if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
			program_counter += 1;
			continue;
		}
		//PCHL (Jump H and L indirect - move H and L to PC)
		if ((memory.read(program_counter) & 199) == 233)
		{
			program_counter = registers[4] * 256 + registers[5];
			if (log_to_console) cout << "\t\tJump to [HL] " << program_counter << endl;
			continue;
		}
		// =================  Stack, !/0, and Machine Contro! Group  ===================
		//PUSH pair
		if ((memory.read(program_counter) & 207) == 197)
		{
			__int8 Src = (memory.read(program_counter) >> 4) & 3;

			if (Src == 0)
			{
				//загружаем ВС в стек
				memory.write(stack_pointer - 1, registers[0]);
				memory.write(stack_pointer - 2, registers[1]);
				stack_pointer -= 2;
				if (log_to_console) cout << "\t\tPUSH BC(" << registers[1] + registers[0] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
			if (Src == 1)
			{
				//загружаем DE в стек
				memory.write(stack_pointer - 1, registers[2]);
				memory.write(stack_pointer - 2, registers[3]);
				stack_pointer -= 2;
				if (log_to_console) cout << "\t\tPUSH DE(" << registers[3] + registers[2] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
			if (Src == 2)
			{
				//загружаем HL в стек
				memory.write(stack_pointer - 1, registers[4]);
				memory.write(stack_pointer - 2, registers[5]);
				stack_pointer -= 2;
				if (log_to_console) cout << "\t\tPUSH HL(" << registers[5] + registers[4] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
		}
		//PUSH PSW
		if (memory.read(program_counter) == 245)
		{
			memory.write(stack_pointer - 1, registers[7]);
			memory.write(stack_pointer - 2, (Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry));
			stack_pointer -= 2;
			if (log_to_console) cout << "\t\tPUSH PSW  " << (bitset<8>)(Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry) << endl;
			program_counter += 1;
			continue;
		}
		//POP pair
		if ((memory.read(program_counter) & 207) == 193)
		{
			__int8 Src = (memory.read(program_counter) >> 4) & 3;

			if (Src == 0)
			{
				//загружаем ВС из стека
				registers[0] = memory.read(stack_pointer + 1);
				registers[1] = memory.read(stack_pointer);
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tPOP BC(" << registers[1] + registers[0] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
			if (Src == 1)
			{
				//загружаем DE из стека
				registers[2] = memory.read(stack_pointer + 1);
				registers[3] = memory.read(stack_pointer);
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tPOP DE(" << registers[3] + registers[2] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
			if (Src == 2)
			{
				//загружаем HL из стека
				registers[4] = memory.read(stack_pointer + 1);
				registers[5] = memory.read(stack_pointer);
				stack_pointer += 2;
				if (log_to_console) cout << "\t\tPOP HL(" << registers[5] + registers[4] * 256 << ")" << endl;
				program_counter += 1;
				continue;
			}
		}
		//POP PSW
		if (memory.read(program_counter) == 241)
		{
			registers[7] = (memory.read(stack_pointer + 1) & 255);
			Flag_Sign = (memory.read(stack_pointer) >> 7) & 1;
			Flag_Zero = (memory.read(stack_pointer) >> 6) & 1;
			Flag_A_Carry = (memory.read(stack_pointer) >> 4) & 1;
			Flag_Parity = (memory.read(stack_pointer) >> 2) & 1;
			Flag_Carry = memory.read(stack_pointer) & 1;

			stack_pointer += 2;
			if (log_to_console) cout << "\t\tPOP PSW " << (bitset<8>)(Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry) << endl;
			program_counter += 1;
			continue;
		}
		//XTHL (Exchange stack top with H and L)
		if (memory.read(program_counter) == 227)
		{
			unsigned __int16 temp_H = registers[4];
			unsigned __int16 temp_L = registers[5];
			registers[5] = memory.read(stack_pointer);
			registers[4] = memory.read(stack_pointer + 1);
			memory.write(stack_pointer, temp_L);
			memory.write(stack_pointer + 1, temp_H);
			if (log_to_console) cout << "\t\tXTHL" << endl;
			program_counter += 1;
			continue;
		}
		//SPHL (MoveHLtoSP) 
		if (memory.read(program_counter) == 249)
		{
			stack_pointer = registers[4] * 256 + registers[5];
			if (log_to_console) cout << "\t\tLoad HL to SP" << endl;
			program_counter += 1;
			continue;
		}
		//Input from port
		if (memory.read(program_counter) == 219)
		{
			if (log_to_console) cout << "\t\tInput from port" << endl;
			program_counter++;
			continue;
		}
		//OUT port
		if (memory.read(program_counter) == 211)
		{
			if (log_to_console) cout << "\t\tOutput to port" << endl;
			program_counter++;
			continue;
		}
		//EI (Enable interrupts)
		if (memory.read(program_counter) == 251)
		{
			Interrupts_enabled = true;
			if (log_to_console) cout << "interrupts ON" << endl;
			speaker.beep_on();
			program_counter++;
			continue;
		}
		//DI (Disableinterrupts)
		if (memory.read(program_counter) == 243)
		{
			Interrupts_enabled = false;
			if (log_to_console) cout << "interrupts OFF" << endl;
			speaker.beep_off();
			program_counter++;
			continue;
		}
		// HLT (Halt)
		if (memory.read(program_counter) == 118)
		{
			if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\t" << "HALT" << endl;
			break;
		}
		SetConsoleTextAttribute(hConsole, 4);
		cout << "Unknown command (" << (int)memory.read(program_counter) << ")! ";
		SetConsoleTextAttribute(hConsole, 7);
		cout << "Program counter = " << program_counter << endl;
		cout << "Debug " << debug[0] << " " << debug[1] << " " << debug[2] << " " << debug[3] << endl;
		restart = true;
	}
	cout << "press a key" << endl;
	while (!_kbhit);
	return 0;
}

void print_all()
{
	//выводим значения всех регистров и памяти
	cout << "======================================================================================" << endl;
	for (int i = 0; i < 0x7600; i++)
	{
		//if (memory.read(i) > 0) cout << i << "\t\t" << (int)memory.read(i) << endl;
	}
	cout << "======================================================================================" << endl;
	cout << "PC = " << (int)program_counter << endl;
	cout << "SP = " << (int)stack_pointer << " [";
	int SP_t = stack_pointer;
	int c = 0;// counter
	do {
		cout << int(memory.read(SP_t)) << ", ";
		SP_t++;
		c++;
	} while (SP_t <= 0x76c7 && c < 20);
	cout << ")" << endl;

	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + " CY_A=" + to_string(Flag_A_Carry) + "]";
	cout << "FLAGS " << flags << endl;
	cout << "A = " << registers[7] << "\tCY= " << Flag_Carry << endl;
	cout << "B = " << registers[0] << "\tC = " << registers[1] << endl;
	cout << "D = " << registers[2] << "\tE = " << registers[3] << endl;
	cout << "H = " << registers[4] << "\tL = " << registers[5] << endl;
	cout << "======RAM=============================================================================" << endl;
	for (int i = 0; i < 20; i++) cout << (int)(0x7600 + i) << "\t" << (int)memory.read(0x7600 + i) << endl;
}

//определение методов монитора

void Video_device::sync(int elapsed_ms)
{
	//цикл отрисовки экрана
	unsigned int start = 0x77c2;			//сам экран
	if (RAM_amount == 16) start = 0x37c2;   //для версии 16К
	//unsigned int start = 0x75E6;   //захват области переменных
	//unsigned int start = 0x7600;   //область экрана 30208

	if (cursor_clock.getElapsedTime().asMicroseconds() > 500000)
	{
		cursor_clock.restart();
		cursor_flipflop = !cursor_flipflop;
	}

	sf::Text text(font);
	sf::Text text_speed(font);
	text.setCharacterSize(40);
	text_speed.setCharacterSize(40);
	//text.setFillColor(sf::Color::White);
	//text.setFillColor(sf::Color::Green);

	//текстура символов
	//font_sprite.setScale(sf::Vector2f(1.0, 1.0));


	main_window.clear();
	unsigned __int16 addr, font_t_x, font_t_y, mem_num;
	unsigned __int8 sym_code;
	bool attr_under = false;       //атрибут подчеркивания
	bool attr_blink = false;       //атрибут мигания
	bool attr_highlight = false;   //атрибут подсветки

	for (int y = -1; y < 25 + 1; y++)  //25
	{
		for (int x = -1; x < 64 + 1; x++)  //64
		{
			addr = start + y * (64 + 14) + x;
			if ((y >= 5 && y < 30 && x >= 8 && x < 72) || 1)
			{
				//область экрана

				if (y >= 0 && y < 25 && x >= 0 && x < 64) sym_code = memory.read(addr);
				else sym_code = 0; // область по краям экрана
				//sym_code = y;

				font_t_y = sym_code >> 4;
				font_t_x = sym_code - (font_t_y << 4);
				font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(0, 384), sf::Vector2i(48, 60)));
				font_sprite.setPosition(sf::Vector2f((x + 1) * 36, (y + 1) * 60));
				font_sprite.setScale(sf::Vector2f(1, 1));
				main_window.draw(font_sprite);

				if (cursor_x - 8 == x && cursor_y - 3 == y && video_enable && cursor_flipflop)
				{
					//рисуем курсор в позиции
					font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(15 * 48 + 48 * 0.25, 5 * 48), sf::Vector2i(48 * 0.75, 48)));
					font_sprite.setPosition(sf::Vector2f((x + 1) * 36, (y + 1) * 60));
					main_window.draw(font_sprite);
				}

				if (!(sym_code >> 7) && this->video_enable)  // код < 128 и видео активно
				{
					if (attr_under) //подчеркивание
					{
						font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(15 * 48 + 48 * 0.25, 5 * 48 + 36), sf::Vector2i(48 * 0.75, 6)));
						font_sprite.setPosition(sf::Vector2f((x + 1) * 36, (y + 1) * 60 + 6 * under_line_pos)); //рисуем на позиции подчеркивания
						main_window.draw(font_sprite);
					}

					if (!attr_blink || cursor_flipflop) { //с учетом атрибута мигания
						font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(font_t_x * 48 + 48 * 0.25 + attr_highlight * 768, font_t_y * 48), sf::Vector2i(48 * 0.75, 48)));
						font_sprite.setPosition(sf::Vector2f((x + 1) * 36, (y + 1) * 60));
						main_window.draw(font_sprite);
					}
				}
				if ((sym_code >> 7) && video_enable)  // код >= 128 (или режим служебных символов) и видео активно
				{
					if ((sym_code >> 6) & 1)
					{
						//выводим псевдографику (старшие биты 11)
						unsigned __int8 alt_code = (sym_code >> 2) & 15;
						if (alt_code < 11)
						{
							font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(48 + alt_code * 48, 384), sf::Vector2i(48, 48)));
							font_sprite.setPosition(sf::Vector2f((x + 1) * 36, (y + 1) * 60));
							font_sprite.setScale(sf::Vector2f(1, 1 * 0.75));
							main_window.draw(font_sprite);
						}
					}
					else
					{
						//сброс при переустановке
						attr_under = attr_blink = attr_highlight = false;

						//устанавливаем атрибуты  (старшие биты 10)
						//сначала непрозрачные transp_attr = 1;
						if (transp_attr)
						{
							if ((sym_code & 32) == 32) attr_under = true;
							if ((sym_code & 2) == 2) attr_blink = true;
							if (sym_code & 1) attr_highlight = true;
						}
						else
						{
							// = 0 прозрачные атрибуты, применяем сразу
							//применяем сразу (позже допилю, нужно вводить поправку)





						}
					}
				}
			}
			else
			{
				//область за экраном
				// mem_num = memory.read(addr);
				if (mem_num & 0) {
					text.setString(int_to_hex(mem_num));
					text.setPosition(sf::Vector2f(x * 36, y * 60));
					main_window.draw(text);
				}
			}
		}
	}
	if (RUSLAT_LED) {
		text.setFillColor(sf::Color::Green);
		//text.setCharacterSize(40);
		text.setString("RUS");
	}
	else
	{
		text.setFillColor(sf::Color::Red);
		//text.setCharacterSize(40);
		text.setString("LAT");
	}
	text.setPosition(sf::Vector2f(14, 1565));
	main_window.draw(text);
	if (!step_mode && elapsed_ms) {

		//обновляем массив времени кадра
		speed_history[0]++;
		if (speed_history[0] >= 10) speed_history[0] = 1;
		speed_history[speed_history[0]] = floor(op_counter * 1000000 / elapsed_ms + 1);
		//рассчитываем среднее время кадра
		int avg_speed = 0;
		for (int i = 1; i <= 10; i++) avg_speed += speed_history[i];
		avg_speed = avg_speed / 10;
		text_speed.setFillColor(sf::Color::White);
		//text_speed.setCharacterSize(40);
		text_speed.setString(to_string(avg_speed) + (string)" op/sec ");
		text_speed.setPosition(sf::Vector2f(200, 1565));
		main_window.draw(text_speed);
	}

	if (step_mode) text.setString("STEP ON");
	else text.setString("STEP OFF");
	text.setPosition(sf::Vector2f(700, 1565));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	if (log_to_console) text.setString("LOG ON");
	else text.setString("LOG OFF");
	text.setPosition(sf::Vector2f(900, 1565));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	if (step_mode)
	{
		text.setString("PC = " + int_to_hex(program_counter));
		text.setPosition(sf::Vector2f(1100, 1565));
		text.setFillColor(sf::Color::White);
		main_window.draw(text);
	}

	if (RAM_amount == 16) text.setString("VIDEO 16K");
	else text.setString("VIDEO 32K");
	text.setPosition(sf::Vector2f(1500, 1565));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	//вывод позиции курсора
	text.setString("(" + to_string((memory.read(0x7602) - 8)) + " " + to_string((memory.read(0x7603) - 3)) + ")");
	text.setPosition(sf::Vector2f(1750, 1565));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	//вывод частоты звука
#ifdef DEBUG
	if (speaker.get_frequancy() == 0) text.setString(tmp_s);
	else { tmp_s = to_string(speaker.get_frequancy()) + " Hz";  text.setString(tmp_s); }
	text.setPosition(sf::Vector2f(2000, 1565));
	text.setFillColor(sf::Color::Yellow);
	main_window.draw(text);
#endif	

	attr_blink = false;
	attr_highlight = false;
	attr_under = false;

	//вывод комментариев
	text.setString(comm1 + "    " + comm2);
	text.setPosition(sf::Vector2f(100, 1365));
	text.setFillColor(sf::Color::Yellow);
	main_window.draw(text);




	main_window.display();
	int_request = true;//устанавливаем флаг в конце кадра
	while (main_window.pollEvent()) {};
}

Video_device::Video_device()   // конструктор класса
{
	//инициализируем графические константы
	GAME_WINDOW_X_RES = (64 + 2) * 6 * 6 + 12; //2560
	GAME_WINDOW_Y_RES = (25 + 2) * 10 * 6 + 0; //1440

	//получаем данные о текущем дисплее
	my_display_H = sf::VideoMode::getDesktopMode().size.y;
	my_display_W = sf::VideoMode::getDesktopMode().size.x;

	cout << "Video Init " << my_display_H << " x " << my_display_W << " display" << endl;

	//создаем главное окно

	main_window.create(sf::VideoMode(sf::Vector2u(GAME_WINDOW_X_RES, GAME_WINDOW_Y_RES)), "8080 emulator", sf::Style::Titlebar, sf::State::Windowed);
	main_window.setPosition({ my_display_W - GAME_WINDOW_X_RES - 50, 50 });
	main_window.setFramerateLimit(120);
	main_window.setMouseCursorVisible(1);
	main_window.setKeyRepeatEnabled(0);
	main_window.setVerticalSyncEnabled(1);
	main_window.setActive(true);

	//загружаем шрифт
	//if (!font.openFromFile("AnkaCoder-C75-r.ttf")) cout << "Error loading font" << endl;
	//if (!font.openFromFile("trafaretkit.ttf")) cout << "Error loading font" << endl; 
	if (!font.openFromFile(path + "MOSCOW2024.otf")) cout << "Error loading font" << endl;

	//настройка текстур
	font_sprite.setScale(sf::Vector2f(1, 1));

	speed_history[0] = 1;//настраиваем массив измерений

	cursor_clock.restart(); //запускаем таймер мигания

}

void Video_device::set_command(unsigned __int8 data) //команда контроллеру
{
	command_reg = data;			//запись в регистр параметров CREG

	if (data == 0)				//команда сброса
	{
		count_param = 4;		// счетчик параметров для загрузки
		return;
	}

	if (((data >> 5) & 1) && ((data & 31) > 0)) //команда старта
	{
		video_enable = true;
		int_enable = true;
		//биты 4 - 2 количество символов между запросами ДМА
		//биты 1 - 0 количество ДМА запросов между прерываниями (?)

		// пока не буду устанавливать все флаги, может не актуально
		return;
	}

	if (((data >> 5) & 1) && ((data & 31) == 0)) //команда останова
	{
		video_enable = false;
		return;
	}

	if (data == 128) //установка позиции курсора
	{
		count_param = 2;	// счетчик параметров для загрузки
		return;
	}

	if (data == 160) //Разрешить прерывания
	{
		int_enable = true;
		return;
	}

	if (data == 192) //Запретить прерывания
	{
		int_enable = false;
		return;
	}

	if (data == 192) //Установить счетчик
	{
		//непонятно зачем
		return;
	}


}
void Video_device::set_param(unsigned __int8 data)	//параметры команды
{
	if (command_reg == 128) //установка позиции курсора
	{
		if (count_param == 2) //первый параметр = Х
		{
			//cout << "cursor_x= " << dec << (int)data << hex << endl;
			cursor_x = data;
			count_param--;
			return;
		}

		if (count_param == 1) //первый параметр = Х
		{
			//cout << "cursor_y= " << dec << (int)data << hex << endl;
			cursor_y = data;
			count_param--;
			return;
		}
	}

	if (command_reg == 0) //команда сброса
	{
		if (count_param == 4) //первый параметр
		{
			//7 бит - режим символов 0 - нормальные, 1 - spaced (хз что это)

			// биты 6-0 - количество столбцов
			if ((data & 127) + 1 <= 80) display_columns = data;
			else improper_command = true;
			count_param--;
			//cout << "display_columns = " << dec << (int)display_columns << hex << endl;
			return;
		}

		if (count_param == 3) //2-й параметр
		{
			//биты 7-6 - число строк в кадровом синхроимпульсе. Не нужно.
			//биты 5-0 - число строк на экране от 1 до 64
			display_lines = (data & 63) + 1;
			count_param--;
			//cout << "display_lines = " << dec << (int)display_lines << hex << endl;
			return;
		}
		if (count_param == 2) //3-й параметр
		{
			//биты 7-4 - позиция линии подчеркивания 1-16 (для символов с аттрибутом U)
			under_line_pos = ((data & 240) >> 4) + 1;

			//биты 3-0 - число горизонтальных линий в строке(высота символа в пикселах) 1-16
			line_height = (data & 15) + 1;
			count_param--;
			//cout << "under_line_pos = " << dec << (int)under_line_pos << " line_height = " << (int)line_height << hex << endl;
			return;
		}
		if (count_param == 1) //4-й параметр
		{
			// бит 7 - режим счетчика линий 0 - нормальный, 1 - смещенный на 1

			// бит 6 - атрибут поля 0 - "прозрачный" 1 - "непрозрачный"
			transp_attr = ((data >> 6) & 1);
			// биты 5-4 - формат курсора
			cursor_format = (data >> 4) & 3;
			//cout << "transp_attr = " << dec << (int)transp_attr << " cursor_format = " << (int)cursor_format << hex << endl;
			// биты 3-0 - число символов в строчном синхроимпульсе. Пока не нужно.
			//cout << "cursor_format = " << dec << (int)cursor_format << hex << endl;
			count_param--;
			return;
		}
	}
}

unsigned __int8 Video_device::get_status() //запрос статуса
{
	status = 0;
	status = status | (int_enable << 6);
	status = status | (int_request << 5);
	status = status | (video_enable << 2);
	status = status | (improper_command << 3);

	// сброс параметров после их запроса
	int_request = false;
	improper_command = false;
	return status; //чтение CREG
}

unsigned __int8 Video_device::get_params() //запрос параметров
{
	return 0; //чтение PREG
}


//определение методов клавиатуры

int KBD::get_key_C()
{
	//эмуляция порта С - три управляющие клавиши
	int code = 0xFF;

	if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) code = code & 112;     //R_Ctrl = Рус/Лат
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)) code = code & 176;     //R_Ctrl = CC
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift)) code = code & 223;     //R_Ctrl = УС
	return code;
}

int KBD::get_key_B()
{
	//эмуляция порта B - основная матрица клавиатуры
	//выдаем данные в соответствии с данными на порту А

	__int8 output = 255;

	if (((input_data >> 1) & 1) == 0) //байт 2 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Tab)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::PageDown)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Enter)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Backspace)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) return 127;
	}

	if (((input_data >> 2) & 1) == 0) //байт 3 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num0) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad0)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num1) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad1)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num2) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad2)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num3) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad3)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num4) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad4)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num5) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad5)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num6) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad6)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num7) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad7)) return 127;
	}

	if (((input_data >> 3) & 1) == 0) //байт 4 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num8) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad8)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num9) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Numpad9)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Multiply)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Add)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Comma)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Equal) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Subtract)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Period)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Slash)) return 127;
	}

	if (((input_data >> 4) & 1) == 0) //байт 5 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Grave)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::B)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::C)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::E)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::G)) return 127;
	}

	if (((input_data >> 5) & 1) == 0) //байт 6 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::H)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::I)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::J)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::K)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::L)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::M)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::N)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::O)) return 127;
	}

	if (((input_data >> 6) & 1) == 0) //байт 7 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::P)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Q)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::T)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::U)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::V)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) return 127;
	}

	if (((input_data >> 7) & 1) == 0) //байт 8 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::X)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Y)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Z)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LBracket)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Backslash)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RBracket)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space)) return 127;
	}

	if ((input_data & 1) == 0) //байт 1 порта А
	{
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Home)) return 254;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::PageUp)) return 253;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) return 251;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F1)) return 247;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F2)) return 239;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F3)) return 223;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F4)) return 191;
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F5)) return 127;
	}
	return output;
}

void KBD::port_A_input(__int8 data)
{
	input_data = data;
}

void Mem_Ctrl::write(unsigned __int16 address, unsigned __int8 data)
{
	address = address & (256 * 256 - 1);
	//cout << hex;
	// if ((address >> 12) == 8) cout << "port = " << (int)address << " write " << (int)data << endl;

	if (address == 0x8002) {
		//запись в порт С
		if ((data & 8) == 8) {
			RUSLAT_LED = true; //cout << "LED ON" << endl;    //зажигаем диод
		}
		if ((data & 8) == 0) {
			RUSLAT_LED = false; //cout << "LED OFF" << endl;            //гасим диод
		}
		return;
	}

	if (address == 0x8003) {
		//запись управляющего слова D20
		//cout << "8003 = " << (bitset<8>)data << endl;
		return;
	}

	if (address == 0x8000) {
		//cout << "KB write 8000 = " << (bitset<8>)data << endl; //скан клавиатуры
		keyboard.port_A_input(data);
		return;
	}

	if (address == 0xA003) {
		//запись управляющего слова D14 ROM-диск
		//cout << "A003 = " << (bitset<8>)data << endl;
		//step_mode = true;
		return;
	}

	if (address == 0xA001) {
		//cout << "A0001 -> " << (int)data << endl;
		HDD.set_addr_low(data);
		//step_mode = true;
		//log_to_console = true;
		return;
	}

	if (address == 0xA002) {
		//cout << "A0002 -> " << (int)data << endl;
		HDD.set_addr_high(data);
		//step_mode = true;
		//log_to_console = true;
		return;
	}

	if (address == 0xC001) {
		//cout << "monitor set command " << (bitset<8>)data << endl;
		monitor.set_command(data);
		//step_mode = true;
		//log_to_console = true;
		return;
	}

	if (address == 0xC000) {
		//cout << "monitor set params " << (bitset<8>)data << endl;
		monitor.set_param(data);
		//step_mode = true;
		//log_to_console = true;
		return;
	}

	if (address > size(mem_array))
	{
		global_error = 1;
	}
	else
	{
		mem_array[address] = data;
		//cout << "write at " << (int)address << " " << (int)data << endl;
	}
}

unsigned __int8 Mem_Ctrl::read(unsigned __int16 address)
{
	//if ((address >> 12) == 8) cout << "port = " << (int)address << " read " << endl;

	if (address > size(mem_array))
	{
		cout << "ERROR illegal address";
		return 0;
	}
	else
	{

		if (address == 0x8003) return 0; // считывать данные из этого порта нельзя

		//проверяем не указан ли порт 8002 (управляющие клавиши клавиатуры и магнитофон)
		if (address == 0x8002)
		{
			//порт С контроллера
			unsigned __int8 data = (keyboard.get_key_C()) & 255; //данные из субпорта C (биты С5-С7)
			data = data & 0b11100000;
			//data = data | (HDD.read_bit() << 4); //данные из субпорта C (бит С4)
			//cout << "read HDD " << data << endl;
			return data;
		}

		//проверяем не указан ли порт 8001 (основная матрица клавиатуры)
		if (address == 0x8001)
		{
			//порт С контроллера
			int k = (keyboard.get_key_B() & 255);
			//if (k != 255) { cout << "kbd = " << k << endl; step_mode = true; }
			return k;
		}

		if (address == 0x8000) return 0; //читать отсюда нельзя

		//проверяем не указан ли порт A002 (ПЗУ на микросхеме D14)
		if (address == 0xA000)
		{
			//порт A контроллера
			//cout << "Read HDD " << HDD.read_byte() << endl;
			//step_mode = true;
			//log_to_console = true;
			return HDD.read_byte();
		}

		//проверяем не указан ли порт C001 (контроллер монитора)
		if (address == 0xC001)
		{
			//cout << "Monitor get status" << endl;
			//запрос статуса
			//step_mode = true;
			//log_to_console = true;
			return monitor.get_status();
		}

		//проверяем не указан ли порт C000 (контроллер монитора)
		if (address == 0xC000)
		{
			//cout << "Monitor get params" << endl;
			//запрос параметров
			//step_mode = true;
			//log_to_console = true;
			return monitor.get_params();
		}
		return mem_array[address];
	}
}

void HDD_Ctrl::write_byte(unsigned __int8 data)
{
	//запись значений на диск по байтам при старте эмулятора
	data_array[byte_pointer] = data;
	byte_pointer++;
	if (byte_pointer == size(data_array))
	{
		cout << "превышение адресного пространства HDD (" << (int)byte_pointer << "). Сброс в начало." << endl;
		byte_pointer = 0;
	}

}

unsigned __int8 HDD_Ctrl::read_byte()
{
	if (byte_pointer >= size(data_array))
	{
		cout << "превышение адресного пространства HDD. Чтение невозможно" << endl;
		return 0;
	}

	//чтение бита с виртуального диска
	return data_array[byte_pointer];
}

void HDD_Ctrl::set_addr_low(unsigned __int8 data)
{
	byte_pointer = byte_pointer & 0xFF00;
	byte_pointer = byte_pointer | data;
	//cout << "set low " << (int)byte_pointer << endl;
}

void HDD_Ctrl::set_addr_high(unsigned __int8 data)
{
	byte_pointer = byte_pointer & 0x00FF;
	byte_pointer = byte_pointer | (data << 8);
	//cout << "set high " << (int)byte_pointer << endl;
}

string get_sym(int code)
{
	string s = "";
	if (code == 0x1b) s = "<ESC>";
	if (code == 0x08) s = "<-";
	if (code == 0x18) s = "->";
	if (code == 0x19) s = "<UP>";
	if (code == 0x1A) s = "<Down>";
	if (code == 0x0D) s = "<ВК>";
	if (code == 0x0A) s = "<ПС>";
	if (code == 0x1F) s = "<CLS>";
	if (code == 0x0C) s = "<HOME>";
	if (code == 0x2E) s = ".";
	if (code == 0x59) s = "Y";
	if (code == 0x0E) s = ">";
	if (code == 0x07) s = "<BEEP>";
	if (code >= 0x30 && code <= 0x39) s = to_string((char)(code - 0x30));

	return s;
}

void SoundMaker::sync()	//счетчик тактов
{
	//monitor.comm1 = "stream status" + to_string((int)audio_stream.getStatus());
	
	silense_dur++;
	if (silense_dur >= 30)
	{
		signal_on = -1;
		silense_dur = 0;
		//чистим буфер
		//cout << "free waves" << endl;
		//for (int i = 0; i < 10; i++) waves[i] = 0;
		//empty = true;
	}

	if (sound_timer.getElapsedTime().asMicroseconds() < 200000) return; //выход, если таймер слишком мал
	sound_timer.restart();

	int f = get_frequancy(); //получаем текущую частоту
	if (!f) return;			 //если ничего не играет - возврат

	// создаем звуковой сэмпл
	for (int i = 0; i < sample_size; i++)
	{
		//f = 300;
		float step = 8000.0 / f;			//период частоты в отсчетах
		float a = i / step * 3.1415;        //угол
		float h =  (sin(a) - 0.5) * 20000 + 10000;
		sound_sample[i] = floor(h) * sin(3.1415 * i / sample_size);  //=SIN(3,14*A11/B11)
	}
	//monitor.comm2 = "  freq " + to_string(f);

	//очистка буфера
	for (int i = 0; i < 14; i++) waves[i] = 0;
	empty = true; pointer = 0;

	audio_stream.buffer_ready = true;
	//cout << "stream -> add new data" << endl;

	//if (audio_stream.getStatus() == sf::SoundSource::Status::Stopped || audio_stream.getStatus() == sf::SoundSource::Status::Paused)
	{
		audio_stream.stop();
		audio_stream.play();
		
		//cout << "stream -> start play" << endl;
	}
}	

void SoundMaker::beep_on()     //сигнал ВКЛ
{
	//return;
	//cout << "dur " << (int)silense_dur << endl;
	if (signal_on != 1)
	{
		pointer++;
		if (pointer == 8) pointer = 0;
	}
	signal_on = 1;
	silense_dur = 0;
	waves[pointer]++; //увеличиваем счетчик длительности импульсов
	empty = false;
	//cout << waves[pointer] << " ";
}
void SoundMaker::beep_off()    //сигнал ВЫКЛ
{
	//return;
	if (signal_on != 0)
	{
		pointer++;
		if (pointer == 8) pointer = 0;
	}
	signal_on = 0;
	silense_dur = 0;
	waves[pointer]++; //увеличиваем счетчик длительности импульсов
	empty = false;
	//cout << waves[pointer] << " ";
}

int SoundMaker::get_frequancy()
{
	if (empty) return 0;  // если буфер пуст сразу выходим
	
	//рассчет частоты звука
	int sum = 0;	//сумма
	int count = 0;  //кол-во чисел
	//cout << "array ";
	for (int i = 0; i < 8; i++)
	{
		//cout << (int)waves[i] << "  ";
		if (waves[i] && i!=pointer)
		{
			count++;
			sum += waves[i];
		}
	}
	//cout << endl;
	if (!count) return 0;
	//cout << to_string((int)floor(85000 * count / sum)) << endl;
	int f = floor(42000 * count / sum);
	//if (f < 200) f = 200;
	//if (f > 2000) f = 1600;
	return f;
}

bool MyAudioStream::onGetData(Chunk& data)
{
	if (buffer_ready)
	{
		data.samples = s_buffer;
		data.sampleCount = sample_size;
		buffer_ready = false;
		//cout << "data -> stream" << endl;
		return true;
	}
	//cout << "buffer empty -> stop" << endl;
	//stop();
	return false;
}

void MyAudioStream::onSeek(sf::Time timeOffset)
{

}

void syscallF809()
{
	cout << "sysF809 ";
	//системный вызов вывод символа
	//регистр С - код символа
	if (memory.read(0x7604)) return; //выход если вводится ESC-последовательность
	if (registers[1] == 27 ) return; //начало ESC-последовательности
	//рассчитываем адрес курсора
	unsigned __int16 t_Addr = memory.read(0x7600) + memory.read(0x7601) * 256;
	memory.write(t_Addr, registers[1]); //пишем в память
	//переходим на адрес возврата
	program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
	stack_pointer += 2;

}