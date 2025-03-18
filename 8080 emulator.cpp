/*
Emulator of i8080 processor.It is better to run it from the MSVC IDE in DEBUG mode. At least it works fine for me in this case.
So far, the emulator understands only the text format of files (this is convenient for adding comments and debugging).
The emulator uses SFML to construct image.

Keys for command string (not tested):
8080_emulator.exe -f BusiCom.txt -ru -list -step -log
 -f <filename>   - txt file with program
 -ru             - russian localization
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
bool cont_exec = true; //переменная для продолжения основного цикла. снимается HALT

string path = ""; //текущий каталог
//текстура шрифта
sf::Texture font_texture;
sf::Sprite font_sprite(font_texture);

//таймеры
sf::Clock myclock;
sf::Clock video_clock;
sf::Clock cpu_clock; //таймер для выравнивания скорости процессора

//счетчики
unsigned int op_counter = 0;
unsigned int service_counter = 0;

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
	void flash_rom(unsigned __int16 address, unsigned __int8 data); //запись в ПЗУ
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
	int speed_history[33] = { 100000 };
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
	unsigned __int8 cursor_x = 0;		//позиция курсора
	unsigned __int8 cursor_y = 0;
	unsigned __int8 display_lines = 30;  //кол-во строк на экране
	unsigned __int8 display_columns = 78;//кол-во столбцов на экране
	unsigned __int8 under_line_pos = 10;	 //позиция линии подчеркивания (по высоте)
	unsigned __int8 cursor_format = 1;	 //формат курсора: 0 - мигающий блок, 1 - мигающий штрих, 2 - инверсный блок, 3 - немигающий штрих
	bool transp_attr = true;			 //невидимый атрибут поля (при установке специальных атрибутов) 0 - невидимый, 1 - обычный (с разрывами)

public:
	unsigned __int8 line_height = 10;	 //высота строки в пикселях
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
	unsigned int waves[20] = { 0 };     //подсчет импульсов
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
//string filename_ROM = "test86rk.txt";   //программа проверки из журнала
//string filename_ROM = "test.txt";		  //отладка команд
//string filename_ROM = "memtest32.txt";  //типовой тест памяти
//tring filename_ROM = "86RK32.txt";	  //типовая прошивка
string filename_ROM = "86RK32fix.txt";  //исправленная прошивка
//string filename_ROM = "bios16.txt";     //прошивка с сайта rk86.ru

// HDD

//string filename_HDD = "1_CHERV.txt";		//  0000 - норм
//string filename_HDD = "klad.txt";			//  0000 - норм
//string filename_HDD = "glass1.txt";		//  0000 - норм
//string filename_HDD = "diverse.txt";		//  0000 - норм
//string filename_HDD = "vmemtest.txt";		//  0000 - тест видеопамяти
//string filename_HDD = "formula.txt";		//  0000 - гонки
//string filename_HDD = "sirius.txt";		//  0000  - не видно врагов, нестандартный знакоген
//string filename_HDD = "xonix.txt";		//  0000 - норм
//string filename_HDD = "test_message.txt";
//string filename_HDD = "pacman.txt";		//  0000 - норм
//string filename_HDD = "music.txt";		//  0000 - ну, что-то играет
//string filename_HDD = "rk86_basic.txt";	//	0000 - зависает
//string filename_HDD = "stena.txt";		//	0000 - норм
//string filename_HDD = "pi80.txt";			//	4200 - норм
//string filename_HDD = "zmeya.txt";		//	0000 - норм


unsigned __int16 program_counter = 0xf800;  // первая команда при старте ПК
int first_address_ROM = 0xF800;			    // адрес загрузки ROM (прошивки)
int shift_for_load_to_RAM = 0x0;		    // смещение при загрузке сразу в память
#ifdef DEBUG
vector<int> breakpoints;                    // точки останова
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
bool step_mode = false;		//ждать ли нажатия пробела для выполнения команд
bool go_forward;			//переменная для выхода из цикла обработки нажатий
bool RU_lang = true;		//локализация
bool list_at_start = false; //вывод листинга на старте
bool log_to_console = false;//логирование команд на консоль
bool short_print = false;	//сокращенный набор регистров для вывода

void print_all();
string get_sym(int code);

//замена системных вызовов
void syscallF809();

//таблица указателей на функции для выполнения кодов операций
void (*op_code_table[256])() = {0};

//декларирование функций обработчиков операций

void op_code_NOP();			// No operation
//============== Data Transfer Group ===========

void op_code_MOV_R_R();		// MOV (R) to (R)
void op_code_MOV_M_R();		// MOV (M) to (R)
void op_code_MOV_R_M();		// MOV (R) to (M)
void op_code_MVI_R();		// MOV (IMM) to (R)
void op_code_MVI_M();		// MOV (IMM) to (M)

void op_code_LXI_BC();		// MOV (IMM) to (BC)
void op_code_LXI_DE();		// MOV (IMM) to (DE)
void op_code_LXI_HL();		// MOV (IMM) to (HL)
void op_code_LXI_SP();		// MOV (IMM) to (SP)

void op_code_LDA();			// LDA from M[IMM]
void op_code_STA();			// STA to M[IMM]
void op_code_LHLD();		// LHLD from M[IMM]
void op_code_SHLD();		// SHLD to M[IMM]
void op_code_LDAX_BC();		// LDA from M[BC]
void op_code_LDAX_DE();		// LDA from M[DE]
void op_code_STAX_BC();		// STA to M[BC]
void op_code_STAX_DE();		// STA to M[DE]
void op_code_XCHG();		// XCHG (HL<>DE)

//============== Arithmetic group =======================================
void op_code_ADD_B();		// ADD (B)
void op_code_ADD_C();		// ADD (C)
void op_code_ADD_D();		// ADD (D)
void op_code_ADD_E();		// ADD (E)
void op_code_ADD_H();		// ADD (H)
void op_code_ADD_L();		// ADD (L)
void op_code_ADD_M();		// ADD (M)
void op_code_ADD_A();		// ADD (A)
void op_code_ADD_I();		// ADD IMM

void op_code_ADC_B();		// ADD (B) with Carry
void op_code_ADC_C();		// ADD (C) with Carry
void op_code_ADC_D();		// ADD (D) with Carry
void op_code_ADC_E();		// ADD (E) with Carry
void op_code_ADC_H();		// ADD (H) with Carry
void op_code_ADC_L();		// ADD (L) with Carry
void op_code_ADC_M();		// ADD (M) with Carry
void op_code_ADC_A();		// ADD (A) with Carry
void op_code_ADC_I();		// ADD IMM with Carry

void op_code_SUB_B();		// SUB (B) 
void op_code_SUB_C();		// SUB (C) 
void op_code_SUB_D();		// SUB (D) 
void op_code_SUB_E();		// SUB (E) 
void op_code_SUB_H();		// SUB (H) 
void op_code_SUB_L();		// SUB (L) 
void op_code_SUB_M();		// SUB (M) 
void op_code_SUB_A();		// SUB (A) 
void op_code_SUB_I();		// SUB IMM 

void op_code_SBB_B();		// SUB (B)  with Carry
void op_code_SBB_C();		// SUB (C)  with Carry
void op_code_SBB_D();		// SUB (D)  with Carry
void op_code_SBB_E();		// SUB (E)  with Carry
void op_code_SBB_H();		// SUB (H)  with Carry
void op_code_SBB_L();		// SUB (L)  with Carry
void op_code_SBB_M();		// SUB (M)  with Carry
void op_code_SBB_A();		// SUB (A)  with Carry
void op_code_SBB_I();		// SUB IMM  with Carry

void op_code_INR_B();		// INR (B)
void op_code_INR_C();		// INR (C)
void op_code_INR_D();		// INR (D)
void op_code_INR_E();		// INR (E)
void op_code_INR_H();		// INR (H)
void op_code_INR_L();		// INR (L)
void op_code_INR_M();		// INR (M)
void op_code_INR_A();		// INR (A)

void op_code_DCR_B();		// DCR (B)
void op_code_DCR_C();		// DCR (C)
void op_code_DCR_D();		// DCR (D)
void op_code_DCR_E();		// DCR (E)
void op_code_DCR_H();		// DCR (H)
void op_code_DCR_L();		// DCR (L)
void op_code_DCR_M();		// DCR (M)
void op_code_DCR_A();		// DCR (A)

void op_code_INX_BC();		// INX (BC)
void op_code_INX_DE();		// INX (DE)
void op_code_INX_HL();		// INX (HL)
void op_code_INX_SP();		// INX (SP)

void op_code_DCX_BC();		// DCX (BC)
void op_code_DCX_DE();		// DCX (DE)
void op_code_DCX_HL();		// DCX (HL)
void op_code_DCX_SP();		// DCX (SP)

void op_code_DAD_BC();		// DAD (Add BC to HL)
void op_code_DAD_DE();		// DAD (Add DE to HL)
void op_code_DAD_HL();		// DAD (Add HL to HL)
void op_code_DAD_SP();		// DAD (Add SP to HL)

void op_code_DAA();			// DAA

// ============= Logical Group ===========================================
void op_code_AND_B();		// AND (B)
void op_code_AND_C();		// AND (C)
void op_code_AND_D();		// AND (D)
void op_code_AND_E();		// AND (E)
void op_code_AND_H();		// AND (H)
void op_code_AND_L();		// AND (L)
void op_code_AND_M();		// AND (M)
void op_code_AND_A();		// AND (A)
void op_code_AND_IMM();		// AND IMM

void op_code_XOR_B();		// XOR (B)
void op_code_XOR_C();		// XOR (C)
void op_code_XOR_D();		// XOR (D)
void op_code_XOR_E();		// XOR (E)
void op_code_XOR_H();		// XOR (H)
void op_code_XOR_L();		// XOR (L)
void op_code_XOR_M();		// XOR (M)
void op_code_XOR_A();		// XOR (A)
void op_code_XOR_IMM();		// XOR IMM

void op_code_OR_B();		// OR (B)
void op_code_OR_C();		// OR (C)
void op_code_OR_D();		// OR (D)
void op_code_OR_E();		// OR (E)
void op_code_OR_H();		// OR (H)
void op_code_OR_L();		// OR (L)
void op_code_OR_M();		// OR (M)
void op_code_OR_A();		// OR (A)
void op_code_OR_IMM();		// OR Imm

void op_code_CMP_B();		// CMP (B)
void op_code_CMP_C();		// CMP (C)
void op_code_CMP_D();		// CMP (D)
void op_code_CMP_E();		// CMP (E)
void op_code_CMP_H();		// CMP (H)
void op_code_CMP_L();		// CMP (L)
void op_code_CMP_M();		// CMP (M)
void op_code_CMP_A();		// CMP (A)
void op_code_CMP_IMM();		// CMP Imm

void op_code_RLC();			// RLC
void op_code_RRC();			// RRC
void op_code_RAL();			// RAL
void op_code_RAR();			// RAR
void op_code_CMA();			// CMA
void op_code_CMC();			// CMC
void op_code_STC();			// STC

// ============= Branch Group ===========================================
void op_code_JMP();			//Jump
void op_code_JMP_NZ();		//Cond Jump Not Zero
void op_code_JMP_Z();		//Cond Jump Zero
void op_code_JMP_NC();		//Cond Jump No Carry
void op_code_JMP_C();		//Cond Jump Carry
void op_code_JMP_NP();		//Cond Jump No Parity
void op_code_JMP_P();		//Cond Jump Parity
void op_code_JMP_Plus();	//Cond Jump Plus
void op_code_JMP_Minus();	//Cond Jump Minus
void op_code_CALL();		//Call
void op_code_CALL_NZ();		//Cond Call Not Zero
void op_code_CALL_Z();		//Cond Call Zero
void op_code_CALL_NC();		//Cond Call No Carry
void op_code_CALL_C();		//Cond Call Carry
void op_code_CALL_NP();		//Cond Call No Parity
void op_code_CALL_P();		//Cond Call Parity
void op_code_CALL_Plus();	//Cond Call Plus
void op_code_CALL_Minus();	//Cond Call Minus
void op_code_RET();			//RET
void op_code_RET_NZ();		//Cond RET Not Zero
void op_code_RET_Z();		//Cond RET Zero
void op_code_RET_NC();		//Cond RET No Carry
void op_code_RET_C();		//Cond RET Carry
void op_code_RET_NP();		//Cond RET No Parity
void op_code_RET_P();		//Cond RET Parity
void op_code_RET_Plus();	//Cond RET Plus
void op_code_RET_Minus();	//Cond RET Minus
void op_code_RSTn();		//RSTn
void op_code_PCHL();		//PCHL Jump to [HL]

//===========  Stack, !/0, and Machine Contro! Group  ===================
void op_code_EI();			//EI (Enable Interrupts)
void op_code_DI();			//DI (Disable Interrupts)
void op_code_PUSH_BC();		//PUSH pair (BC)
void op_code_PUSH_DE();		//PUSH pair (DE)
void op_code_PUSH_HL();		//PUSH pair (HL)
void op_code_POP_BC();		//POP pair (BC)
void op_code_POP_DE();		//POP pair (DE)
void op_code_POP_HL();		//POP pair (HL)
void op_code_PUSH_PSW();	//PUSH PSW
void op_code_POP_PSW();		//POP PSW
void op_code_XTHL();		//XTHL (Exchange stack top with H and L)
void op_code_SPHL();		//SPHL (Move HL to SP) 
void op_code_IN_Port();		//IN port
void op_code_OUT_Port();	//OUT port
void op_code_HLT();			//HLT (Halt)

int main(int argc, char* argv[]) {
#ifdef DEBUG
	//breakpoints.push_back(0xf7f);
	//breakpoints.push_back(0x05EB);
	//breakpoints.push_back(0x0160);
	//breakpoints.push_back(0x06c3);
	//breakpoints.push_back(0x06D8);
	//breakpoints.push_back(0x06E6);
	//breakpoints.push_back(0xA29);   // 1339  0132 0168 0A29
#endif
	
	//заполняем таблицу функций

	op_code_table[0b00000000] = &op_code_NOP; // NOP
	op_code_table[0b00001000] = &op_code_NOP; // NOP
	op_code_table[0b00010000] = &op_code_NOP; // NOP
	op_code_table[0b00011000] = &op_code_NOP; // NOP
	op_code_table[0b00100000] = &op_code_NOP; // NOP
	op_code_table[0b00101000] = &op_code_NOP; // NOP
	op_code_table[0b00110000] = &op_code_NOP; // NOP
	op_code_table[0b00111000] = &op_code_NOP; // NOP

	//============== Data Transfer Group ====================================
	op_code_table[0b01000000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101000] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111000] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101001] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111001] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101010] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111010] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101011] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111011] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101100] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111100] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101101] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111101] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01001111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01010111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01011111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01100111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01101111] = &op_code_MOV_R_R;	// MOV (R) to (R)
	op_code_table[0b01111111] = &op_code_MOV_R_R;	// MOV (R) to (R)

	op_code_table[0b01000110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01001110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01010110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01011110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01100110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01101110] = &op_code_MOV_M_R;	// MOV (M) to (R)
	op_code_table[0b01111110] = &op_code_MOV_M_R;	// MOV (M) to (R)

	op_code_table[0b01110000] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110001] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110010] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110011] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110100] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110101] = &op_code_MOV_R_M;	// MOV (R) to (M)
	op_code_table[0b01110111] = &op_code_MOV_R_M;	// MOV (R) to (M)

	op_code_table[0b00000110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00001110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00010110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00011110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00100110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00101110] = &op_code_MVI_R;		// MOV (IMM) to (R)
	op_code_table[0b00111110] = &op_code_MVI_R;		// MOV (IMM) to (R)

	op_code_table[0b00110110] = &op_code_MVI_M;		// MOV (IMM) to (M)

	op_code_table[0b00000001] = &op_code_LXI_BC;	// MOV (IMM) to (BC)
	op_code_table[0b00010001] = &op_code_LXI_DE;	// MOV (IMM) to (DE)
	op_code_table[0b00100001] = &op_code_LXI_HL;	// MOV (IMM) to (HL)
	op_code_table[0b00110001] = &op_code_LXI_SP;	// MOV (IMM) to (SP)

	op_code_table[0b00111010] = &op_code_LDA;		// LDA from M[IMM]
	op_code_table[0b00110010] = &op_code_STA;		// STA to M[IMM]
	op_code_table[0b00101010] = &op_code_LHLD;		// LHLD from M[IMM]
	op_code_table[0b00100010] = &op_code_SHLD;		// SHLD to M[IMM]
	op_code_table[0b00001010] = &op_code_LDAX_BC;	// LDA from M[BC]
	op_code_table[0b00011010] = &op_code_LDAX_DE;	// LDA from M[DE]
	op_code_table[0b00000010] = &op_code_STAX_BC;	// STA to M[BC]
	op_code_table[0b00010010] = &op_code_STAX_DE;	// STA to M[DE]
	op_code_table[0b11101011] = &op_code_XCHG;		// XCHG (HL<>DE)

	//============== Arithmetic group =======================================
	op_code_table[0b10000000] = &op_code_ADD_B;		// ADD (B)
	op_code_table[0b10000001] = &op_code_ADD_C;		// ADD (C)
	op_code_table[0b10000010] = &op_code_ADD_D;		// ADD (D)
	op_code_table[0b10000011] = &op_code_ADD_E;		// ADD (E)
	op_code_table[0b10000100] = &op_code_ADD_H;		// ADD (H)
	op_code_table[0b10000101] = &op_code_ADD_L;		// ADD (L)
	op_code_table[0b10000110] = &op_code_ADD_M;		// ADD (M)
	op_code_table[0b10000111] = &op_code_ADD_A;		// ADD (A)
	op_code_table[0b11000110] = &op_code_ADD_I;		// ADD IMM
	
	op_code_table[0b10001000] = &op_code_ADC_B;		// ADD (B) with Carry
	op_code_table[0b10001001] = &op_code_ADC_C;		// ADD (C) with Carry
	op_code_table[0b10001010] = &op_code_ADC_D;		// ADD (D) with Carry
	op_code_table[0b10001011] = &op_code_ADC_E;		// ADD (E) with Carry
	op_code_table[0b10001100] = &op_code_ADC_H;		// ADD (H) with Carry
	op_code_table[0b10001101] = &op_code_ADC_L;		// ADD (L) with Carry
	op_code_table[0b10001110] = &op_code_ADC_M;		// ADD (M) with Carry
	op_code_table[0b10001111] = &op_code_ADC_A;		// ADD (A) with Carry
	op_code_table[0b11001110] = &op_code_ADC_I;		// ADD IMM with Carry

	op_code_table[0b10010000] = &op_code_SUB_B;		// SUB (B) 
	op_code_table[0b10010001] = &op_code_SUB_C;		// SUB (C) 
	op_code_table[0b10010010] = &op_code_SUB_D;		// SUB (D) 
	op_code_table[0b10010011] = &op_code_SUB_E;		// SUB (E) 
	op_code_table[0b10010100] = &op_code_SUB_H;		// SUB (H) 
	op_code_table[0b10010101] = &op_code_SUB_L;		// SUB (L) 
	op_code_table[0b10010110] = &op_code_SUB_M;		// SUB (M) 
	op_code_table[0b10010111] = &op_code_SUB_A;		// SUB (A) 
	op_code_table[0b11010110] = &op_code_SUB_I;		// SUB IMM 

	op_code_table[0b10011000] = &op_code_SBB_B;		// SUB (B)  with Carry
	op_code_table[0b10011001] = &op_code_SBB_C;		// SUB (C)  with Carry
	op_code_table[0b10011010] = &op_code_SBB_D;		// SUB (D)  with Carry
	op_code_table[0b10011011] = &op_code_SBB_E;		// SUB (E)  with Carry
	op_code_table[0b10011100] = &op_code_SBB_H;		// SUB (H)  with Carry
	op_code_table[0b10011101] = &op_code_SBB_L;		// SUB (L)  with Carry
	op_code_table[0b10011110] = &op_code_SBB_M;		// SUB (M)  with Carry
	op_code_table[0b10011111] = &op_code_SBB_A;		// SUB (A)  with Carry
	op_code_table[0b11011110] = &op_code_SBB_I;		// SUB IMM  with Carry

	op_code_table[0b00000100] = &op_code_INR_B;		// INR (B)
	op_code_table[0b00001100] = &op_code_INR_C;		// INR (C)
	op_code_table[0b00010100] = &op_code_INR_D;		// INR (D)
	op_code_table[0b00011100] = &op_code_INR_E;		// INR (E)
	op_code_table[0b00100100] = &op_code_INR_H;		// INR (H)
	op_code_table[0b00101100] = &op_code_INR_L;		// INR (L)
	op_code_table[0b00110100] = &op_code_INR_M;		// INR (M)
	op_code_table[0b00111100] = &op_code_INR_A;		// INR (A)

	op_code_table[0b00000101] = &op_code_DCR_B;		// DCR (B)
	op_code_table[0b00001101] = &op_code_DCR_C;		// DCR (C)
	op_code_table[0b00010101] = &op_code_DCR_D;		// DCR (D)
	op_code_table[0b00011101] = &op_code_DCR_E;		// DCR (E)
	op_code_table[0b00100101] = &op_code_DCR_H;		// DCR (H)
	op_code_table[0b00101101] = &op_code_DCR_L;		// DCR (L)
	op_code_table[0b00110101] = &op_code_DCR_M;		// DCR (M)
	op_code_table[0b00111101] = &op_code_DCR_A;		// DCR (A)

	op_code_table[0b00000011] = &op_code_INX_BC;	// INX (BC)
	op_code_table[0b00010011] = &op_code_INX_DE;	// INX (DE)
	op_code_table[0b00100011] = &op_code_INX_HL;	// INX (HL)
	op_code_table[0b00110011] = &op_code_INX_SP;	// INX (SP)

	op_code_table[0b00001011] = &op_code_DCX_BC;	// DCX (BC)
	op_code_table[0b00011011] = &op_code_DCX_DE;	// DCX (DE)
	op_code_table[0b00101011] = &op_code_DCX_HL;	// DCX (HL)
	op_code_table[0b00111011] = &op_code_DCX_SP;	// DCX (SP)

	op_code_table[0b00001001] = &op_code_DAD_BC;	// DAD (Add BC to HL)
	op_code_table[0b00011001] = &op_code_DAD_DE;	// DAD (Add DE to HL)
	op_code_table[0b00101001] = &op_code_DAD_HL;	// DAD (Add HL to HL)
	op_code_table[0b00111001] = &op_code_DAD_SP;	// DAD (Add SP to HL)

	op_code_table[0b00100111] = &op_code_DAA;		//DAA
	
	//============== Logical group =======================================
	op_code_table[0b10100000] = &op_code_AND_B;		// AND (B)
	op_code_table[0b10100001] = &op_code_AND_C;		// AND (C)
	op_code_table[0b10100010] = &op_code_AND_D;		// AND (D)
	op_code_table[0b10100011] = &op_code_AND_E;		// AND (E)
	op_code_table[0b10100100] = &op_code_AND_H;		// AND (H)
	op_code_table[0b10100101] = &op_code_AND_L;		// AND (L)
	op_code_table[0b10100110] = &op_code_AND_M;		// AND (M)
	op_code_table[0b10100111] = &op_code_AND_A;		// AND (A)
	op_code_table[0b11100110] = &op_code_AND_IMM;	// AND Imm

	op_code_table[0b10101000] = &op_code_XOR_B;		// XOR (B)
	op_code_table[0b10101001] = &op_code_XOR_C;		// XOR (C)
	op_code_table[0b10101010] = &op_code_XOR_D;		// XOR (D)
	op_code_table[0b10101011] = &op_code_XOR_E;		// XOR (E)
	op_code_table[0b10101100] = &op_code_XOR_H;		// XOR (H)
	op_code_table[0b10101101] = &op_code_XOR_L;		// XOR (L)
	op_code_table[0b10101110] = &op_code_XOR_M;		// XOR (M)
	op_code_table[0b10101111] = &op_code_XOR_A;		// XOR (A)
	op_code_table[0b11101110] = &op_code_XOR_IMM;	// XOR Imm

	op_code_table[0b10110000] = &op_code_OR_B;		// OR (B)
	op_code_table[0b10110001] = &op_code_OR_C;		// OR (C)
	op_code_table[0b10110010] = &op_code_OR_D;		// OR (D)
	op_code_table[0b10110011] = &op_code_OR_E;		// OR (E)
	op_code_table[0b10110100] = &op_code_OR_H;		// OR (H)
	op_code_table[0b10110101] = &op_code_OR_L;		// OR (L)
	op_code_table[0b10110110] = &op_code_OR_M;		// OR (M)
	op_code_table[0b10110111] = &op_code_OR_A;		// OR (A)
	op_code_table[0b11110110] = &op_code_OR_IMM;	// OR Imm

	op_code_table[0b10111000] = &op_code_CMP_B;		// CMP (B)
	op_code_table[0b10111001] = &op_code_CMP_C;		// CMP (C)
	op_code_table[0b10111010] = &op_code_CMP_D;		// CMP (D)
	op_code_table[0b10111011] = &op_code_CMP_E;		// CMP (E)
	op_code_table[0b10111100] = &op_code_CMP_H;		// CMP (H)
	op_code_table[0b10111101] = &op_code_CMP_L;		// CMP (L)
	op_code_table[0b10111110] = &op_code_CMP_M;		// CMP (M)
	op_code_table[0b10111111] = &op_code_CMP_A;		// CMP (A)
	op_code_table[0b11111110] = &op_code_CMP_IMM;	// CMP Imm

	op_code_table[0b00000111] = &op_code_RLC;			// RLC
	op_code_table[0b00001111] = &op_code_RRC;			// RRC
	op_code_table[0b00010111] = &op_code_RAL;			// RAL
	op_code_table[0b00011111] = &op_code_RAR;			// RAR
	op_code_table[0b00101111] = &op_code_CMA;			// CMA
	op_code_table[0b00111111] = &op_code_CMC;			// CMC
	op_code_table[0b00110111] = &op_code_STC;			// STC

	// ====================== Branch Group ==================================
	op_code_table[0b11000011] = &op_code_JMP;		//Jump
	op_code_table[0b11001011] = &op_code_JMP;		//Jump undoc
	op_code_table[0b11000010] = &op_code_JMP_NZ;	//Cond Jump Not Zero
	op_code_table[0b11001010] = &op_code_JMP_Z;		//Cond Jump Zero
	op_code_table[0b11010010] = &op_code_JMP_NC;	//Cond Jump No Carry
	op_code_table[0b11011010] = &op_code_JMP_C;		//Cond Jump Carry
	op_code_table[0b11100010] = &op_code_JMP_NP;	//Cond Jump No Parity
	op_code_table[0b11101010] = &op_code_JMP_P;		//Cond Jump Parity
	op_code_table[0b11110010] = &op_code_JMP_Plus;	//Cond Jump Plus
	op_code_table[0b11111010] = &op_code_JMP_Minus;	//Cond Jump Minus
	op_code_table[0b11001101] = &op_code_CALL;		//Call
	op_code_table[0b11011101] = &op_code_CALL;		//Call undoc
	op_code_table[0b11101101] = &op_code_CALL;		//Call undoc
	op_code_table[0b11111101] = &op_code_CALL;		//Call undoc
	op_code_table[0b11000100] = &op_code_CALL_NZ;	//Cond Call Not Zero
	op_code_table[0b11001100] = &op_code_CALL_Z;	//Cond Call Zero
	op_code_table[0b11010100] = &op_code_CALL_NC;	//Cond Call No Carry
	op_code_table[0b11011100] = &op_code_CALL_C;	//Cond Call Carry
	op_code_table[0b11100100] = &op_code_CALL_NP;	//Cond Call No Parity
	op_code_table[0b11101100] = &op_code_CALL_P;	//Cond Call Parity
	op_code_table[0b11110100] = &op_code_CALL_Plus;	//Cond Call Plus
	op_code_table[0b11111100] = &op_code_CALL_Minus;//Cond Call Minus
	op_code_table[0b11001001] = &op_code_RET;		//RET
	op_code_table[0b11011001] = &op_code_RET;		//RET undocumented
	op_code_table[0b11000000] = &op_code_RET_NZ;	//Cond RET Not Zero
	op_code_table[0b11001000] = &op_code_RET_Z;		//Cond RET Zero
	op_code_table[0b11010000] = &op_code_RET_NC;	//Cond RET No Carry
	op_code_table[0b11011000] = &op_code_RET_C;		//Cond RET Carry
	op_code_table[0b11100000] = &op_code_RET_NP;	//Cond RET No Parity
	op_code_table[0b11101000] = &op_code_RET_P;		//Cond RET Parity
	op_code_table[0b11110000] = &op_code_RET_Plus;	//Cond RET Plus
	op_code_table[0b11111000] = &op_code_RET_Minus;	//Cond RET Minus
	op_code_table[0b11101001] = &op_code_PCHL;		//RCHL Jump to [HL]
	op_code_table[0b11000111] = &op_code_RSTn;		//RST0
	op_code_table[0b11001111] = &op_code_RSTn;		//RST1
	op_code_table[0b11010111] = &op_code_RSTn;		//RST2
	op_code_table[0b11011111] = &op_code_RSTn;		//RST3
	op_code_table[0b11100111] = &op_code_RSTn;		//RST4
	op_code_table[0b11101111] = &op_code_RSTn;		//RST5
	op_code_table[0b11110111] = &op_code_RSTn;		//RST6
	op_code_table[0b11111111] = &op_code_RSTn;		//RST7

	//===========  Stack, !/0, and Machine Contro! Group  ===================

	op_code_table[0b11111011] = &op_code_EI;		//EI (Enable Interrupts)
	op_code_table[0b11110011] = &op_code_DI;		//DI (Disable Interrupts)
	op_code_table[0b11000101] = &op_code_PUSH_BC;	//PUSH pair (BC)
	op_code_table[0b11010101] = &op_code_PUSH_DE;	//PUSH pair (DE)
	op_code_table[0b11100101] = &op_code_PUSH_HL;	//PUSH pair (HL)
	op_code_table[0b11000001] = &op_code_POP_BC;	//POP pair (BC)
	op_code_table[0b11010001] = &op_code_POP_DE;	//POP pair (DE)
	op_code_table[0b11100001] = &op_code_POP_HL;	//POP pair (HL)
	op_code_table[0b11110101] = &op_code_PUSH_PSW;	//PUSH PSW
	op_code_table[0b11110001] = &op_code_POP_PSW;	//POP PSW
	op_code_table[0b11100011] = &op_code_XTHL;		//XTHL (Exchange stack top with H and L)
	op_code_table[0b11111001] = &op_code_SPHL;		//SPHL (Move HL to SP) 
	op_code_table[0b11011011] = &op_code_IN_Port;	//IN port
	op_code_table[0b11010011] = &op_code_OUT_Port;	//OUT port
	op_code_table[0b01110110] = &op_code_HLT;		//HLT (Halt)

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
			memory.flash_rom(line_number, number); //пишем в память
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
			cout << "Файл HDD ";
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
				memory.write(line_count + shift_for_load_to_RAM, number);//пишем сразу в RAm
					
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
	cpu_clock.start();

	//предотвращение дребезга клавиш управления
	bool keys_up = true;

	cout << "Running..." << hex << endl;
	//основной цикл программы
	while (cont_exec)
	{
		op_counter++;   //счетчик операций
		service_counter++;  //счетчик для вызова служебных процедур

		//переход в пошаговый режим при попадании в точку останова
#ifdef DEBUG
		
		for (int b = 0; b < breakpoints.capacity(); b++)
		{
			if (program_counter == breakpoints.at(b))   //breakpoints.at(b)
			{
				step_mode = true;
				cout << "Breakpoint at " << (int)program_counter << endl;
				log_to_console = true;
			}
		}
#endif

		//перехват системных вызовов
		if (program_counter == 0xFCBA) syscallF809();

		//служебные подпрограммы
		if (service_counter == 100)
		{
			service_counter = 0;
			go_forward = false;

			if (!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F8) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F7) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F3) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F4) &&
				!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F6)) keys_up = true;

			//вызываем видеоадаптер по таймеру

			if (video_clock.getElapsedTime().asMicroseconds() > 20000)
			{
				video_clock.stop();
				monitor.sync(video_clock.getElapsedTime().asMicroseconds()); //синхроимпульс для монитора
				//cout << dec << "OP_count = " << op_counter << "  time (ms) = " << video_clock.getElapsedTime().asMicroseconds() << hex << endl;
				video_clock.restart();
				op_counter = 0;
			}



			//мониторинг нажатия клавиш в обычном режиме
#ifdef DEBUG
		//проверяем нажатие кнопки P
		//if (pressed_key == 112 || pressed_key == 167 || pressed_key == 80 || pressed_key == 135) step_mode = !step_mode;
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F9) && keys_up) { step_mode = !step_mode; keys_up = false; }

			//проверяем нажатие кнопки C
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F10) && keys_up) { log_to_console = !log_to_console; keys_up = false; }
			//if (pressed_key == 99 || pressed_key == 67 || pressed_key == 225 || pressed_key == 145) log_to_console = !log_to_console;
#endif

			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F3) && keys_up) { monitor.line_height--; keys_up = false; }
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F4) && keys_up) { monitor.line_height++; keys_up = false; }
#ifdef DEBUG
			//выводим содержимое регистров если эмулятор работает в обычном режиме
			if (!step_mode && !log_to_console && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F12) && keys_up) { print_all();  keys_up = false; }
#endif
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

		}

		//синхронизация звука
		speaker.sync();

		//замедление работы		
		if (!service_counter) for (int t = 0; t< 1100; t++); //торможение


		//основной цикл 
#ifdef DEBUG
		//выводим текущую команду в консоль
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
		
		//новый декодер через таблицу
		op_code_table[memory.read(program_counter)]();
		
		cpu_clock.restart();
		continue;
	}
	cout << "Program ended. Press a key" << endl;
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
	} while (SP_t < 0x76D0 && c < 20);
	cout << ")" << endl;

	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + " CY_A=" + to_string(Flag_A_Carry) + "]";
	cout << "FLAGS " << flags << endl;
	cout << "A = " << registers[7] << "\tCY= " << Flag_Carry << endl;
	cout << "B = " << registers[0] << "\tC = " << registers[1] << endl;
	cout << "D = " << registers[2] << "\tE = " << registers[3] << endl;
	cout << "H = " << registers[4] << "\tL = " << registers[5] << endl;
	cout << "======RAM=============================================================================" << endl;
	for (int i = 0xd800; i < 0xd810; i++) cout << (int)(0x0 + i) << "\t" << (int)memory.read(0x0 + i) << endl;
}

//определение методов монитора

void Video_device::sync(int elapsed_ms)
{
	//цикл отрисовки экрана
	unsigned int start = 0x77c2 - 78 - 1;			//сам экран
	if (RAM_amount == 16) start = 0x37c2 - 78 - 1;   //для версии 16К
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

	//отрисовка экрана
	for (int y = 0; y < 25 + 2; y++)  //25
	{
		for (int x = 0; x < 64 + 2; x++)  //64
		{
			addr = start + y * (64 + 14) + x;

				sym_code = memory.read(addr) & 127; // считываем символ и обнуляем старший бит
				font_t_y = sym_code >> 4;
				font_t_x = sym_code - (font_t_y << 4);
				
				//фон экрана
				font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(0, 384), sf::Vector2i(48, 60)));
				font_sprite.setPosition(sf::Vector2f((x + 0) * 36, (y + 0) * 60));
				font_sprite.setScale(sf::Vector2f(1, 1));
				main_window.draw(font_sprite);

				if (cursor_x == x + 7 && cursor_y == y + 2 && video_enable && cursor_flipflop)
				{
					//рисуем курсор в позиции
					font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(15 * 48 + 48 * 0.25, 5 * 48), sf::Vector2i(48 * 0.75, 48)));
					font_sprite.setPosition(sf::Vector2f((x + 0) * 36, (y + 0) * 6 * line_height));
					main_window.draw(font_sprite);
				}

				if (!(sym_code >> 7) && this->video_enable)  // код < 128 и видео активно
				{
					if (attr_under) //подчеркивание
					{
						
						font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(15 * 48 + 48 * 0.25, 5 * 48 + 36), sf::Vector2i(48 * 0.75, 6)));
						font_sprite.setPosition(sf::Vector2f((x + 0) * 36, (y + 0) * 6 * line_height + 6 * (min(under_line_pos, line_height) - 1))); //рисуем на позиции подчеркивания
						main_window.draw(font_sprite);
					}

					if (!attr_blink || cursor_flipflop) { //с учетом атрибута мигания
						font_sprite.setTextureRect(sf::IntRect(sf::Vector2i(font_t_x * 48 + 48 * 0.25 + attr_highlight * 768, font_t_y * 48), sf::Vector2i(48 * 0.75, 48)));
						font_sprite.setPosition(sf::Vector2f((x + 0) * 36, (y + 0) * 6 * line_height));
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
							font_sprite.setPosition(sf::Vector2f((x + 0) * 36, (y + 0) * 6 * line_height));
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
	text.setPosition(sf::Vector2f(30, 26 * 60));
	main_window.draw(text);
	if (!step_mode && elapsed_ms) {

		//обновляем массив времени кадра
		speed_history[0]++;
		if (speed_history[0] >= 32) speed_history[0] = 1;
		speed_history[speed_history[0]] = floor(op_counter * (1000000.0 / elapsed_ms) + 1);
		//cout << dec << speed_history[speed_history[0]] << hex << endl;
		//рассчитываем среднее время кадра
		int avg_speed = 0;
		for (int i = 1; i <= 32; i++) avg_speed += speed_history[i];
		avg_speed = (avg_speed >> 8) << 3;
		text_speed.setFillColor(sf::Color::White);
		//text_speed.setCharacterSize(40);
		text_speed.setString(to_string(avg_speed) + " op/sec ");
		text_speed.setPosition(sf::Vector2f(200, 26 * 60));
		main_window.draw(text_speed);
	}

	if (step_mode) text.setString("STEP ON");
	else text.setString("STEP OFF");
	text.setPosition(sf::Vector2f(700, 26 * 60));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	if (log_to_console) text.setString("LOG ON");
	else text.setString("LOG OFF");
	text.setPosition(sf::Vector2f(1000, 26 * 60));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);

	if (step_mode)
	{
		text.setString("PC = " + int_to_hex(program_counter));
		text.setPosition(sf::Vector2f(1200, 26 * 60));
		text.setFillColor(sf::Color::White);
		main_window.draw(text);
	}

	if (RAM_amount == 16) text.setString("VIDEO 16K");
	else text.setString("VIDEO 32K");
	text.setPosition(sf::Vector2f(1500, 26 * 60));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);


	//вывод позиции курсора
	/*
	text.setString("(X=" + to_string((memory.read(0x7602))) + " Y=" + to_string((memory.read(0x7603))) + ")");
	text.setPosition(sf::Vector2f(1750, 26 * 60));
	text.setFillColor(sf::Color::White);
	main_window.draw(text);
	*/

	//вывод частоты звука
