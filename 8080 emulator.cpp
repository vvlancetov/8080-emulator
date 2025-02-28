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

using namespace std;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
__int8 global_error = 0; //глобальный код ошибки

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

struct comment
{
    int address;
    string text;
};

class Video_device
{
    private:
        int ink_color = 0;    // 0 - black, 1 - red
        int curr_sector = 0;  //текущий сектор на барабане
        vector<string> paper; // виртуальная "бумага" для вывода информации
        unsigned int shifter; // сдвиговый регистр на 20 ячеек
        unsigned int sync_counter; //счетчик синхроимпульсов
        vector<string> drum; // виртуальный барабан принтера

    public:
        Video_device()   // конструктор класса
            {
              shifter = 0;
              sync_counter = 0;
              
              // вставка "бумаги" в массив строк
              paper.push_back("                  "); 

              //инициализация барабана
              drum.push_back("000000000000000 d#");
              drum.push_back("111111111111111 +*");
              drum.push_back("222222222222222 -|");
              drum.push_back("333333333333333 x|");
              drum.push_back("444444444444444 /|");
              drum.push_back("555555555555555 MM");
              drum.push_back("666666666666666 MM");
              drum.push_back("777777777777777 ^T");
              drum.push_back("888888888888888 =K");
              drum.push_back("999999999999999 vE");
              drum.push_back("............... %E");
              drum.push_back("............... cC");
              drum.push_back("_______________ RM");
            }; 

        void sync();             //импульс синхронизации
};

//создаем монитор
Video_device monitor;

class KBD //класс клавиатуры
{
    private:
        unsigned int shifter = 0; // сдвиговый регистр на 10 ячеек
        bool key_pressed = false;     // индикатор нажатия клавиши
        int shifter_pattern = 0; // маска для сравнения
        int data_for_port = 0;   // выходной поток байтов

    public:
        KBD()  // конструктор класса
            {
            shifter = 0;
            };

        int get_data();          //отправка данных процессору через порт ROM 1
};

// создаем клавиатуру
KBD keyboard;

vector<comment> comments; //комментарии к программе
string filename = "memtest32.txt"; //имя входного файла

//регистры процессора
unsigned __int8 Flags = 0; //Flags
unsigned __int16 registers[8] = { 0 };//внутренние регистры, включаяя аккумулятор
                     // 111(7) - A
                     // 000(0) - B
                     // 001(1) - C
                     // 010(2) - D
                     // 011(3) - E
                     // 100(4) - H
                     // 101(5) - L
                     // 110(6) - признак другой операции

string regnames[8] = {"B", "C", "D", "E" ,"H" ,"L" ,"-" ,"A"};
string pairnames[4] = { "BC", "DE", "HL", "SP"};
                     
//vector<ram_register> ram;// RAM
//vector<int>rom_ports; //порты ROM
//vector<int>ram_ports; //порты RAM

unsigned __int16 program_counter = 0; //счетчик команд
unsigned __int16 stack_pointer = 0; //счетчик команд

// флаги для изменения работы эмулятора
bool step_mode = true; //ждать ли нажатия пробела для выполнения команд
bool go_forward; //переменная для выхода из цикла обработки нажатий
bool RU_lang = true; //локализация
bool list_at_start = true; //вывод листинга на старте
bool log_to_console = true; //логирование команд на консоль
bool short_print = false; //сокращенный набор регистров для вывода

void print_all();

void disassemble(unsigned __int16 start, unsigned __int16 end);