#ifdef DEBUG
	/*
	if (speaker.get_frequancy() == 0) text.setString(tmp_s);
	else { tmp_s = to_string(speaker.get_frequancy()) + " Hz";  text.setString(tmp_s); }
	text.setPosition(sf::Vector2f(2000, 26 * 60));
	text.setFillColor(sf::Color::Yellow);
	main_window.draw(text);
	*/
#endif	

	attr_blink = false;
	attr_highlight = false;
	attr_under = false;

	//вывод комментариев
#ifdef DEBUG
	text.setString(comm1 + "    " + comm2);
	text.setPosition(sf::Vector2f(100, 1365));
	text.setFillColor(sf::Color::Yellow);
	main_window.draw(text);
#endif	
	/*
	text.setString("lines " + to_string(display_lines) + "  col " + to_string(display_columns) + " | line_height " + to_string(line_height));
	text.setPosition(sf::Vector2f(30, 1560));
	text.setFillColor(sf::Color::Yellow);
	main_window.draw(text);
	*/

	main_window.display();
	int_request = true;//устанавливаем флаг в конце кадра
	while (main_window.pollEvent()) {};
}

Video_device::Video_device()   // конструктор класса
{
	//инициализируем графические константы
	GAME_WINDOW_X_RES = (64 + 2 + 0) * 6 * 6 + 0; //
	GAME_WINDOW_Y_RES = (25 + 2 + 0) * 10 * 6 + 0; // поставим 30 линий

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
			if ((data & 127) + 1 <= 80) display_columns = (data & 127) + 1;
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
	//address = address & (256 * 256 - 1);
	//cout << hex << address << endl;
	// if ((address >> 12) == 8) cout << "port = " << (int)address << " write " << (int)data << endl;
	if (address >> 15)
	{
		//cout << " port " << endl;
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

		if (address == 0xC001 || address == 0xD801) {
			//cout << "monitor set command " << (bitset<8>)data << endl;
			monitor.set_command(data);
			//step_mode = true;
			//log_to_console = true;
			return;
		}

		if (address == 0xC000 || address == 0xD800) {
			//cout << "monitor set params " << (bitset<8>)data << endl;
			monitor.set_param(data);
			//step_mode = true;
			//log_to_console = true;
			return;
		}
	}
	else
	{
		mem_array[address] = data;
		//cout << "Write RAM" << (int)data << endl;
	}

}

void Mem_Ctrl::flash_rom(unsigned __int16 address, unsigned __int8 data)
{
	mem_array[address] = data;
}

unsigned __int8 Mem_Ctrl::read(unsigned __int16 address)
{
	//if ((address >> 12) == 8) cout << "port = " << (int)address << " read " << endl;

	if (!(address >> 15))
	{
		
		return mem_array[address];
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
		if (address == 0xC001 || address == 0xD801)
		{
			//cout << "Monitor get status" << endl;
			//запрос статуса
			//step_mode = true;
			//log_to_console = true;
			return monitor.get_status();
		}

		//проверяем не указан ли порт C000 (контроллер монитора)
		if (address == 0xC000 || address == 0xD800)
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

	if (sound_timer.getElapsedTime().asMicroseconds() < 20000) return; //выход, если таймер слишком мал
	sound_timer.restart();

	int f = get_frequancy(); //получаем текущую частоту
	if (!f) return;			 //если ничего не играет - возврат

	// создаем звуковой сэмпл
	for (int i = 0; i < sample_size; i++)
	{
		//f = 300;
		float h;
		int h_max = 20000; //максимальная амплитуда
		int step = (floor(8000.0 / f)) * 2;			//период частоты в отсчетах
		//float a = i / step * 3.1415;			//угол
		//h =  (sin(a) - 0.5) * 20000 + 10000;  //синус
		int mini_step = i % step;
		if (mini_step < step / 2) h = h_max;
		else h = -h_max;
		//sound_sample[i] = floor(h) * sin(3.1415 * i / sample_size);  //синус
		sound_sample[i] = h * sin(3.1415 * i / sample_size);  //квадрат
	}
	//monitor.comm2 = "  freq " + to_string(f);

	//очистка буфера
	for (int i = 0; i < 20; i++) waves[i] = 0;
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
		if (pointer == 20) pointer = 0;
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
		if (pointer == 20) pointer = 0;
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
	for (int i = 0; i < 20; i++)
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
	if (f < 100) f = 100;
	if (f > 2000) f = 2000;
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
	return;
}

void syscallF809()
{
	//cout << "syscall F809 (Char out) catch " << endl;
	//системный вызов вывод символа
	//регистр С - код символа
	if (memory.read(0x7604)) return; //выход если вводится ESC-последовательность
	if (registers[1] == 27 ) return; //начало ESC-последовательности
	//спецсимволы
	if (registers[1] == 0x1f)  //CLS
	{
		//cout << "SysCall CLS" << endl;
		//очищаем экран и устанавливаем курсор в начало
		for (int i = 0x76D0; i < 0x7ff4; i++) memory.write(i, 0);
		//вносим изменения в ячейки монитора
		memory.write(0x7602, 8);
		memory.write(0x7603, 3);
		__int16 new_addr = 0x77c2;
		memory.write(0x7600, new_addr & 255);
		memory.write(0x7601, new_addr >> 8);
		//отправляем команду видеочипу
		memory.write(0xC001, 128);
		memory.write(0xC000, 8); //X
		memory.write(0xC000, 3); //Y

		//извлекаем адрес возврата из стека
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
		return;
	}

	if (registers[1] == 0x08) return; //курсор влево
	if (registers[1] == 0x18) return; //курсор вправо
	if (registers[1] == 0x19) return; //курсор вверх
	if (registers[1] == 0x1a) return; //курсор вниз
	if (registers[1] == 0x0d) return; //возврат каретки
	if (registers[1] == 0x0a) return; //перевод строки
	if (registers[1] == 0x0c) return; //курсор в начало экрана

	//рассчитываем адрес курсора
	unsigned __int16 t_Addr = memory.read(0x7600) + memory.read(0x7601) * 256;
	memory.write(t_Addr, registers[1]); //пишем в память
	//перемещаем курсор вправо
	int cur_x = memory.read(0x7602) + 1;
	int cur_y = memory.read(0x7603);
	if (cur_x == 72)
	{
		cur_x = 8;
		cur_y++;
	}
	if (cur_y == 28)
	{
		//прокрутка экрана вверх
		cur_y = 27;
		for (int y = 0; y < 24; y++)
		{
			for (int x = 0; x < 64; x++)
			{
				memory.write(0x77c2 + y * 78 + x, memory.read(0x77c2 + (y + 1) * 78 + x));
			}
		}
	}
	//вносим изменения в ячейки монитора
	memory.write(0x7602, cur_x);
	memory.write(0x7603, cur_y);
	__int16 new_addr = 0x77c2 + (cur_y-3) * 78 + (cur_x - 8);
	memory.write(0x7600, new_addr & 255);
	memory.write(0x7601, new_addr >> 8);
	//отправляем команду видеочипу
	memory.write(0xC001, 128);
	memory.write(0xC000, cur_x); //X
	memory.write(0xC000, cur_y); //Y

	//извлекаем адрес возврата из стека
	program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
	stack_pointer += 2;
}
//================OPCODES==================================
void op_code_NOP()  // NOP
{
	 program_counter++;
#ifdef DEBUG
	 if (log_to_console) cout << "NOP" << endl;
#endif
}

void op_code_MOV_R_R()  //MOV R to R
{
	unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
	unsigned __int8 Src = memory.read(program_counter) & 7;
	
	//копирование между регистрами
	registers[Dest] = registers[Src];
#ifdef DEBUG
	if (log_to_console) cout << "\t\tMove " << regnames[Src] << " -> " << regnames[Dest] << "[" << registers[Dest] << "]" << endl;
#endif
	program_counter++;
}
void op_code_MOV_M_R()		// MOV (M) to (R)
{
	unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
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
}
void op_code_MOV_R_M()		// MOV (R) to (M)
{
	unsigned __int8 Src = memory.read(program_counter) & 7;
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
}
void op_code_MVI_R()		// MOV (IMM) to (R)
{
	unsigned __int8 Dest = (memory.read(program_counter) >> 3) & 7;
#ifdef DEBUG	
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\t";
#endif
	//загружаем непосредственные  данные из памяти в регистр
	registers[Dest] = memory.read(program_counter + 1);
#ifdef DEBUG
	if (log_to_console) {
		cout << "Load immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)memory.read(program_counter + 1);
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to " << regnames[Dest] << "(" << registers[Dest] << ")" << endl;
	}
#endif
	program_counter += 2;
}
void op_code_MVI_M()		// MOV (IMM) to (M)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\t";
#endif
	//загружаем непосредственные данные из памяти в адрес из [HL]
	unsigned __int16 addr = registers[4] * 256 + registers[5];
	memory.write(addr, memory.read(program_counter + 1));
#ifdef DEBUG
	if (log_to_console) {
		cout << "Load immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)memory.read(program_counter + 1);
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to address " << (unsigned __int16)addr << endl;
	}
#endif
	program_counter += 2;
}
void op_code_LXI_BC()		// MOV (IMM) to (BC)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2);
#endif
	//загружаем непосредственные данные в ВС
	registers[0] = memory.read(program_counter + 2);
	registers[1] = memory.read(program_counter + 1);
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tLoad immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)(registers[1] + registers[0] * 256);
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to BC" << endl;
	}