int main(int argc, char* argv[]) {

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
                filename = argv[i + 1];// "Prog.txt";
                cout << "new filename = " << filename << endl;
            }
            else
            {
                filename = "Prog.txt";
                cout << "filename = " << filename << " (default)"  << endl;
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

    ifstream file(filename);
    if (!file.is_open()) {
        if (RU_lang)
        {
            cout << "Файл ";
            SetConsoleTextAttribute(hConsole, 4);
            cout << filename;
            SetConsoleTextAttribute(hConsole, 7);
            cout << " не найден!" << endl;
        }
        else
        {
            cout << "File " << filename << " not found!" << endl;
        }
        return 1;
    }

    string line;
    int number;
    string text;
    
    //считываем данные из файла
    int line_number = 0;
    while (getline(file, line)) {
        stringstream ss(line);
        if (ss >> number) { // Проверяем, есть ли число в начале строки
            memory.write(line_number, number); //пишем в память
            line_number++;
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
        cout << "Загружено " << line_number  << " команд(ы) из файла" << endl;
    }
    else
    {
        cout << "Loaded " << line_number << " commands from file" << endl;
    }

    //листинг
    if (list_at_start)
    {
        disassemble(0, 32); // Вывод листинга программы
    }

    if (RU_lang)
    {
        SetConsoleTextAttribute(hConsole, 10);
        cout << "Начинаем выполнение... нажмите любую клавишу" << endl << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }
    else
    {
        cout << endl << "Starting programm... ";
        SetConsoleTextAttribute(hConsole, 10);
        cout << "press a key" << endl << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    //ждем нажатия клавиши
    while (!_kbhit()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    //выводим памятку
    if (step_mode)
    {
        if (RU_lang)
        {
            cout << "Эмулятор работает в режиме пошагового выполнения команд" << endl;
            cout << "Для выполнения следующей команды нажимайте <ПРОБЕЛ>" << endl;
            cout << "Для отключения/включения пошагового режима нажмите <P> (английская)" << endl;
            cout << "Для вывода на экран содержимого регистров и памяти нажмите <TAB>" << endl;
        }
        else
        {
            cout << "Step by step mode ON" << endl;
            cout << "Press <SPACE> for next command" << endl;
            cout << "To turn ON/OFF step mode press <P>" << endl;
            cout << "To display the contents of registers and memory, press <TAB>" << endl;
        }
        cout << endl;
    }
    else {
        if (RU_lang) {
            cout << "Режим пошагового выполнения команд отключен" << endl;
            cout << "Для вывода на экран состояния ламп нажмите <TAB>" << endl;
        }
        else
        {
            cout << "Step by step mode OFF" << endl;
            cout << "To display lamp states, press <TAB>" << endl;
        }

    }

    //основной цикл программы
    while (1)
    {
        go_forward = false;
        monitor.sync(); //синхроимпульс для монитора

        //мониторинг нажатия клавиш в обычном режиме
        if (_kbhit())
        {
            int pressed_key = _getch();
            //проверяем нажатие кнопки P
            if (pressed_key == 112 || pressed_key == 167 || pressed_key == 80 || pressed_key == 135) step_mode = !step_mode;

            //выводим содержимое регистров если эмулятор работает в обычном режиме
            if (!step_mode && !log_to_console)
            {
                if (pressed_key == 9) //нажата TAB
                    print_all();
            }

        }

        //задержка вывода по нажатию кнопки в пошаговом режиме
        while (!go_forward && step_mode)
        {
            //засыпаем, чтобы не загружать процессор
            while (!_kbhit()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            int pressed_key = _getch();
            if (pressed_key == 0) pressed_key = _getch();
            //cout << pressed_key << endl;
            if (pressed_key == 32) go_forward = true;
            if (pressed_key == 9)
              {
                print_all();
              }
            if (pressed_key == 112 || pressed_key == 167 || pressed_key == 80 || pressed_key == 135) step_mode = !step_mode;
            if (!step_mode) go_forward = true;
        };
        
        //основной цикл 
        
        //выводим текущую команду
        if (log_to_console)
        {
            //ищем комментарий к команде и печатаем
            for (auto comm : comments)
            {
                if (comm.address == program_counter) cout << comm.text << endl;
            }
        }
        
        //декодер комманд
        
        // NOP
        if (memory.read(program_counter) == 0)
        {
            program_counter++;
            if(log_to_console) cout << "NOP" << endl;
            continue;
        }

        //==== Data Transfer Group ===========

       // Move Register 01-DDD-SSS or to/from memory
        if ((memory.read(program_counter) >> 6) == 1)
        {
            __int8 Dest = (memory.read(program_counter) >> 3) & 7;
            __int8 Src = memory.read(program_counter) & 7;
            if (Dest != 6 && Src != 6)
            {
                //копирование между регистрами
                registers[Dest] = registers[Src];
                if (log_to_console) cout << "Move " << regnames[Src] << " -> " << regnames[Dest] << "[" << registers[Dest]  << "]" << endl;
                program_counter++;
                continue;
            }
            else
            {
                if (Src == 6) //источник - память по адресу HL
                {
                    __int8 addr = memory.read(registers[4] * 256 + registers[5]); //адрес ячейки в HL
                    registers[Dest] = memory.read(addr);
                    if (log_to_console) cout << "Move [" << registers[Dest] << "] at address " << addr << " -> " << regnames[Dest] << endl;
                    program_counter++;
                    continue;
                }
                else
                {
                    if (Dest == 6) //адресат - память по адресу HL
                    {
                        __int8 addr = memory.read(registers[4] * 256 + registers[5]); //адрес ячейки в HL
                        memory.write(addr, registers[Src]);
                        if (log_to_console) cout << "Move [" << registers[Src] << "] at " << regnames[Src] << " -> address " << addr << endl;
                        program_counter++;
                        continue;
                    }
                }
            }
        }

        // Move immediate
        if ((memory.read(program_counter) & 199) == 6)
        {
            __int8 Dest = (memory.read(program_counter) >> 3) & 7;

            if (Dest != 6)
            {
                //загружаем непосредственные  данные из памяти в регистр
                registers[Dest] = memory.read(program_counter + 1);
                if (log_to_console) cout << "Move immediate [" << registers[Dest] << "] to " << regnames[Dest] << endl;
                program_counter +=2;
                continue;
            }
            else
            {
                //загружаем непосредственные данные из памяти в указанный адрес [HL]
                int addr = registers[4] * 256 + registers[5];
                memory.write(addr, memory.read(program_counter + 1));
                if (log_to_console) cout << "Move immediate [" << registers[Dest] << "] to address " << addr << endl;
                program_counter += 2;
                continue;
            }
        }

        // Load RP immediate
        if ((memory.read(program_counter) & 207) == 1)
        {
            __int8 Dest = (memory.read(program_counter) >> 4) & 3;

            if (Dest == 0)
            {
                //загружаем непосредственные данные в ВС
                registers[0] = memory.read(program_counter + 2);
                registers[1] = memory.read(program_counter + 1);
                if (log_to_console) cout << "Load immediate [" << registers[1] + registers[0] * 256 << "] to " << pairnames[0] << endl;
                program_counter += 3;
                continue;
            }
            if (Dest == 1)
            {
                //загружаем непосредственные данные в ВС
                registers[2] = memory.read(program_counter + 2);
                registers[3] = memory.read(program_counter + 1);
                if (log_to_console) cout << "Load immediate [" << registers[3] + registers[2] * 256 << "] to " << pairnames[1] << endl;
                program_counter += 3;
                continue;
            }
            if (Dest == 2)
            {
                //загружаем непосредственные данные в HL
                registers[4] = memory.read(program_counter + 2);
                registers[5] = memory.read(program_counter + 1);
                if (log_to_console) cout << "Load immediate [" << registers[5] + registers[4] * 256 << "] to " << pairnames[2] << endl;
                program_counter += 3;
                continue;
            }
            if (Dest == 3)
            {
                //загружаем непосредственные данные в SP
                stack_pointer = memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1);
                if (log_to_console) cout << "Load immediate [" << memory.read(program_counter + 2) * 256 + memory.read(program_counter + 1) << "] to SP" << endl;
                program_counter += 3;
                continue;
            }
        }

        //Load ACC direct (LDA)
        if (memory.read(program_counter) == 58)
        {
            __int8 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
            registers[7] = memory.read(addr);
            if (log_to_console) cout << "Load ACC from address " << addr << endl;
            program_counter += 3;
            continue;
        }

        //Store ACC direct (STA)
        if (memory.read(program_counter) == 50)
        {
            __int8 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
            memory.write(addr, registers[7]);
            if (log_to_console) cout << "Save ACC to address " << addr << endl;
            program_counter += 3;
            continue;
        }

        //Load HL direct (LHDL)
        if (memory.read(program_counter) == 42)
        {
            __int8 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
            registers[5] = memory.read(addr);
            registers[4] = memory.read(addr + 1);
            if (log_to_console) cout << "Load HL from addresses " << addr << ", " << addr + 1 << endl;
            program_counter += 3;
            continue;
        }

        //Store HL direct (SHLD)
        if (memory.read(program_counter) == 34)
        {
            __int8 addr = memory.read(program_counter + 1) + memory.read(program_counter + 2) * 256;
            memory.write(addr, registers[5]);
            memory.write(addr + 1, registers[4]);
            if (log_to_console) cout << "Save HL to address " << addr << ", " << addr + 1 << endl;
            program_counter += 3;
            continue;
        }

        //Load ACC indirect (LDAX)
        if ((memory.read(program_counter) & 207 ) == 10)
        {
            __int8 pair = (memory.read(program_counter) >> 4) & 3;
            if (pair == 0)
            {
                //пара BC
                __int8 addr = registers[0] * 256 + registers[1];
                registers[7] = memory.read(addr);
                if (log_to_console) cout << "Load ACC from address " << addr << "(BC)" << endl;
                program_counter ++;
                continue;
            }
            if (pair == 1)
            {
                //пара DE
                __int8 addr = registers[2] * 256 + registers[3];
                registers[7] = memory.read(addr);
                if (log_to_console) cout << "Load ACC from address " << addr << "(DE)" << endl;
                program_counter++;
                continue;
            }
        }

        //Store ACC indirect (STAX)
        if ((memory.read(program_counter) & 207) == 2)
        {
            __int8 pair = (memory.read(program_counter) >> 4) & 3;
            if (pair == 0)
            {
                //пара BC
                __int8 addr = registers[0] * 256 + registers[1];
                memory.write(addr, registers[7]);
                if (log_to_console) cout << "Save ACC to address " << addr << "(BC)" << endl;
                program_counter++;
                continue;
            }
            if (pair == 1)
            {
                //пара DE
                __int8 addr = registers[2] * 256 + registers[3];
                memory.write(addr, registers[7]);
                if (log_to_console) cout << "Save ACC to address " << addr << "(DE)" << endl;
                program_counter++;
                continue;
            }
        }

        //Exchange HL <-> DE
        if (memory.read(program_counter) == 235)
        {
            int tmp_D = registers[2];
            int tmp_E = registers[3];
            registers[2] = registers[4];
            registers[3] = registers[5];
            registers[4] = tmp_D;
            registers[5] = tmp_E;
            if (log_to_console) cout << "Exchange DE <-> HL" << endl;
            program_counter++;
            continue;
        }




        SetConsoleTextAttribute(hConsole, 4);
        cout << "Unknown command (" << (int)memory.read(program_counter) << ")! ";
        SetConsoleTextAttribute(hConsole, 7);
        cout << "Program counter = " << program_counter << endl;
        break;
    }

    return 0;
}

void print_all()
{
    //выводим значения всех регистров и памяти
    /*
    cout << "================================================================" << endl;
    cout << "ACC = " << ACC << " CarryFlag (CF) = " << CF << " TEST = " << TEST << "    ";
    if ((ram_ports.at(1) & 1) == 1) { SetConsoleTextAttribute(hConsole, 10); } else { SetConsoleTextAttribute(hConsole, 8); }
    cout << " MEMORY ";
    SetConsoleTextAttribute(hConsole, 7);
    if ((ram_ports.at(1) & 2) == 2) { SetConsoleTextAttribute(hConsole, 12); } else { SetConsoleTextAttribute(hConsole, 8); }
    cout << " OVERFLOW ";
    SetConsoleTextAttribute(hConsole, 7);
    if ((ram_ports.at(1) & 4) == 4) {SetConsoleTextAttribute(hConsole, 10); } else { SetConsoleTextAttribute(hConsole, 8); }
    cout << " MINUS ";
    SetConsoleTextAttribute(hConsole, 7);
    cout << endl;
    cout << "================================================================" << endl;
    
    if (!short_print) // выводим регистры если не включен short_print
    {
        
        for (int i = 0; i < 16; i += 2)
        {
            cout << "R" << i << "R" << i + 1 << "\t= ";
            if (registers.at(i) < 10) cout << " ";
            cout << registers.at(i) << " ";
            if (registers.at(i + 1) < 10) cout << " ";
            cout << registers.at(i + 1) << " total ";
            if (registers.at(i) * 16 + registers.at(i + 1) < 10) cout << " ";
            if (registers.at(i) * 16 + registers.at(i + 1) < 100) cout << " ";
            cout << registers.at(i) * 16 + registers.at(i + 1) << endl;
        }
        cout << "======================================================================================" << endl;
        cout << "REG|  3   2   1   0 | 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0 " << endl;
        cout << "======================================================================================" << endl;
        for (int i = 0; i < 8; i += 1)
        {
            cout << "R" << i << " | ";
            if (ram.at(i).stat_char.at(3) < 10) cout << " ";
            cout << ram.at(i).stat_char.at(3) << "  ";
            if (ram.at(i).stat_char.at(2) < 10) cout << " ";
            cout << ram.at(i).stat_char.at(2) << "  ";
            if (ram.at(i).stat_char.at(1) < 10) cout << " ";
            cout << ram.at(i).stat_char.at(1) << "  ";
            if (ram.at(i).stat_char.at(0) < 10) cout << " ";
            cout << ram.at(i).stat_char.at(0) << " | ";
            for (int j = 15; j >= 0; j--)
            {
                if (ram.at(i).cell.at(j) < 10) cout << " ";
                cout << ram.at(i).cell.at(j) << "  ";
            }
            cout << endl;
        }

        cout << "======================================================================================" << endl;
        cout << "Stack: " << stack.size() << endl;
        for (int i = 0; i < stack.size(); i++)
        {
            cout << i << " " << stack.at(i) << endl;
        }
        cout << "======================================================================================" << endl;
        cout << "Printer shifter: " << monitor.get_shifter() << endl;
        cout << "Keyboard shifter: " << keyboard.get_shifter() << endl;
        cout << "======================================================================================" << endl;
    }
    */
}

void disassemble(unsigned __int16 start, unsigned __int16 end)
{
    cout << endl;
    if (RU_lang) 
    {
        cout << "Включена опция листинга программы, адреса с " << start << " по " << end << endl;
        cout << "-------------------------------------------------" << endl;
        cout << " N стр" << endl;
        cout << "DEC/HEX" << "\t\t" << "Код" << "\t" << "Операция" << "\t" << endl;
        cout << "-------------------------------------------------" << endl;
    }
    else
    {
        cout << "Listing in ON, addresses from " << start << " to " << end << endl;
        cout << "-------------------------------------------------" << endl;
        cout << "Line N" << endl;
        cout << "DEC/HEX" << "\t\t" << "OPcode" << "\t" << "Operation" << "\t" << endl;
        cout << "-------------------------------------------------" << endl;
    }
    
    for (unsigned __int16 i = start; i < end; ++i)
    {
        
        if (i < 10) cout << " ";
        if (i < 100) cout << " ";
        cout << i << " " << hex;
        if (i < 16) cout << " ";
        if (i < 256) cout << " ";
        cout << i << dec << "\t\t" << (int)memory.read(i) << "\t";

        // NOP
        if (memory.read(i) == 0) cout << "NOP";

        //==== Data Transfer Group ===========

        // Move Register 01-DDD-SSS or to/from memory
        if ((memory.read(i) >> 6) == 1)
        {
            __int8 Dest = (memory.read(i) >> 3) & 7;
            __int8 Src = memory.read(i) & 7;
            if (Dest != 6 && Src != 6)
            {
                //копирование между регистрами
                cout << "Move " << regnames[Src] << " -> " << regnames[Dest];
            }
            else
            {
                if (Src == 6) //источник - память по адресу HL
                {
                    cout << "Move M[HL] -> " << regnames[Dest];
                }
                else
                {
                    if (Dest == 6) //адресат - память по адресу HL
                    {
                        cout << "Move " << regnames[Src] << " -> M[HL]";
                    }
                }
            }
        }

        // Move immediate
        if ((memory.read(i) & 199) == 6)
        {
            __int8 Dest = (memory.read(i) >> 3) & 7;

            if (Dest != 6)
            {
                //загружаем данные из памяти в регистр
                cout << "Move immediate [" << (int)memory.read(i + 1) << "] to " << regnames[Dest];
                
            }
            else
            {
                //загружаем данные из памяти в другой адрес [HL]
                cout << "Move immediate [" << (int)memory.read(i + 1) << "] to M[HL]";
            }
        }

        // Load RP immediate
        if ((memory.read(i) & 207) == 1)
        {
            __int8 Dest = (memory.read(i) >> 4) & 3;

            if (Dest == 0)
            {
                //загружаем непосредственные данные в ВС
                cout << "Load immediate [" << memory.read(i + 2) + memory.read(i + 1) * 256 << "] to " << pairnames[0];
                i += 2;
            }
            if (Dest == 1)
            {
                //загружаем непосредственные данные в ВС
                cout << "Load immediate [" << memory.read(i + 2) + memory.read(i + 1) * 256 << "] to " << pairnames[1];
                i += 2;
            }
            if (Dest == 2)
            {
                //загружаем непосредственные данные в HL
                cout << "Load immediate [" << memory.read(i + 2) + memory.read(i + 1) * 256 << "] to " << pairnames[2];
                i += 2;
            }
            if (Dest == 3)
            {
                //загружаем непосредственные данные в SP
                cout << "Load immediate [" << memory.read(i + 2) + memory.read(i + 1) * 256 << "] to SP";
                i += 2;
            }
        }


        //Load ACC direct (LDA)
        if (memory.read(i) == 58)
        {
            __int8 addr = memory.read(i + 1) + memory.read(i + 2) * 256;
            cout << "Load ACC from address " << addr;
            i += 2;
        }

        //Store ACC direct (STA)
        if (memory.read(i) == 50)
        {
            __int8 addr = memory.read(i + 1) + memory.read(i + 2) * 256;
            cout << "Save ACC to address " << addr;
            i += 2;
        }

        //Load HL direct (LHDL)
        if (memory.read(i) == 42)
        {
            __int8 addr = memory.read(i + 1) + memory.read(i + 2) * 256;
            cout << "Load HL from addresses " << addr << ", " << addr + 1;
            i += 2;
        }

        //Store HL direct (SHLD)
        if (memory.read(i) == 34)
        {
            __int8 addr = memory.read(i + 1) + memory.read(i + 2) * 256;
            cout << "Save HL to address " << addr << ", " << addr + 1;
            i += 2;
        }

        //Load ACC indirect (LDAX)
        if ((memory.read(i) & 207) == 10)
        {
            __int8 pair = (memory.read(i) >> 4) & 3;
            if (pair == 0)
            {
                //пара BC
                cout << "Load ACC from address in (BC)";
            }
            if (pair == 1)
            {
                //пара DE
                cout << "Load ACC from address in (DE)";
            }
        }

        //Store ACC indirect (STAX)
        if ((memory.read(i) & 207) == 2)
        {
            __int8 pair = (memory.read(i) >> 4) & 3;
            if (pair == 0)
            {
                //пара BC
                cout << "Save ACC to address in (BC)";
            }
            if (pair == 1)
            {
                //пара DE
                cout << "Save ACC to address in (DE)";
            }
        }

        //Exchange HL <-> DE
        if (memory.read(program_counter) == 235)
        {
            cout << "Exchange DE <-> HL";
        }





        cout << "\t";
        
        //ищем комментарий к команде и печатаем
        for (auto comm : comments)
        {
            if (comm.address == i) cout << comm.text;
        }

        cout << endl;
    }
    cout << "-------------------------------------------------" << endl;
    cout << endl;
};

//определение методов принтера

void Video_device::sync()
{


}

//определение методов клавиатуры

int KBD::get_data()
{

    return 0;
}


void Mem_Ctrl::write(unsigned __int16 address, unsigned __int8 data)
{
    if (address > size(mem_array)) 
    {
        global_error = 1;
    }
    else
    {
        mem_array[address] = data;
    }
}

unsigned __int8 Mem_Ctrl::read(unsigned __int16 address)
{
    if (address > size(mem_array))
    {
        return 0;
    }
    else
    {
        return mem_array[address];
    }
}