#endif
	program_counter += 3;
}
void op_code_LXI_DE()		// MOV (IMM) to (DE)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2);
#endif
	//загружаем непосредственные данные в DE
	registers[2] = memory.read(program_counter + 2);
	registers[3] = memory.read(program_counter + 1);
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tLoad immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)(registers[3] + registers[2] * 256);
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to DE" << endl;
	}
#endif
	program_counter += 3;
}
void op_code_LXI_HL()		// MOV (IMM) to (HL)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2);
#endif
	//загружаем непосредственные данные в HL
	registers[4] = memory.read(program_counter + 2);
	registers[5] = memory.read(program_counter + 1);
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tLoad immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)(registers[5] + registers[4] * 256);
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to HL" << endl;
	}
#endif
	program_counter += 3;
}
void op_code_LXI_SP()		// MOV (IMM) to (SP)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2);
#endif
	//загружаем непосредственные данные в SP
	stack_pointer = memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1);
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tLoad immediate [";
		SetConsoleTextAttribute(hConsole, 5);
		cout << (int)(memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1));
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] to SP" << endl;
	}
#endif
	program_counter += 3;
}

void op_code_LDA()			// LDA from M[IMM]
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
}
void op_code_STA()			// STA to M[IMM]
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
}
void op_code_LHLD()		// LHLD from M[IMM]
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
}
void op_code_SHLD()		// SHLD to M[IMM]
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif
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
}
void op_code_LDAX_BC()		// LDA from M[BC]
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
}
void op_code_LDAX_DE()		// LDA from M[DE]
{
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
}
void op_code_STAX_BC()		// STA to M[BC]
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
}
void op_code_STAX_DE()		// STA to M[DE]
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
}
void op_code_XCHG()		// XCHG (HL<>DE)
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
}


void op_code_ADD_B()		// ADD (B)
{
	temp_ACC_16 = registers[7] + registers[0];
	temp_ACC_8 = (registers[7] & 15) + (registers[0] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + B(" << registers[0] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_C()		// ADD (C)
{
	temp_ACC_16 = registers[7] + registers[1];
	temp_ACC_8 = (registers[7] & 15) + (registers[1] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + C(" << registers[1] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_D()		// ADD (D)
{
	temp_ACC_16 = registers[7] + registers[2];
	temp_ACC_8 = (registers[7] & 15) + (registers[2] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + D(" << registers[2] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_E()		// ADD (E)
{
	temp_ACC_16 = registers[7] + registers[3];
	temp_ACC_8 = (registers[7] & 15) + (registers[3] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + E(" << registers[3] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_H()		// ADD (H)
{
	temp_ACC_16 = registers[7] + registers[4];
	temp_ACC_8 = (registers[7] & 15) + (registers[4] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + H(" << registers[4] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_L()		// ADD (L)
{
	temp_ACC_16 = registers[7] + registers[5];
	temp_ACC_8 = (registers[7] & 15) + (registers[5] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + L(" << registers[5] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_M()		// ADD (M)
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
}
void op_code_ADD_A()		// ADD (A)
{
	temp_ACC_16 = registers[7] + registers[7];
	temp_ACC_8 = (registers[7] & 15) + (registers[7] & 15);
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + A(" << registers[7] << ") = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADD_I()	// ADD IMM
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
}

void op_code_ADC_B()		// ADD (B) with Carry
{
	temp_ACC_16 = registers[7] + registers[0] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[0] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + B + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_C()		// ADD (C) with Carry
{
	temp_ACC_16 = registers[7] + registers[1] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[1] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + C + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_D()		// ADD (D) with Carry
{
	temp_ACC_16 = registers[7] + registers[2] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[2] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + D + CY = " << registers[7] << endl;
#endif
	program_counter++;
	}
void op_code_ADC_E()		// ADD (E) with Carry
{
	temp_ACC_16 = registers[7] + registers[3] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[3] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + E + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_H()		// ADD (H) with Carry
{
	temp_ACC_16 = registers[7] + registers[4] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[4] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + H + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_L()		// ADD (L) with Carry
{
	temp_ACC_16 = registers[7] + registers[5] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[5] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + L + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_M()		// ADD (M) with Carry
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
}
void op_code_ADC_A()		// ADD (A) with Carry
{
	temp_ACC_16 = registers[7] + registers[7] + Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) + (registers[7] & 15) + Flag_A_Carry;
	Flag_Carry = temp_ACC_16 >> 8;
	Flag_A_Carry = temp_ACC_8 >> 4;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = registers[7] >> 7;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tADD A + A + CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_ADC_I()		// ADD IMM with Carry
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
}

void op_code_SUB_B()		// SUB (B)
{
	temp_ACC_16 = registers[7] - registers[0];
	temp_ACC_8 = (registers[7] & 15) - (registers[0] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - B = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_C()		// SUB (C) 
{
	temp_ACC_16 = registers[7] - registers[1];
	temp_ACC_8 = (registers[7] & 15) - (registers[1] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - C = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_D()		// SUB (D) 
{
	temp_ACC_16 = registers[7] - registers[2];
	temp_ACC_8 = (registers[7] & 15) - (registers[2] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - C = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_E()		// SUB (E) 
{
	temp_ACC_16 = registers[7] - registers[3];
	temp_ACC_8 = (registers[7] & 15) - (registers[3] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - D = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_H()		// SUB (H) 
{
	temp_ACC_16 = registers[7] - registers[4];
	temp_ACC_8 = (registers[7] & 15) - (registers[4] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - H = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_L()		// SUB (L) 
{
	temp_ACC_16 = registers[7] - registers[5];
	temp_ACC_8 = (registers[7] & 15) - (registers[5] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - L = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_M()		// SUB (M) 
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
}
void op_code_SUB_A()		// SUB (A) 
{
	temp_ACC_16 = registers[7] - registers[7];
	temp_ACC_8 = (registers[7] & 15) - (registers[7] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - A = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SUB_I()		// SUB IMM 
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
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\tSUB A - IMM(" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
#endif			
	program_counter += 2;
}

void op_code_SBB_B()		// SUB (B)  with Carry
{
	temp_ACC_16 = registers[7] - registers[0] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[0] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - B - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_C()		// SUB (C)  with Carry
{
	temp_ACC_16 = registers[7] - registers[1] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[1] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - C - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_D()		// SUB (D)  with Carry
{
	temp_ACC_16 = registers[7] - registers[2] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[2] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - D - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_E()		// SUB (E)  with Carry
{
	temp_ACC_16 = registers[7] - registers[3] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[3] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - E - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_H()		// SUB (H)  with Carry
{
	temp_ACC_16 = registers[7] - registers[4] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[4] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - H - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_L()		// SUB (L)  with Carry
{
	temp_ACC_16 = registers[7] - registers[5] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[5] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A -L - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_M()		// SUB (M)  with Carry
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
}
void op_code_SBB_A()		// SUB (A)  with Carry
{
	temp_ACC_16 = registers[7] - registers[7] - Flag_Carry;
	temp_ACC_8 = (registers[7] & 15) - (registers[7] & 15) - Flag_A_Carry;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	registers[7] = temp_ACC_16 & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tSUB A - A - CY = " << registers[7] << endl;
#endif
	program_counter++;
}
void op_code_SBB_I()		// SUB IMM  with Carry
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
}

void op_code_INR_B()		// INR (B)
{
	Flag_A_Carry = ((registers[0] & 15) + 1) >> 4;
	registers[0] = (registers[0] + 1) & 255;
	if (registers[0]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[0] >> 7) & 1;
	Flag_Parity = ~registers[0] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC B = " << registers[0] << endl;
#endif
	program_counter++;
}
void op_code_INR_C()		// INR (C)
{
	Flag_A_Carry = ((registers[1] & 15) + 1) >> 4;
	registers[1] = (registers[1] + 1) & 255;
	if (registers[1]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[1] >> 7) & 1;
	Flag_Parity = ~registers[1] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC C = " << registers[1] << endl;
#endif
	program_counter++;
}
void op_code_INR_D()		// INR (D)
{
	Flag_A_Carry = ((registers[2] & 15) + 1) >> 4;
	registers[2] = (registers[2] + 1) & 255;
	if (registers[2]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[2] >> 7) & 1;
	Flag_Parity = ~registers[2] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC D = " << registers[2] << endl;
#endif
	program_counter++;
}
void op_code_INR_E()		// INR (E)
{
	Flag_A_Carry = ((registers[3] & 15) + 1) >> 4;
	registers[3] = (registers[3] + 1) & 255;
	if (registers[3]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[3] >> 7) & 1;
	Flag_Parity = ~registers[3] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC E = " << registers[3] << endl;
#endif
	program_counter++;
}
void op_code_INR_H()		// INR (H)
{
	Flag_A_Carry = ((registers[4] & 15) + 1) >> 4;
	registers[4] = (registers[4] + 1) & 255;
	if (registers[4]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[4] >> 7) & 1;
	Flag_Parity = ~registers[4] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC H = " << registers[4] << endl;
#endif
	program_counter++;
}
void op_code_INR_L()		// INR (L)
{
	Flag_A_Carry = ((registers[5] & 15) + 1) >> 4;
	registers[5] = (registers[5] + 1) & 255;
	if (registers[5]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[5] >> 7) & 1;
	Flag_Parity = ~registers[5] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC L = " << registers[5] << endl;
#endif
	program_counter++;
}
void op_code_INR_M()		// INR (M)
{
	temp_Addr = registers[4] * 256 + registers[5];
	Flag_A_Carry = (memory.read(temp_Addr) & 15 + 1) >> 4;
	temp_ACC_16 = (memory.read(temp_Addr) + 1) & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~temp_ACC_16 & 1;
	memory.write(temp_Addr, (temp_ACC_16 & 255));
#ifdef DEBUG
	if (log_to_console) {
		cout << "\t\tINC Mem at [";
		SetConsoleTextAttribute(hConsole, 10);
		cout << (int)temp_Addr;
		SetConsoleTextAttribute(hConsole, 7); cout << "] = " << (int)temp_ACC_16 << endl;
	}
#endif
	program_counter++;
}
void op_code_INR_A()		// INR (A)
{
	Flag_A_Carry = ((registers[7] & 15) + 1) >> 4;
	registers[7] = (registers[7] + 1) & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC A = " << registers[7] << endl;
#endif
	program_counter++;
}

void op_code_DCR_B()		// DCR (B)
{
	temp_ACC_8 = (registers[0] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[0]; //старое значение
	registers[0]--;
	registers[0] = registers[0] & 255;
	if (registers[0]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[0] >> 7) & 1;
	Flag_Parity = ~registers[0] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC B(" << (int)temp_ACC_8 << ") = " << (int)registers[0] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_C()		// DCR (C)
{
	temp_ACC_8 = (registers[1] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[1]; //старое значение
	registers[1]--;
	registers[1] = registers[1] & 255;
	if (registers[1]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[1] >> 7) & 1;
	Flag_Parity = ~registers[1] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC C(" << (int)temp_ACC_8 << ") = " << (int)registers[1] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_D()		// DCR (D)
{
	temp_ACC_8 = (registers[2] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[2]; //старое значение
	registers[2]--;
	registers[2] = registers[2] & 255;
	if (registers[2]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[2] >> 7) & 1;
	Flag_Parity = ~registers[2] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC D(" << (int)temp_ACC_8 << ") = " << (int)registers[2] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_E()		// DCR (E)
{
	temp_ACC_8 = (registers[3] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[3]; //старое значение
	registers[3]--;
	registers[3] = registers[3] & 255;
	if (registers[3]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[3] >> 7) & 1;
	Flag_Parity = ~registers[3] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC E(" << (int)temp_ACC_8 << ") = " << (int)registers[3] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_H()		// DCR (H)
{
	temp_ACC_8 = (registers[4] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[4]; //старое значение
	registers[4]--;
	registers[4] = registers[4] & 255;
	if (registers[4]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[4] >> 7) & 1;
	Flag_Parity = ~registers[4] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC H(" << (int)temp_ACC_8 << ") = " << (int)registers[4] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_L()		// DCR (L)
{
	temp_ACC_8 = (registers[5] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[5]; //старое значение
	registers[5]--;
	registers[5] = registers[5] & 255;
	if (registers[5]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[5] >> 7) & 1;
	Flag_Parity = ~registers[5] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC L(" << (int)temp_ACC_8 << ") = " << (int)registers[5] << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_M()		// DCR (M)
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
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDEC Mem(" << temp_ACC_8 << ") at " << (int)temp_Addr << " = " << (int)(temp_ACC_16 & 255) << " " << flags << endl;
#endif
	program_counter++;
}
void op_code_DCR_A()		// DCR (A)
{
	temp_ACC_8 = (registers[7] & 15) - 1;
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	temp_ACC_8 = registers[7]; //старое значение
	registers[7]--;
	registers[7] = registers[7] & 255;
	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG	
	string flags = "[Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << "\t\tDEC A(" << (int)temp_ACC_8 << ") = " << (int)registers[7] << " " << flags << endl;
#endif
	program_counter++;
}

void op_code_INX_BC()		// INX (BC)
{
	//увеличиваем ВС на 1
	temp_ACC_16 = registers[0];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[1] + 1;
	registers[1] = temp_ACC_16 & 255;
	registers[0] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC BC = " << (int)temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_INX_DE()		// INX (DE)
{
	//увеличиваем DE на 1
	temp_ACC_16 = registers[2];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[3] + 1;
	registers[3] = temp_ACC_16 & 255;
	registers[2] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC DE = " << (int)temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_INX_HL()		// INX (HL)
{
	//увеличиваем HL на 1
	temp_ACC_16 = registers[4];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[5] + 1;
	registers[5] = temp_ACC_16 & 255;
	registers[4] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC HL = " << (int)temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_INX_SP()		// INX (SP)
{
	//увеличиваем SP на 1
	stack_pointer++;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tINC SP = " << (int)stack_pointer << endl;
#endif
	program_counter += 1;
}

void op_code_DCX_BC()		// DCX (BC)
{
	//уменьшаем ВС на 1
	temp_ACC_16 = registers[0];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[1] - 1;
	registers[1] = temp_ACC_16 & 255;
	registers[0] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDEC BC = " << temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_DCX_DE()		// DCX (DE)
{
	//уменьшаем DE на 1
	temp_ACC_16 = registers[2];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[3] - 1;
	registers[3] = temp_ACC_16 & 255;
	registers[2] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDEC DE = " << temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_DCX_HL()		// DCX (HL)
{
	//уменьшаем HL на 1
	temp_ACC_16 = registers[4];
	temp_ACC_16 = (temp_ACC_16 << 8) + registers[5] - 1;
	registers[5] = temp_ACC_16 & 255;
	registers[4] = temp_ACC_16 >> 8;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDEC HL = " << temp_ACC_16 << endl;
#endif
	program_counter += 1;
}
void op_code_DCX_SP()		// DCX (SP)
{
	//уменьшаем SP на 1
	stack_pointer--;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDEC SP = " << stack_pointer << endl;
#endif
	program_counter += 1;
}

void op_code_DAD_BC()  // DAD (Add BC to HL)
{
	//HL + ВС = HL 
	unsigned int new_reg = registers[0] * 256 + registers[1] + registers[4] * 256 + registers[5];
	Flag_Carry = (new_reg >> 16) & 1;
	registers[4] = (new_reg >> 8) & 255;
	registers[5] = new_reg & 255;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tHL + BC = " << registers[5] + registers[4] * 256 << endl;
#endif
	program_counter += 1;
	return;
}
void op_code_DAD_DE() // DAD (Add DE to HL)
{
	//HL + DE = HL 
	unsigned int new_reg = registers[2] * 256 + registers[3] + registers[4] * 256 + registers[5];
	Flag_Carry = (new_reg >> 16) & 1;
	registers[4] = (new_reg >> 8) & 255;
	registers[5] = new_reg & 255;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tHL + DE = " << registers[5] + registers[4] * 256 << endl;
#endif
	program_counter += 1;
	return;
}
void op_code_DAD_HL()// DAD (Add HL to HL)
{
	//HL + HL = HL 
	unsigned int new_reg = registers[4] * 256 + registers[5] + registers[4] * 256 + registers[5];
	Flag_Carry = (new_reg >> 16) & 1;
	registers[4] = (new_reg >> 8) & 255;
	registers[5] = new_reg & 255;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tHL + HL = " << registers[5] + registers[4] * 256 << endl;
#endif
	program_counter += 1;
	return;
}
void op_code_DAD_SP()// DAD (Add SP to HL)
{
	//HL + SP = HL 
	unsigned int new_reg = stack_pointer + registers[4] * 256 + registers[5];
	Flag_Carry = (new_reg >> 16) & 1;
	registers[4] = (new_reg >> 8) & 255;
	registers[5] = new_reg & 255;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tHL + SP = " << registers[5] + registers[4] * 256 << endl;
#endif
	program_counter += 1;
	return;
}

void op_code_DAA()			// DAA
{
	temp_ACC_8 = registers[7] & 15;
	temp_ACC_16 = registers[7];
	if (temp_ACC_8 > 9 || Flag_A_Carry)
	{
		temp_ACC_16 = temp_ACC_16 + 6;
		Flag_A_Carry = true;
	}
	temp_ACC_8 = (temp_ACC_16 >> 4) & 31; //старшие 4 бита

	if (temp_ACC_8 > 9 || Flag_Carry)
	{
		temp_ACC_8 += 6; // +6 к старшим битам
		if (temp_ACC_8 > 15) Flag_Carry = true;
		temp_ACC_8 = temp_ACC_8 & 15;
	}
	registers[7] = (temp_ACC_16 & 15) | (temp_ACC_8 << 4);

	if (registers[7]) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = (~registers[7]) & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tDAA (" << (int)(registers[7] >> 4) << ")(" << (int)(registers[7] & 15) << ") CY= " << Flag_Carry << " CA= " << Flag_A_Carry << endl;
#endif
	program_counter += 1;
}

void op_code_AND_B()		// AND (B)
{
	temp_ACC_16 = (registers[7] & registers[0]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND B = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_C()		// AND (C)
{
	temp_ACC_16 = (registers[7] & registers[1]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND C = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_D()		// AND (D)
{
	temp_ACC_16 = (registers[7] & registers[2]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND D = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_E()		// AND (E)
{
	temp_ACC_16 = (registers[7] & registers[3]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND E = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_H()		// AND (H)
{
	temp_ACC_16 = (registers[7] & registers[4]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND H = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_L()		// AND (L)
{
	temp_ACC_16 = (registers[7] & registers[5]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND L = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_M()		// AND (M)
{
	temp_ACC_16 = (registers[7] & memory.read(registers[4] * 256 + registers[5])) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tACC AND M at [";
		SetConsoleTextAttribute(hConsole, 10);
		cout << (int)registers[4] * 256 + registers[5];
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] = " << registers[7] << endl;
	}
#endif
	program_counter += 1;
}
void op_code_AND_A()		// AND (A)
{
	temp_ACC_16 = (registers[7] & registers[7]) & 255;
	Flag_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND A = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_AND_IMM()  //AND immediate
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1);
#endif
	temp_ACC_16 = (registers[7] & memory.read(program_counter + 1)) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC AND IMM(" << (int)memory.read(program_counter + 1) << ") = " << registers[7] << endl;
#endif
	program_counter += 2;
}
void op_code_XOR_B()		// XOR (B)
{
	temp_ACC_16 = (registers[7] ^ registers[0]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR B = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_C()		// XOR (C)
{
	temp_ACC_16 = (registers[7] ^ registers[1]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR C = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_D()		// XOR (D)
{
	temp_ACC_16 = (registers[7] ^ registers[2]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR D = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_E()		// XOR (E)
{
	temp_ACC_16 = (registers[7] ^ registers[3]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR E = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_H()		// XOR (H)
{
	temp_ACC_16 = (registers[7] ^ registers[4]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR H = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_L()		// XOR (L)
{
	temp_ACC_16 = (registers[7] ^ registers[5]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR L = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_M()		// XOR (M)
{
	temp_ACC_16 = (registers[7] ^ memory.read(registers[4] * 256 + registers[5])) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tACC XOR M at [";
		SetConsoleTextAttribute(hConsole, 10);
		cout << (int)registers[4] * 256 + registers[5];
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] = " << registers[7] << endl;
	}
#endif
	program_counter += 1;
}
void op_code_XOR_A()		// XOR (A)
{
	temp_ACC_16 = (registers[7] ^ registers[7]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC XOR A = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_XOR_IMM()  //XOR immediate
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1);
#endif
	temp_ACC_16 = (registers[7] ^ memory.read(program_counter + 1)) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\tXOR IMM (" << (bitset<8>)memory.read(program_counter + 1) << ") = " << (bitset<8>)registers[7] << endl;
#endif
	program_counter += 2;
}
void op_code_OR_B()		// OR (B)
{
	temp_ACC_16 = (registers[7] | registers[0]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR B = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_C()		// OR (C)
{
	temp_ACC_16 = (registers[7] | registers[1]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR C = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_D()		// OR (D)
{
	temp_ACC_16 = (registers[7] | registers[2]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR D = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_E()		// OR (E)
{
	temp_ACC_16 = (registers[7] | registers[3]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR E = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_H()		// OR (H)
{
	temp_ACC_16 = (registers[7] | registers[4]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR H = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_L()		// OR (L)
{
	temp_ACC_16 = (registers[7] | registers[5]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR L = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_M()		// OR (M)
{
	temp_ACC_16 = (registers[7] | memory.read(registers[4] * 256 + registers[5])) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) {
		cout << "\tACC OR M at [";
		SetConsoleTextAttribute(hConsole, 10);
		cout << (int)registers[4] * 256 + registers[5];
		SetConsoleTextAttribute(hConsole, 7);
		cout << "] = " << registers[7] << endl;
	}
#endif
	program_counter += 1;
}
void op_code_OR_A()		// OR (A)
{
	temp_ACC_16 = (registers[7] | registers[7]) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC OR A = " << registers[7] << endl;
#endif
	program_counter += 1;
}
void op_code_OR_IMM()  //OR immediate
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1);
#endif
	temp_ACC_16 = (registers[7] | memory.read(program_counter + 1)) & 255;
	Flag_Carry = false;
	Flag_A_Carry = false;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = (~temp_ACC_16) & 1;
	registers[7] = temp_ACC_16;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tOR IMM (" << (bitset<8>)memory.read(program_counter + 1) << ") = " << (bitset<8>)registers[7] << endl;
#endif
	program_counter += 2;
}

void op_code_CMP_B()		// CMP (B)
{
	temp_ACC_16 = registers[7] - registers[0];
	temp_ACC_8 = (registers[7] & 15) - (registers[0] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with B(" << registers[0] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_C()		// CMP (C)
{
	temp_ACC_16 = registers[7] - registers[1];
	temp_ACC_8 = (registers[7] & 15) - (registers[1] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with C(" << registers[1] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_D()		// CMP (D)
{
	temp_ACC_16 = registers[7] - registers[2];
	temp_ACC_8 = (registers[7] & 15) - (registers[2] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with D(" << registers[2] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_E()		// CMP (E)
{
	temp_ACC_16 = registers[7] - registers[3];
	temp_ACC_8 = (registers[7] & 15) - (registers[3] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with E(" << registers[3] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_H()		// CMP (H)
{
	temp_ACC_16 = registers[7] - registers[4];
	temp_ACC_8 = (registers[7] & 15) - (registers[4] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with H(" << registers[4] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_L()		// CMP (L)
{
	temp_ACC_16 = registers[7] - registers[5];
	temp_ACC_8 = (registers[7] & 15) - (registers[5] & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with L(" << registers[5] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_M()		// CMP (M)
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
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC Comp with M at " << (int)(registers[4] * 256 + registers[5]) << " Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_A()		// CMP (A)
{
	Flag_A_Carry = false;
	Flag_Carry = false;
	Flag_Zero = true;
	Flag_Sign = (registers[7] >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tACC(" << (int)registers[7] << ") Comp with A(" << registers[7] << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_CMP_IMM()		// CMP Imm
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t";
#endif
	temp_ACC_16 = registers[7] - memory.read(program_counter + 1);
	temp_ACC_8 = (registers[7] & 15) - (memory.read(program_counter + 1) & 15);
	Flag_A_Carry = (temp_ACC_8 >> 4) & 1;
	Flag_Carry = (temp_ACC_16 >> 8) & 1;
	temp_ACC_16 = temp_ACC_16 & 255;
	if (temp_ACC_16) Flag_Zero = false;
	else Flag_Zero = true;
	Flag_Sign = (temp_ACC_16 >> 7) & 1;
	Flag_Parity = ~registers[7] & 1;
#ifdef DEBUG
	if (log_to_console) cout << "\tACC(" << registers[7] << ") Comp with IMM(" << (int)memory.read(program_counter + 1) << ") Z = " << Flag_Zero << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 2;
}

void op_code_RLC()			// RLC
{
	Flag_Carry = (registers[7] >> 7) & 1;
	registers[7] = (registers[7] << 1) & 255;
	registers[7] = registers[7] | Flag_Carry;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tRotate left ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_RRC()			// RRC
{
	Flag_Carry = registers[7] & 1;
	registers[7] = registers[7] >> 1;
	registers[7] = registers[7] | 128 * Flag_Carry;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tRotate right ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
#endif
	program_counter += 1;
}
void op_code_RAL()			// RAL
	{
		bool tmp = (registers[7] >> 7) & 1; //старший бит
		registers[7] = (registers[7] << 1) & 255;
		registers[7] = registers[7] | Flag_Carry;
		Flag_Carry = tmp;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tRotate left through CY ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
#endif
		program_counter += 1;
	}
void op_code_RAR()			// RAR
	{
		bool tmp = registers[7] & 1;
		registers[7] = registers[7] >> 1;
		registers[7] = registers[7] + Flag_Carry * 128;
		Flag_Carry = tmp;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tRotate right through CY ACC = " << (bitset<8>)registers[7] << " CY = " << Flag_Carry << endl;
#endif
		program_counter += 1;
	}
void op_code_CMA()			// CMA
	{
#ifdef DEBUG
		if (log_to_console) cout << "\t\tInvert ACC(" << (bitset<8>)registers[7] << ")";
#endif
		registers[7] = (~registers[7]) & 255;
#ifdef DEBUG
		if (log_to_console) cout << " = " << (int)(registers[7]) << endl;
#endif
		program_counter += 1;
	}
void op_code_CMC()			// CMC
	{
		Flag_Carry = !Flag_Carry;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tInvert CY = " << Flag_Carry << endl;
#endif
		program_counter += 1;
	}
void op_code_STC()			// STC
	{
		Flag_Carry = true;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tset CY = " << Flag_Carry << endl;
#endif
		program_counter += 1;
	}

void op_code_JMP()	//Jump unconditional
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif
	program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
	if (log_to_console) cout << "jump to " << (int)program_counter << endl;
#endif
}
void op_code_JMP_NZ()		//Cond Jump Not Zero
{
#ifdef DEBUG
	string flags = "[NOT Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (!Flag_Zero)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_Z()		//Cond Jump Zero
{
#ifdef DEBUG
	string flags = "[Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (Flag_Zero)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_NC()		//Cond Jump No Carry
{
#ifdef DEBUG
	string flags = "[NO Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (!Flag_Carry)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_C()		//Cond Jump Carry
{
#ifdef DEBUG
	string flags = "[Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (Flag_Carry)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_NP()		//Cond Jump No Parity
{
#ifdef DEBUG
	string flags = "[NO Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (!Flag_Parity)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_P()		//Cond Jump Parity
{
#ifdef DEBUG
	string flags = "[Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (Flag_Parity)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_Plus()		//Cond Jump Plus
{
#ifdef DEBUG
	string flags = "[Plus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (!Flag_Sign)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_JMP_Minus()	//Cond Jump Minus
{
#ifdef DEBUG
	string flags = "[Minus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + "CA=" + to_string(Flag_A_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif			
	if (Flag_Sign)
	{
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
		if (log_to_console) cout << "jump to " << program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "Jump cond not met " << flags << endl;
#endif
	program_counter += 3;
	return;
}
void op_code_CALL()	//Call
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
#endif
	memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
	memory.write(stack_pointer - 2, (program_counter + 3) & 255);
	stack_pointer -= 2;
	program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG
	if (log_to_console) cout << "CALL -> " << (int)program_counter << " (return to " << int(memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256) << ")" << endl;
#endif
}
void op_code_CALL_NZ()		//Cond Call Not Zero
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[NOT Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Zero)  //call if NOT zero
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_Z()		//Cond Call Zero
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Zero)  //call if NOT zero
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_NC()		//Cond Call No Carry
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[NO Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Carry)  //call if NO Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_C()		//Cond Call Carry
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Carry)  //call if Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_NP()		//Cond Call No Parity
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[No Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Parity)  //call if Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_P()		//Cond Call Parity
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Parity)  //call if Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_Plus()	//Cond Call Plus
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[Plus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Sign)  //call if Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_CALL_Minus()	//Cond Call Minus
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t" << (int)memory.read(program_counter + 2) << "\t";
	string flags = "[Minus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Sign)  //call if Carry
	{
		memory.write(stack_pointer - 1, (program_counter + 3) >> 8);
		memory.write(stack_pointer - 2, (program_counter + 3) & 255);
		stack_pointer -= 2;
		program_counter = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
#ifdef DEBUG	
		if (log_to_console) cout << "cond CALL " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG		
	if (log_to_console) cout << "cond Call [deny]" << flags << endl;
#endif
	program_counter += 3;
}
void op_code_RET()	//Return
{
	program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
	stack_pointer += 2;
#ifdef DEBUG
	SetConsoleTextAttribute(hConsole, 11);
	if (log_to_console) cout << "\t\tRET to " << (int)program_counter << endl;
	SetConsoleTextAttribute(hConsole, 7);
#endif
}
void op_code_RET_NZ()	//Cond RET Not Zero
{
#ifdef DEBUG			
	string flags = "[NOT Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Zero)  //RET if NOT zero
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_Z()	//Cond RET Zero
{
#ifdef DEBUG			
	string flags = "[Zero][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Zero)  //RET if Zero
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_NC()	//Cond RET No Carry
{
#ifdef DEBUG			
	string flags = "[No Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Carry)  //RET if No Carry
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_C()	//Cond RET Carry
{
#ifdef DEBUG			
	string flags = "[Carry][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Carry)  //RET if Carry
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_NP()	//Cond RET No Parity
{
#ifdef DEBUG			
	string flags = "[No Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Parity)  //RET if No Parity
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_P()	//Cond RET Parity
{
#ifdef DEBUG			
	string flags = "[Parity][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Parity)  //RET if Parity
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_Plus()		//Cond RET Plus
{
#ifdef DEBUG			
	string flags = "[Plus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (!Flag_Sign)  //RET if Plus
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RET_Minus()	//Cond RET Minus
{
#ifdef DEBUG			
	string flags = "[Minus][Z=" + to_string(Flag_Zero) + " CY=" + to_string(Flag_Carry) + " S=" + to_string(Flag_Sign) + " P=" + to_string(Flag_Parity) + "]";
#endif
	if (Flag_Sign)  //RET if Plus
	{
		program_counter = memory.read(stack_pointer) + memory.read(stack_pointer + 1) * 256;
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tcond RET to " << (int)program_counter << flags << endl;
#endif
		return;
	}
#ifdef DEBUG
	if (log_to_console) cout << "\t\tcond RET[deny]" << flags << endl;
#endif
	program_counter += 1;
}
void op_code_RSTn() //RSTn
{
	int rstN = (memory.read(program_counter) >> 3) & 7;
	memory.write(stack_pointer - 1, (program_counter + 1) >> 8);
	memory.write(stack_pointer - 2, (program_counter + 1) & 255);
	stack_pointer -= 2;
	program_counter = rstN << 3;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tRST" << dec << (int)rstN << hex << " jump to " << (int)program_counter << endl;
#endif
}

void op_code_PCHL()	//RCHL Jump to [HL]
{
		program_counter = registers[4] * 256 + registers[5];
#ifdef DEBUG
		if (log_to_console) cout << "\t\tJump to [HL] " << program_counter << endl;
#endif
}

void op_code_EI() //EI (Enable Interrupts)
{
	//op_code = 251 
	Interrupts_enabled = true;
#ifdef DEBUG
	if (log_to_console) cout << "interrupts ON" << endl;
#endif
	speaker.beep_on();
	program_counter++;
}
void op_code_DI() //DI (Disable Interrupts)
{
	//op_code = 243
	Interrupts_enabled = false;
#ifdef DEBUG
	if (log_to_console) cout << "interrupts OFF" << endl;
#endif
	speaker.beep_off();
	program_counter++;
}
void op_code_PUSH_BC()  //PUSH pair (BC)
{
	//загружаем ВС в стек
	memory.write(stack_pointer - 1, registers[0]);
	memory.write(stack_pointer - 2, registers[1]);
	stack_pointer -= 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPUSH BC(" << registers[1] + registers[0] * 256 << ")" << endl;
#endif
	program_counter += 1;
}
void op_code_PUSH_DE() //PUSH pair (DE)
{
	//загружаем DE в стек
	memory.write(stack_pointer - 1, registers[2]);
	memory.write(stack_pointer - 2, registers[3]);
	stack_pointer -= 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPUSH DE(" << registers[3] + registers[2] * 256 << ")" << endl;
#endif
	program_counter += 1;
}
void op_code_PUSH_HL() //PUSH pair (HL)
{
	//загружаем HL в стек
	memory.write(stack_pointer - 1, registers[4]);
	memory.write(stack_pointer - 2, registers[5]);
	stack_pointer -= 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPUSH HL(" << registers[5] + registers[4] * 256 << ")" << endl;
#endif
	program_counter += 1;
}
void op_code_POP_BC() //POP pair (BC)
{
	//загружаем ВС из стека
	registers[0] = memory.read(stack_pointer + 1);
	registers[1] = memory.read(stack_pointer);
	stack_pointer += 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPOP BC(" << registers[1] + registers[0] * 256 << ")" << endl;
#endif
	program_counter += 1;
}
void op_code_POP_DE() //POP pair (DE)
{
		//загружаем DE из стека
		registers[2] = memory.read(stack_pointer + 1);
		registers[3] = memory.read(stack_pointer);
		stack_pointer += 2;
#ifdef DEBUG
		if (log_to_console) cout << "\t\tPOP DE(" << registers[3] + registers[2] * 256 << ")" << endl;
#endif
		program_counter += 1;
		}
void op_code_POP_HL() //POP pair (HL)
{
	//загружаем HL из стека
	registers[4] = memory.read(stack_pointer + 1);
	registers[5] = memory.read(stack_pointer);
	stack_pointer += 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPOP HL(" << registers[5] + registers[4] * 256 << ")" << endl;
#endif
	program_counter += 1;
}
void op_code_PUSH_PSW() //PUSH PSW
{
	memory.write(stack_pointer - 1, registers[7]);
	memory.write(stack_pointer - 2, (Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry));
	stack_pointer -= 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPUSH PSW  " << (bitset<8>)(Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry) << endl;
#endif
	program_counter += 1;
}
void op_code_POP_PSW() //POP PSW
{
	registers[7] = (memory.read(stack_pointer + 1) & 255);
	Flag_Sign = (memory.read(stack_pointer) >> 7) & 1;
	Flag_Zero = (memory.read(stack_pointer) >> 6) & 1;
	Flag_A_Carry = (memory.read(stack_pointer) >> 4) & 1;
	Flag_Parity = (memory.read(stack_pointer) >> 2) & 1;
	Flag_Carry = memory.read(stack_pointer) & 1;
	stack_pointer += 2;
#ifdef DEBUG
	if (log_to_console) cout << "\t\tPOP PSW " << (bitset<8>)(Flag_Sign << 7 | Flag_Zero << 6 | Flag_A_Carry << 4 | Flag_Parity << 2 | 2 | Flag_Carry) << endl;
#endif
	program_counter += 1;
}
void op_code_XTHL()  //XTHL (Exchange stack top with H and L)
{
	unsigned __int16 temp_H = registers[4];
	unsigned __int16 temp_L = registers[5];
	registers[5] = memory.read(stack_pointer);
	registers[4] = memory.read(stack_pointer + 1);
	memory.write(stack_pointer, temp_L);
	memory.write(stack_pointer + 1, temp_H);
#ifdef DEBUG
	if (log_to_console) cout << "\t\tXTHL" << endl;
#endif
	program_counter += 1;
}
void op_code_SPHL() //SPHL (MoveHLtoSP) 
{
	stack_pointer = registers[4] * 256 + registers[5];
#ifdef DEBUG
	if (log_to_console) cout << "\t\tLoad HL to SP" << endl;
#endif
	program_counter += 1;
}
void op_code_IN_Port() //Input from port
{
#ifdef DEBUG
	if (log_to_console) cout << "\t\tInput from port" << endl;
#endif
	program_counter++;
}
void op_code_OUT_Port() //OUT port
{
#ifdef DEBUG
	if (log_to_console) cout << "\t\tOutput to port" << endl;
#endif
	program_counter++;
}
void op_code_HLT() // HLT (Halt)
{
#ifdef DEBUG
	if (log_to_console) cout << (int)memory.read(program_counter + 1) << "\t\t" << "HALT" << endl;
#endif
	cont_exec = false;
}



