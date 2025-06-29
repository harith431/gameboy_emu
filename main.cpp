#include <stdio.h>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cctype>

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}


#define MEMORY_SIZE 0x10000 // 64KB

using json = nlohmann::json;

// ======================= CPU STRUCT ========================
struct CPU {
    uint8_t A, B, C, D, E, F, H, L;       
    uint16_t PC = 0x00, STACK_P = 0;    
    int clock_cycles;
    bool IME = false;
    bool IME_Pending = false;
    bool halted = false;

    void setAF(uint16_t val) { A = val >> 8; F = val & 0xF0; }
    void setBC(uint16_t val) { B = val >> 8; C = val & 0xFF; }
    void setDE(uint16_t val) { D = val >> 8; E = val & 0xFF; }
    void setHL(uint16_t val) { H = val >> 8; L = val & 0xFF; }

    uint16_t getAF() { return (A << 8) | F; }
    uint16_t getBC() { return (B << 8) | C; }
    uint16_t getDE() { return (D << 8) | E; }
    uint16_t getHL() { return (H << 8) | L; }
    
    void setFlagZ(bool condition) { F = condition ? (F | 0x80) : (F & ~0x80); }
    void setFlagN(bool condition) { F = condition ? (F | 0x40) : (F & ~0x40); }
    void setFlagH(bool condition) { F = condition ? (F | 0x20) : (F & ~0x20); }
    void setFlagC(bool condition) { F = condition ? (F | 0x10) : (F & ~0x10); }

    bool getFlagC() { return (F & 0x10) != 0; }

};


// ======================= GLOBALS ==========================
CPU cpu;
json data;


class Memory {
public:
    std::vector<uint8_t> data;

    Memory() {
        data.resize(0x10000); // 64 KB
    }

    uint8_t read(uint16_t addr) const {
        return data[addr];
    }

    void write(uint16_t addr, uint8_t value) {
        data[addr] = value;
    }

    
};
Memory memory;
bool is_interrupt_pending() {
    uint8_t IE = memory.read(0xFFFF);  
    uint8_t IF = memory.read(0xFF0F);  
    return (IE & IF & 0x1F) != 0;       
}

std::vector<uint8_t> rom_data;
uint16_t rom_size = 0;

void generate_rom_data() {
    rom_data.clear();
    for (int i = 0x00; i <= 0xff; i++) {
        rom_data.push_back(i);
        rom_data.push_back( 0xcb);
        rom_data.push_back( i);
    }
    rom_data.push_back (0x76);
    rom_size = rom_data.size();
}
   
void load_rom() {
    for (uint16_t i = 0; i < rom_size && i< 0x8000 ; i++) {
        memory.write(i, rom_data[i]);
        
        printf("loaded opcode : 0x%02X\n", memory.read(i));
    }
}





// ===================== HELPER FUNCTIONS ===================

static uint8_t& resolve_register(const std::string& operand1) {
    if (operand1 == "A") return cpu.A;
    if (operand1 == "B") return cpu.B;
    if (operand1 == "C") return cpu.C;
    if (operand1 == "D") return cpu.D;
    if (operand1 == "E") return cpu.E;
    if (operand1 == "H") return cpu.H;
    if (operand1 == "L") return cpu.L;
    throw std::runtime_error("Unknown register: " + operand1);
}

static uint8_t resolve_value(const std::string& operand2) {
    if (operand2 == "d8") return memory.read(cpu.PC + 1);
    if (operand2 == "A")  return cpu.A;
    if (operand2 == "B")  return cpu.B;
    if (operand2 == "C")  return cpu.C;
    if (operand2 == "D")  return cpu.D;
    if (operand2 == "E")  return cpu.E;
    if (operand2 == "H")  return cpu.H;
    if (operand2 == "L")  return cpu.L;
    throw std::runtime_error("Unkown Operand value : " + operand2);
}

uint8_t getFlagBytes(const std::vector<std::string>& flags, uint8_t currentF = 0x00) {
    uint8_t f = currentF & 0x0F;

    if (flags.size() != 4) return f;

    if (flags[0] == "Z") f |= 0x80;
    if (flags[0] == "-") f |= 0x80;
    if (flags[1] == "N") f |= 0x40;
    else if (flags[1] == "0") f &= ~0x40;
    else if (flags[1] == "1") f |= 0x40;

    if (flags[2] == "H") f |= 0x20;
    else if (flags[2] == "0") f &= ~0x20;
    else if (flags[2] == "1") f |= 0x20;

    if (flags[3] == "C") f |= 0x10;
    else if (flags[3] == "0") f &= ~0x10;
    else if (flags[3] == "1") f |= 0x10;

    return cpu.F = f;
}




// ==================== EXECUTION ============================
void execute_instruction(const std::string& mnemonic, const std::string& operand1, const std::string& operand2, std::vector<std::string> flags, std::vector<int> cycles) {
    if (mnemonic == "RLC") {
        uint8_t val;

        if (operand1 == "(HL)") {
            uint16_t addr = cpu.getHL();
            val = memory.read(addr);

            uint8_t bit7 = (val & 0x80) >> 7;
            uint8_t result = ((val << 1) | bit7) & 0xFF;

            memory.write(addr, result);

            
            cpu.F = 0;
            if (result == 0) cpu.F |= 0x80;  
            if (bit7)         cpu.F |= 0x10; 

            cpu.clock_cycles = 16;
          
        }
        else {
            val = resolve_register(operand1);

            uint8_t bit7 = (val & 0x80) >> 7;
            uint8_t result = ((val << 1) | bit7) & 0xFF;

            resolve_register(operand1)= result;

            
            cpu.F = 0;
            if (result == 0) cpu.F |= 0x80;  
            if (bit7)         cpu.F |= 0x10; 

            cpu.clock_cycles = 8;
            
        }
    }

    if (mnemonic == "INC" && operand1 == "BC") {
        uint16_t x1 = cpu.getBC();
        x1 = x1 + 1;
        cpu.setBC(x1);
        cpu.clock_cycles = cycles[0];
       
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "INC" && operand1 == "DE") {
        uint16_t x1 = cpu.getDE();
        x1 = x1 + 1;
        cpu.setDE(x1);
        cpu.clock_cycles = cycles[0];
     
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "INC" && operand1 == "SP") {
        cpu.STACK_P += 1;
        cpu.clock_cycles = cycles[0];
        
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), cpu.STACK_P);
    }
    else if (mnemonic == "INC" && operand1 == "(HL)") {
        uint16_t addr = cpu.getHL();
        uint8_t val = memory.read(addr);
        val += 1;
        memory.write( addr,val);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), val, cpu.F);
    }
    
    else if (mnemonic == "INC" && operand1 == "BC") {
        cpu.setBC(cpu.getBC() + 1);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), cpu.getBC(), cpu.F);
    }
    else if (mnemonic == "INC" && operand1 == "HL") {
        cpu.setHL(cpu.getHL() + 1);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), cpu.getHL(), cpu.F);

    }
    else if (mnemonic == "INC") {
        uint8_t& x1 = resolve_register(operand1);
        x1 += 1;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), x1, cpu.F);

    }
    if (mnemonic == "DEC" && operand1 == "BC") {
        uint16_t x1 = cpu.getBC();
        x1 = x1 - 1;
        cpu.setBC(x1);       
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "DE") {
        uint16_t x1 = cpu.getDE();
        x1 = x1 - 1;
        cpu.setDE(x1);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "HL") {
        uint16_t x1 = cpu.getHL();
        x1 = x1 - 1;
        cpu.setHL(x1);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "SP") {
        cpu.STACK_P -= 1;
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), cpu.STACK_P);
    }
    else if (mnemonic == "DEC" && operand1 == "(HL)") {
        uint16_t addr = cpu.getHL();
        uint8_t val = memory.read(addr);
        val -= 1;
        memory.write(addr, val);
        cpu.F = getFlagBytes(flags);       
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), val, cpu.F);
    }
    else if (mnemonic == "DEC") {
        uint8_t& x1 = resolve_register(operand1);
        x1 -= 1;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), x1, cpu.F);
    }
    if (mnemonic == "NOP") {
        cpu.PC++;
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s  → 0x%04X\n", mnemonic.c_str(), cpu.PC);
    }   
    if (mnemonic == "RRC" && operand1 == "(HL)") {
        uint16_t addr = cpu.getHL();
        uint8_t val = memory.read(addr);
        uint8_t bit0 = val & 0x01;
        uint8_t result = (val >> 1) | (bit0 << 7);
        memory.write(addr, result);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), result, cpu.F);
    }
    else if  (mnemonic == "RRC") {
        uint8_t bit0 = resolve_register(operand1) & 0x01;
        uint8_t result = (resolve_register(operand1) >> 1) | (bit0 << 7);
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
    if (mnemonic == "RL" && operand1 == "(HL)") {
        uint16_t addr = memory.read(cpu.getHL());
        uint8_t val = memory.read(addr);
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit7 = (cpu.getHL() & 0x80) >> 7;
        uint8_t result = (cpu.getHL() << 1) | oldCarry;
        memory.write(addr, result);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
    else if (mnemonic == "RL") {
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit7 = (resolve_register(operand1) & 0x80) >> 7;
        uint8_t result = (resolve_register(operand1) << 1) | oldCarry;
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
     if (mnemonic == "RR" && operand1 == "(HL)") {
        uint16_t addr = memory.read(cpu.getHL());
        uint8_t val = memory.read(addr);
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit0 = memory.read(cpu.getHL()) & 0x01;
        uint8_t result = (memory.read(cpu.getHL()) >> 1) | (oldCarry << 7);
        memory.write(addr ,result);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
        
    }
    else if (mnemonic == "RR") {
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit0 = resolve_register(operand1) & 0x01;
        uint8_t result = (resolve_register(operand1) >> 1) | (oldCarry << 7);
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
    if (mnemonic == "JR" && operand1 =="NZ") {
        if (!(cpu.F & 0x80)) {
            int8_t offset = (int8_t)memory.read(cpu.PC+1);
            cpu.PC += 2 + offset;
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2;
            cpu.clock_cycles = cycles[1];
            // = length[0];
        }     
    } 
    else if (mnemonic == "JR" && operand1 == "NC") {
        if (!(cpu.F & 0x10)) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2;
            cpu.clock_cycles = cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "Z" && operand2 =="r8") {
        if (cpu.F & 0x80) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2;
            cpu.clock_cycles = cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "C") {
        if (cpu.F & 0x10) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2;
            cpu.clock_cycles = cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "r8") {
        int8_t offset = (int8_t)memory.read(cpu.PC+1);
        cpu.PC += 2 + offset;
        cpu.clock_cycles = cycles[0];
        // = length[0];
    }
  
    if (mnemonic == "JP" && operand1 == "NZ") {
            uint8_t lo = memory.read(cpu.PC + 1);
            uint8_t hi = memory.read(cpu.PC + 2);
            uint16_t addr = (hi << 8) | lo;

            if (!(cpu.F & 0x80)) {
                cpu.PC = addr;
                cpu.clock_cycles = cycles[0];
               
                // = length[0];
            }
            else {
                cpu.PC += 3;
                cpu.clock_cycles = cycles[1];
              
                // = length[0];
            }
    }
    if (mnemonic == "JP" && operand1 == "NC") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);
        uint16_t addr = (hi << 8) | lo;

        if (!(cpu.F & 0x10)) {
            cpu.PC = addr;
            cpu.clock_cycles = cycles[0];
           
            // = length[0];
        }
        else {
            cpu.PC += 3;
            cpu.clock_cycles = cycles[1];
            
            // = length[0];
        }
    }
    if (mnemonic == "JP" && operand1 == "U16") {
        uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
        cpu.PC = addr;
        cpu.clock_cycles = cycles[0];
        
        // = length[0];
    }
    if (mnemonic == "JP" && operand1 == "Z") {
        if (cpu.F & 0x80){
            uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
            cpu.PC = addr;
            cpu.clock_cycles = cycles[0];
            
            // = length[0];
        }
        else {
            cpu.PC += 3;
            cpu.clock_cycles = cycles[1];

            // = length[0];
        }
        
    }
    if (mnemonic == "JP" && operand1 == "C") {
        if (cpu.F & 0x10) {
            uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
            cpu.PC = addr;
            cpu.clock_cycles = cycles[0];
           
            // = length[0];

        }
        else {
            cpu.PC += 3;
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }

    }
    if (mnemonic == "JP" && operand1 == "HL") {
        cpu.PC = cpu.getHL();
        cpu.clock_cycles = cycles[0];
        // = length[0];
    }
    if (mnemonic == "DAA") {
        uint8_t a = cpu.A;
        bool n = cpu.F & 0x40;  
        bool h = cpu.F & 0x20;  
        bool c = cpu.F & 0x10;  

        uint8_t correction = 0;

        if (!n) { // After addition
            if (h || (a & 0x0F) > 9) correction += 0x06;
            if (c || a > 0x99) {
                correction += 0x60;
                cpu.F |= 0x10; // Set Carry flag
                cpu.clock_cycles = cycles[0];
                // = length[0];
            }
        }
        else {  // After subtraction
            if (h) correction |= 0x06;
            if (c) correction |= 0x60;
            a -= correction;
            cpu.clock_cycles = cycles[0];
            // = length[0];

        }

        if (!n) a += correction;

       
        cpu.F &= 0x40;               // Keep only N flag, clear Z, H, C temporarily
        if (a == 0) cpu.F |= 0x80;   // Set Z if result is zero
        if (correction & 0x60) cpu.F |= 0x10; // Set C if high nibble corrected
        cpu.F &= ~0x20;              // Always clear H after DAA

        cpu.A = a;
        cpu.clock_cycles = cycles[0];
        // = length[0];
    }
    if (mnemonic == "SCF") {
        cpu.F = getFlagBytes(flags);
    }

    // ------- ADD ----------------
    if (mnemonic == "ADD") {
        if (operand1 == "A" && operand2 == "(HL)") {
            uint16_t x1 = cpu.A + memory.read(cpu.getHL());
         cpu.A = x1 & 0xff;
         printf("Executed ADD A (HL) : 0x%02X\n", cpu.A);
         cpu.F = getFlagBytes(flags);
         cpu.clock_cycles = cycles[0];
         // = length[0];
        }
        else if (operand2 == "A") {
            uint16_t x1 = cpu.A + cpu.A;
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else if (operand2 == "B") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else if (operand2 == "C") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
            // = length[0];
        }
        else if (operand2 == "D") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "E") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "H") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "L") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand2 == "d8") {
            uint16_t x1 = cpu.A + resolve_value(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand1 == "i8") {
            int8_t offset = (int8_t)memory.read(cpu.PC + 1);
            uint16_t sp = cpu.STACK_P;              
            int32_t result = sp + offset;      
            cpu.STACK_P = result & 0xFFFF;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand1 == "HL"){

            uint16_t hl = cpu.getHL();
            uint16_t value = 0;

            if (operand2 == "BC") value = (cpu.B << 8) | cpu.C;
            else if (operand2 == "DE") value = (cpu.D << 8) | cpu.E;
            else if (operand2 == "HL") value = hl;
            else if (operand2 == "SP") value = cpu.STACK_P;

            uint32_t result = hl + value;

            cpu.setHL(result & 0xFFFF);

           
            
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
           
        }

        
        

    }
    if (mnemonic == "SUB") {
        if (operand1 == "A" && operand2 == "(HL)") {

            cpu.A = cpu.A - memory.read(cpu.getHL());
            cpu.A &= 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "A") {
            uint16_t x1 = cpu.A - cpu.A;
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "B") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "C") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "D") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "E") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "H") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "L") {
            uint16_t x1 = cpu.A - resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand2 == "d8") {
            uint16_t x1 = cpu.A - resolve_value(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand1 == "i8") {
            int8_t offset = (int8_t)memory.read(cpu.PC + 1);
            uint16_t sp = cpu.STACK_P;
            int32_t result = sp - offset;
            cpu.STACK_P = result & 0xFFFF;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand1 == "HL") {
            if (operand2 == "BC") {
                uint32_t x1 = cpu.getHL() - resolve_register(operand2);
                cpu.setHL(x1 & 0xffff);
                cpu.F = getFlagBytes(flags);
                cpu.clock_cycles = cycles[0];
            }
            else if (operand2 == "DE") {
                uint32_t x1 = cpu.getHL() - resolve_register(operand2);
                cpu.setHL(x1 & 0xffff);
                cpu.F = getFlagBytes(flags);
                cpu.clock_cycles = cycles[0];
            }
            else if (operand2 == "HL") {
                uint32_t x1 = cpu.getHL() - resolve_register(operand2);
                cpu.setHL(x1 & 0xffff);
                cpu.F = getFlagBytes(flags);
                cpu.clock_cycles = cycles[0];
            }
            else if (operand2 == "SP") {
                uint32_t x1 = cpu.getHL() - cpu.STACK_P;
                cpu.setHL(x1 & 0xffff);
                cpu.F = getFlagBytes(flags);
                cpu.clock_cycles = cycles[0];
            }
        }


    }
    if (mnemonic == "AND") {
        if (operand1 == "A" && operand2 == "(HL)") {
            cpu.A = cpu.A & memory.read(cpu.getHL());
            cpu.A &= 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "A") {
            uint16_t x1 = cpu.A & cpu.A;
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "B") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "C") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "D") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "E") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "H") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "L") {
            uint16_t x1 = cpu.A & resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand2 == "d8") {
            uint16_t x1 = cpu.A & resolve_value(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
    }
    if (mnemonic == "OR") {
        if (operand1 == "A" && operand2 == "(HL)") {
            cpu.A = cpu.A | memory.read(cpu.getHL());
            cpu.A &= 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "A") {
            uint16_t x1 = cpu.A | cpu.A;
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "B") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "C") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "D") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "E") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "H") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        else if (operand2 == "L") {
            uint16_t x1 = cpu.A | resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
        if (operand2 == "d8") {
            uint16_t x1 = cpu.A | resolve_value(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles = cycles[0];
        }
    }
   

// ---------------- ADC (Add with Carry) ----------------
    if (mnemonic == "ADC") {
        uint8_t value = resolve_value(operand2);
        uint8_t carry = cpu.getFlagC() ? 1 : 0;
        uint16_t result = cpu.A + value + carry;
        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(((cpu.A & 0xF) + (value & 0xF) + carry) > 0xF);
        cpu.setFlagC(result > 0xFF);
        cpu.A = result & 0xFF;
        cpu.clock_cycles = cycles[0];
        printf("Executed: ADC A, %s -> 0x%02X\n", operand2.c_str(), cpu.A);
    }

    // ---------------- SUB (Subtract) ----------------
    else if (mnemonic == "SUB") {
        uint8_t value = resolve_value(operand1);
        uint16_t result = cpu.A - value;
        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(true);
        cpu.setFlagH((cpu.A & 0xF) < (value & 0xF));
        cpu.setFlagC(cpu.A < value);
        cpu.A = result & 0xFF;
        cpu.clock_cycles = cycles[0];
        printf("Executed: SUB A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }

    // ---------------- AND (Bitwise AND) ----------------
    else if (mnemonic == "AND") {
        uint8_t value = resolve_value(operand1);
        cpu.A &= value;
        cpu.setFlagZ(cpu.A == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(true);
        cpu.setFlagC(false);
        cpu.clock_cycles = cycles[0];
        printf("Executed: AND A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }

    // ---------------- XOR (Bitwise XOR) ----------------
    else if (mnemonic == "XOR") {
        uint8_t value = resolve_value(operand1);
        cpu.A ^= value;
        cpu.setFlagZ(cpu.A == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(false);
        cpu.clock_cycles = cycles[0];
        printf("Executed: XOR A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }

    // ---------------- OR (Bitwise OR) ----------------
    else if (mnemonic == "OR") {
        uint8_t value = resolve_value(operand1);
        cpu.A |= value;
        cpu.setFlagZ(cpu.A == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(false);
        cpu.clock_cycles = cycles[0];
        printf("Executed: OR A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }

    // ---------------- CP (Compare) ----------------
    else if (mnemonic == "CP") {
        uint8_t value = resolve_value(operand1);
        uint16_t result = cpu.A - value;
        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(true);
        cpu.setFlagH((cpu.A & 0xF) < (value & 0xF));
        cpu.setFlagC(cpu.A < value);
        cpu.clock_cycles = cycles[0];
        printf("Executed: CP A, %s\n", operand1.c_str());
    }

    // ---------------- SCF (Set Carry Flag) ----------------
    else if (mnemonic == "SCF") {
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(true);
        cpu.clock_cycles = cycles[0];
        printf("Executed: SCF\n");
    }

    // ---------------- CCF (Complement Carry Flag) ----------------
    else if (mnemonic == "CCF") {
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(!cpu.getFlagC());
        cpu.clock_cycles = cycles[0];
        printf("Executed: CCF\n");
    }

    // ---------------- CPL (Complement A) ----------------
    else if (mnemonic == "CPL") {
        cpu.A = ~cpu.A;
        cpu.setFlagN(true);
        cpu.setFlagH(true);
        cpu.clock_cycles = cycles[0];
        printf("Executed: CPL A -> 0x%02X\n", cpu.A);
    }

    // ---------------- PUSH (Stack Push) ----------------
    else if (mnemonic == "PUSH") {
        uint16_t val = 0;
        if (operand1 == "AF") val = cpu.getAF();
        else if (operand1 == "BC") val = cpu.getBC();
        else if (operand1 == "DE") val = cpu.getDE();
        else if (operand1 == "HL") val = cpu.getHL();
        cpu.STACK_P -= 2;
        memory.write(cpu.STACK_P + 1, val >> 8);
        memory.write(cpu.STACK_P, val & 0xFF);
        cpu.clock_cycles = cycles[0];
        printf("Executed: PUSH %s\n", operand1.c_str());
    }

    // ---------------- POP (Stack Pop) ----------------
    else if (mnemonic == "POP") {
        uint16_t val = memory.read(cpu.STACK_P) | (memory.read(cpu.STACK_P + 1) << 8);
        if (operand1 == "AF") cpu.setAF(val);
        else if (operand1 == "BC") cpu.setBC(val);
        else if (operand1 == "DE") cpu.setDE(val);
        else if (operand1 == "HL") cpu.setHL(val);
        cpu.STACK_P += 2;
        cpu.clock_cycles = cycles[0];
        printf("Executed: POP %s\n", operand1.c_str());
    }

    // ---------------- CALL (Call Subroutine) ----------------
    else if (mnemonic == "CALL") {
        bool do_call = true;
        if (operand1 == "NZ") do_call = !(cpu.F & 0x80);
        else if (operand1 == "Z") do_call = cpu.F & 0x80;
        else if (operand1 == "NC") do_call = !(cpu.F & 0x10);
        else if (operand1 == "C") do_call = cpu.F & 0x10;

        if (operand1 == "NZ" || operand1 == "Z" || operand1 == "NC" || operand1 == "C") {
            if (!do_call) {
                cpu.PC += 3;
                cpu.clock_cycles = cycles[1];
                return;
            }
        }

        uint16_t addr = memory.read(cpu.PC + (operand2.empty() ? 1 : 2)) |
            (memory.read(cpu.PC + (operand2.empty() ? 2 : 3)) << 8);
        cpu.STACK_P -= 2;
        memory.write(cpu.STACK_P, (cpu.PC + (operand2.empty() ? 3 : 3)) & 0xFF);
        memory.write(cpu.STACK_P + 1, (cpu.PC + (operand2.empty() ? 3 : 3)) >> 8);
        cpu.PC = addr;
        cpu.clock_cycles = cycles[0];
        printf("Executed: CALL %s 0x%04X\n", operand1.c_str(), addr);
    }

    // ---------------- RET (Return from Subroutine) ----------------
    else if (mnemonic == "RET") {
        bool do_return = true;
        if (operand1 == "NZ") do_return = !(cpu.F & 0x80);
        else if (operand1 == "Z") do_return = cpu.F & 0x80;
        else if (operand1 == "NC") do_return = !(cpu.F & 0x10);
        else if (operand1 == "C") do_return = cpu.F & 0x10;

        if (!do_return && operand1 != "") {
            cpu.PC += 1;
            cpu.clock_cycles = cycles[1];
            return;
        }

        uint16_t ret = memory.read(cpu.STACK_P) | (memory.read(cpu.STACK_P + 1) << 8);
        cpu.STACK_P += 2;
        cpu.PC = ret;
        cpu.clock_cycles = cycles[0];
        printf("Executed: RET %s\n", operand1.c_str());
    }

// ---------------- RST (Restart to fixed addr) ----------------
    else if (mnemonic == "RST") {
    uint8_t rst_val = 0;
    if (operand1 == "00H") rst_val = 0x00;
    else if (operand1 == "08H") rst_val = 0x08;
    else if (operand1 == "10H") rst_val = 0x10;
    else if (operand1 == "18H") rst_val = 0x18;
    else if (operand1 == "20H") rst_val = 0x20;
    else if (operand1 == "28H") rst_val = 0x28;
    else if (operand1 == "30H") rst_val = 0x30;
    else if (operand1 == "38H") rst_val = 0x38;
    cpu.STACK_P -= 2;
    memory.write(cpu.STACK_P, cpu.PC & 0xFF);
    memory.write(cpu.STACK_P + 1, cpu.PC >> 8);
    cpu.PC = rst_val;
    cpu.clock_cycles = cycles[0];
    printf("Executed: RST %s\n", operand1.c_str());
    }

    // ---------------- HALT ----------------
    else if (mnemonic == "HALT") {
            if (cpu.IME || is_interrupt_pending()) {
                cpu.halted = true;
            }
            else {
                cpu.halted = true;  
            }

            printf("Executed: HALT (CPU enters halted state)\n");
           
            cpu.clock_cycles = cycles[0];
    }

        // ---------------- RETI (Return + Enable Interrupts) ----------------
    else if (mnemonic == "RETI") {
            uint16_t ret = memory.read(cpu.STACK_P) | (memory.read(cpu.STACK_P + 1) << 8);
            cpu.STACK_P += 2;
            cpu.PC = ret;
            cpu.clock_cycles = cycles[0];
            printf("Executed: RETI\n");
    }

            // ---------------- STOP ----------------
    else if (mnemonic == "STOP") {
                // Would normally wait for button press or reset event
                printf("Executed: STOP (emulation would freeze here)\n");
                cpu.clock_cycles = cycles[0];
    }

                // ---------------- LDH (High RAM I/O) ----------------
    else if (mnemonic == "LDH") {
                    if (operand1 == "(a8)") {
                        uint8_t val = cpu.A;
                        uint8_t addr = memory.read(cpu.PC + 1);
                        memory.write(0xFF00 + addr, val);
                        printf("Executed: LDH (0xFF%02X), A = 0x%02X\n", addr, val);
                    }
                    else if (operand1 == "A") {
                        uint8_t addr = memory.read(cpu.PC + 1);
                        cpu.A = memory.read(0xFF00 + addr);
                        printf("Executed: LDH A, (0xFF%02X) = 0x%02X\n", addr, cpu.A);
                    }
                    cpu.clock_cycles = cycles[0];
    }

// ---------------- LD HL, SP+r8 ----------------
    else if (mnemonic == "LD" && operand1 == "HL" && operand2 == "SP+r8") {
        int8_t offset = (int8_t)memory.read(cpu.PC + 1);
        uint16_t result = cpu.STACK_P + offset;
        cpu.setHL(result);
        cpu.setFlagZ(false);
        cpu.setFlagN(false);
        cpu.setFlagH(((cpu.STACK_P & 0x0F) + (offset & 0x0F)) > 0x0F);
        cpu.setFlagC(((cpu.STACK_P & 0xFF) + (offset & 0xFF)) > 0xFF);
        cpu.clock_cycles = cycles[0];
        printf("Executed: LD HL, SP+%d → 0x%04X\n", offset, result);
}

// ---------------- LD (C), A ----------------
    else if (mnemonic == "LD" && operand1 == "(C)" && operand2 == "A") {
        memory.write(0xFF00 + cpu.C, cpu.A);
        cpu.clock_cycles = cycles[0];
        printf("Executed: LD (0xFF%02X), A = 0x%02X\n", cpu.C, cpu.A);
        }

        // ---------------- LD A, (C) ----------------
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(C)") {
            cpu.A = memory.read(0xFF00 + cpu.C);
            cpu.clock_cycles = cycles[0];
            printf("Executed: LD A, (0xFF%02X) = 0x%02X\n", cpu.C, cpu.A);
            }

    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(a8)") {
        cpu.A = memory.read(0xFF00 + memory.read(cpu.PC + 1));
        cpu.clock_cycles = cycles[0];
        printf("Executed: LD (0xFF%02X), A = 0x%02X\n", cpu.C, cpu.A);
            }
    else if (mnemonic == "LD" && operand1 == "(a8)" && operand2 == "A") {
        memory.write(0xFF00 + memory.read(cpu.PC + 1), cpu.A);
        cpu.clock_cycles = cycles[0];
        printf("Executed: LD a8, (0xFF%02X) = 0x%02X\n", cpu.C, cpu.A);
    }

            // ---------------- LD SP, HL ----------------
    else if (mnemonic == "LD" && operand1 == "SP" && operand2 == "HL") {
                cpu.STACK_P = cpu.getHL();
                cpu.clock_cycles = cycles[0];
                printf("Executed: LD SP, HL = 0x%04X\n", cpu.STACK_P);
                }

                // ---------------- LD (a16), A ----------------
    else if (mnemonic == "LD" && operand1 == "(a16)" && operand2 == "A") {
                    uint8_t lo = memory.read(cpu.PC + 1);
                    uint8_t hi = memory.read(cpu.PC + 2);
                    uint16_t addr = (hi << 8) | lo;
                    memory.write(addr, cpu.A);
                    cpu.clock_cycles = cycles[0];
                    printf("Executed: LD (0x%04X), A = 0x%02X\n", addr, cpu.A);
                    }

                    // ---------------- LD A, (a16) ----------------
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(a16)") {
                        uint8_t lo = memory.read(cpu.PC + 1);
                        uint8_t hi = memory.read(cpu.PC + 2);
                        uint16_t addr = (hi << 8) | lo;
                        cpu.A = memory.read(addr);
                        cpu.clock_cycles = cycles[0];
                        printf("Executed: LD A, (0x%04X) = 0x%02X\n", addr, cpu.A);
    }

     // ---------------- LD remaing -------------------------------

    else if (mnemonic == "LD" && operand1 == "BC" && operand2 == "d16") {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.setBC(x1);
        printf("Executed: LD BC = 0x%04X\n", cpu.getBC());
    }
    else if (mnemonic == "LD" && operand1 == "DE")
    {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.setBC(x1);
        printf("Executed: LD DE = 0x%04X\n", cpu.getDE());
    }
    else if (mnemonic == "LD" && operand1 == "HL") {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.setBC(x1);
        printf("Executed: LD HL = 0x%04X\n", cpu.getHL());
    }
    else if (mnemonic == "LD" && operand1 == "SP" && operand2 == "d16") {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.STACK_P =x1;
        printf("Executed: LD BC = 0x%04X\n", cpu.getBC());
    }
    else if (mnemonic == "LD" && operand1 == "B") {
        if (operand2 == "B") { cpu.B = cpu.B; }
        if (operand2 == "C") { cpu.B = cpu.C; }
        if (operand2 == "D") { cpu.B = cpu.D; }
        if (operand2 == "E") { cpu.B = cpu.E; }
        if (operand2 == "H") { cpu.B = cpu.H; }
        if (operand2 == "L") { cpu.B = cpu.L; }
        if (operand2 == "A") { cpu.B = cpu.A; }
        if (operand2 == "(HL)") { cpu.B = memory.read(cpu.getHL()); }
    }
    else if (mnemonic == "LD" && operand1 == "C") {
        if (operand2 == "(HL)") {
            cpu.C = memory.read(cpu.getHL());
        }
        else {
            cpu.C = resolve_value(operand2);
        }
    }
    
    else if (mnemonic == "LD" && operand1 == "D") {
        if (operand2 == "(HL)") {
            cpu.D = memory.read(cpu.getHL());
        }
        else {
            cpu.D = resolve_value(operand2);
        }
    }
    else if (mnemonic == "LD" && operand1 == "E") {
        if (operand2 == "(HL)") {
            cpu.E = memory.read(cpu.getHL());
        }
        else {
            cpu.E = resolve_value(operand2);
        }
    }
    else if (mnemonic == "LD" && operand1 == "H") {
        if (operand2 == "(HL)") {
            cpu.H = memory.read(cpu.getHL());
        }
        else {
            cpu.H = resolve_value(operand2);
        }
    }
    else if (mnemonic == "LD" && operand1 == "L") {
        if (operand2 == "(HL)") {
            cpu.L = memory.read(cpu.getHL());
        }
        else {
            cpu.L = resolve_value(operand2);
        }
    }
    else if (mnemonic == "LD" && operand1 == "(HL)") {
        memory.write(cpu.getHL(), resolve_value(operand2));
    }

    else if (mnemonic == "LD" && operand2 == "A") {
        if (operand1 == "(BC)") { memory.write(cpu.getBC(), cpu.A); }
        if (operand1 == "(DE)") { memory.write(cpu.getDE(), cpu.A); }
        if (operand1 == "(HL+)") {
            uint16_t addr = cpu.getHL();
            memory.write(addr, cpu.A);
            cpu.setHL(addr + 1);

            cpu.clock_cycles = 8;
        }
        if (operand1 == "(HL-") {
            uint16_t addr = cpu.getHL();
            memory.write(addr, cpu.A);
            cpu.setHL(addr - 1);

            cpu.clock_cycles = 8;
        }
      


    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(BC)") {
        uint16_t addr = cpu.getBC();
        cpu.A = (memory.read(addr) & 0xff);
        cpu.clock_cycles = cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(DE)") {
        uint16_t addr = cpu.getDE();
        cpu.A = (memory.read(addr) & 0xff);
        cpu.clock_cycles = cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(HL+)") {
        uint16_t addr = cpu.getBC();
        cpu.A = (memory.read(addr + 1) & 0xff);
        cpu.clock_cycles = cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(HL-)") {
        uint16_t addr = cpu.getBC();
        cpu.A = (memory.read(addr - 1) & 0xff);
        cpu.clock_cycles = cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A") {
        if (operand2 == "(HL)") {
            cpu.A = memory.read(cpu.getHL());
        }
        else {
            cpu.B = resolve_value(operand2);
        }
    }
    

    // ---------------- BIT x, r ----------------
     if (mnemonic == "BIT") {
        if (operand2 == "(HL)") {
            uint16_t addr = cpu.getHL();
            uint8_t val = memory.read(addr);

            int bit_index = std::stoi(operand1);  

            
            bool bit_set = val & (1 << bit_index);

            cpu.F &= 0x10;               // Preserve only Carry flag
            cpu.F |= 0x20;               // Set H
            if (!bit_set) cpu.F |= 0x80; // Set Z if bit not set

            cpu.clock_cycles = 12;

        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t val = resolve_value(operand2);
            cpu.setFlagZ(!(val & (1 << bit)));
            cpu.setFlagN(false);
            cpu.setFlagH(true);
            cpu.clock_cycles = cycles[0];
            printf("Executed: BIT %d, %s\n", bit, operand2.c_str());
        }
    }

        // ---------------- RES x, r ----------------
     if (mnemonic == "RES") {
        if (operand2 == "(HL)") {
            uint16_t addr = cpu.getHL();
            uint8_t val = memory.read(addr);

            int bit_index = std::stoi(operand1); // "0" to "7"

            val &= ~(1 << bit_index); // Clear the bit

            memory.write(addr, val);

            cpu.clock_cycles = 16;
        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t& reg = resolve_register(operand2);
            reg &= ~(1 << bit);
            cpu.clock_cycles = cycles[0];
            printf("Executed: RES %d, %s -> 0x%02X\n", bit, operand2.c_str(), reg);
        }
    }

            // ---------------- SET x, r ----------------
    if (mnemonic == "SET") {
        if (operand2 == "(HL)") {
            uint16_t addr = cpu.getHL();
            uint8_t val = memory.read(addr);

            int bit_index = std::stoi(operand1); 

            val |= (1 << bit_index); // set bit x

            memory.write(addr, val);

            cpu.clock_cycles = 16;
        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t& reg = resolve_register(operand2);
            reg |= (1 << bit);
            cpu.clock_cycles = cycles[0];
            printf("Executed: SET %d, %s -> 0x%02X\n", bit, operand2.c_str(), reg);
        }
    }
    // ---------------- SWAP r ----------------
    if (mnemonic == "SWAP") {
        if (operand1 == "(HL)") {
            uint16_t addr = cpu.getHL();
            uint8_t val = memory.read(addr);

            uint8_t result = ((val & 0xF0) >> 4) | ((val & 0x0F) << 4);
            memory.write(addr, result);

   
            cpu.F = 0;
            if (result == 0) cpu.F |= 0x80;  // Z

            cpu.clock_cycles = 16;

        }
        else {
            uint8_t& reg = resolve_register(operand1);
            reg = ((reg & 0xF) << 4) | ((reg & 0xF0) >> 4);
            cpu.setFlagZ(reg == 0);
            cpu.setFlagN(false);
            cpu.setFlagH(false);
            cpu.setFlagC(false);
            cpu.clock_cycles = cycles[0];
            printf("Executed: SWAP %s -> 0x%02X\n", operand1.c_str(), reg);
        }
    }

        // ---------------- SLA r ----------------
     if (mnemonic == "SLA") {

            if (operand1 == "(HL)") {
                uint16_t addr = cpu.getHL();
                uint8_t val = memory.read(addr);

                uint8_t bit7 = (val & 0x80) >> 7;
                uint8_t result = (val << 1) & 0xFF;

                memory.write(addr, result);

                // Set flags
                cpu.F = 0;
                if (result == 0) cpu.F |= 0x80;  // Z
                if (bit7)         cpu.F |= 0x10; // C

                cpu.clock_cycles = 16;
            }

            else {
                uint8_t& reg = resolve_register(operand1);
                cpu.setFlagC((reg & 0x80) != 0);
                reg <<= 1;
                cpu.setFlagZ(reg == 0);
                cpu.setFlagN(false);
                cpu.setFlagH(false);
                cpu.clock_cycles = cycles[0];
                printf("Executed: SLA %s -> 0x%02X\n", operand1.c_str(), reg);
            }
    }
       

            // ---------------- SRA r ----------------
     if (mnemonic == "SRA") {
                if (operand1 == "(HL)") {
                    uint16_t addr = cpu.getHL();
                    uint8_t val = memory.read(addr);

                    uint8_t bit0 = val & 0x01;
                    uint8_t bit7 = val & 0x80; 

                    uint8_t result = (val >> 1) | bit7; 

                    memory.write(addr, result);

                    // Set flags
                    cpu.F = 0;
                    if (result == 0) cpu.F |= 0x80;  // Z
                    if (bit0)        cpu.F |= 0x10;  // C

                    cpu.clock_cycles = 16;
                }
                else {
                    uint8_t& reg = resolve_register(operand1);
                    cpu.setFlagC(reg & 0x01);
                    reg = (reg >> 1) | (reg & 0x80);
                    cpu.setFlagZ(reg == 0);
                    cpu.setFlagN(false);
                    cpu.setFlagH(false);
                    cpu.clock_cycles = cycles[0];
                    printf("Executed: SRA %s -> 0x%02X\n", operand1.c_str(), reg);
                }
     }
    // ---------------- SBC (Subtract with Carry) ----------------
    else if (mnemonic == "SBC") {
        uint8_t value = resolve_value(operand2);
        uint8_t carry = cpu.getFlagC() ? 1 : 0;
        uint16_t result = cpu.A - value - carry;
        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(true);
        cpu.setFlagH(((cpu.A & 0xF) - (value & 0xF) - carry) < 0);
        cpu.setFlagC(cpu.A < (value + carry));
        cpu.A = result & 0xFF;
        cpu.clock_cycles = cycles[0];
        // = length[0];
        printf("Executed: SBC A, %s -> 0x%02X\n", operand2.c_str(), cpu.A);
        }

        // ---------------- DI (Disable Interrupts) ----------------
    else if (mnemonic == "DI") {
            cpu.IME = false;
            printf("Executed: DI (Interrupts disabled)\n");
            cpu.clock_cycles = cycles[0];
            }

            // ---------------- EI (Enable Interrupts) ----------------
    else if (mnemonic == "EI") {
        cpu.IME_Pending = true;
        printf("Executed: EI (Interrupts enabled)\n");
        cpu.clock_cycles = cycles[0];
    }
    // ----------- RLCA ----------------
    else if (mnemonic == "RLCA") {
        uint8_t bit7 = (cpu.A & 0x80) >> 7;
        cpu.A = ((cpu.A << 1) | bit7) & 0xFF;

        cpu.F = 0;  
        if (bit7) cpu.F |= 0x10;  
        printf(" Executed RLCA : 0x%02X\n", cpu.A);
        cpu.clock_cycles = cycles[0];
        
    }
    // ------------- RRCA --------------------
    else if (mnemonic == "RRCA") {
        uint8_t bit0 = cpu.A & 0x01;
        cpu.A = ((bit0 << 7) | (cpu.A >> 1)) & 0xFF;

        cpu.F = 0;  
        if (bit0) cpu.F |= 0x10;  
        printf("Executed RRCA cpu.A : 0x%02X\n", cpu.A);
        cpu.clock_cycles = cycles[0];
        
    }
    //---------------RLA -------------------
    else if (mnemonic == "RLA") {
        uint8_t old_carry = (cpu.F & 0x10) ? 1 : 0;
        uint8_t bit7 = (cpu.A & 0x80) >> 7;

        cpu.A = ((cpu.A << 1) | old_carry) & 0xFF;
        printf("Executed RLA cpu.A : 0x%02X\n", cpu.A);
        cpu.F = 0;  
        if (bit7) cpu.F |= 0x10;  
        cpu.clock_cycles = cycles[0];
    }
    // ------------- RRA -------------------
    else if (mnemonic == "RRA") {
        uint8_t old_carry = (cpu.F & 0x10) ? 1 : 0;
        uint8_t bit0 = cpu.A & 0x01;

        cpu.A = ((old_carry << 7) | (cpu.A >> 1)) & 0xFF;
        printf("Executed RRA cpu.A : 0x%02X\n", cpu.A);
        if (bit0) cpu.F |= 0x10;  

        cpu.clock_cycles = cycles[0];

    }


    
} 


           

    int handle_instruction_metadata(uint8_t opcode) {
        char opcode_str[5];
        snprintf(opcode_str, sizeof(opcode_str), "0x%02X", opcode);
        std::string key = to_lower(opcode_str);

        if (opcode == 0xCB) {
            uint8_t cb_opcode = memory.read(cpu.PC + 1);
            char cb_str[5];
            snprintf(cb_str, sizeof(cb_str), "0x%02X", cb_opcode);
            std::string cb_key = to_lower(cb_str);

            if (data["cbprefixed"].contains(cb_key)) {
                auto info = data["cbprefixed"][cb_key];

                std::string mnemonic = info["mnemonic"];
                std::string operand1 = info.value("operand1", "");
                std::string operand2 = info.value("operand2", "");
                std::vector<std::string> flags = info.value("flags", std::vector<std::string>());
                std::vector<int> length;
                if (info["length"].is_array()) {
                    length = info["length"].get<std::vector<int>>();
                }
                else {
                    length = { info["length"].get<int>() };
                }

                std::vector<int> cycles;
                if (info["cycles"].is_array()) {
                    cycles = info["cycles"].get<std::vector<int>>();
                }
                else {
                    cycles = { info["cycles"].get<int>() };
                }
                printf("CB Opcode 0x%02X: mnemonic=%s operand1=%s\n", cb_opcode, mnemonic.c_str(), operand1.c_str());
                // printf("0x%02X", cpu.PC); 
                
                execute_instruction(mnemonic, operand1, operand2, flags, cycles);
               
            }
            else {
                printf("Unknown CB-prefixed opcode: 0x%02X\n", cb_opcode);
            }
            return 1;
        }

        if (data["unprefixed"].contains(key)) {
            auto info = data["unprefixed"][key];

            std::string mnemonic = info["mnemonic"];
            std::string operand1 = info.value("operand1", "");
            std::string operand2 = info.value("operand2", "");
            std::vector<std::string> flags = info.value("flags", std::vector<std::string>());

            std::vector<int> length;
            if (info["length"].is_array()) {
                length = info["length"].get<std::vector<int>>();
            }
            else {
                length = { info["length"].get<int>() };
            }

            std::vector<int> cycles;
            if (info["cycles"].is_array()) {
                cycles = info["cycles"].get<std::vector<int>>();
            }
            else {
                cycles = { info["cycles"].get<int>() };
            }
            if (!flags.empty())
                cpu.F = getFlagBytes(flags);
            int inst_length = length[1];
            execute_instruction(mnemonic, operand1, operand2,flags,cycles);
            return inst_length;
          
        }
        else {
            printf("Unknown opcode metadata: 0x%02X\n", opcode);
            printf("Handled opcode 0x%02X \n", opcode);

        }

        return 1;
    };

   
    void service_interrupts() {
        if (!cpu.IME) return;

        uint8_t IE = memory.read(0xFFFF);
        uint8_t IF = memory.read(0xFF0F);
        uint8_t pending = IE & IF;

        if (pending == 0) return ;

        cpu.IME = false;

        for (int i = 0; i < 5; i++) {
            if (pending & (1 << i)) {
                
                cpu.STACK_P -= 2;
                memory.write(cpu.STACK_P, cpu.PC & 0xFF);         // Low byte
                memory.write(cpu.STACK_P + 1, (cpu.PC >> 8));     // High byte

                
                memory.write(0xFF0F, IF & ~(1 << i));

                // Jump to interrupt vector
                static const uint16_t vector[5] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
                cpu.PC = vector[i];
                return;
            }
        }
    }
    void Intial_cpu_stage() {
        cpu.setAF(0x01B0);
        cpu.setBC (0x0013);
        cpu.setDE (0x00D8);
        cpu.setHL(0x014D);
        cpu.STACK_P = 0xFFFE;
        cpu.PC = 0x0000;

        memory.write(0xFF05, 0x00); // TIMA
        memory.write(0xFF06, 0x00); // TMA
        memory.write(0xFF07, 0x00); // TAC
        memory.write(0xFF10, 0x80); // NR10
        memory.write(0xFF11, 0xBF); // NR11
        memory.write(0xFF12, 0xF3); // NR12
        memory.write(0xFF14, 0xBF); // NR14
        memory.write(0xFF16, 0x3F); // NR21
        memory.write(0xFF17, 0x00); // NR22
        memory.write(0xFF19, 0xBF); // NR24
        memory.write(0xFF1A, 0x7F); // NR30
        memory.write(0xFF1B, 0xFF); // NR31
        memory.write(0xFF1C, 0x9F); // NR32
        memory.write(0xFF1E, 0xBF); // NR33
        memory.write(0xFF20, 0xFF); // NR41
        memory.write(0xFF21, 0x00); // NR42
        memory.write(0xFF22, 0x00); // NR43
        memory.write(0xFF23, 0xBF); // NR30
        memory.write(0xFF24, 0x77); // NR50
        memory.write(0xFF25, 0xF3); // NR51
        memory.write(0xFF26, 0xF1); // NR52 (CGB = 0xF0)
        memory.write(0xFF40, 0x91); // LCDC
        memory.write(0xFF42, 0x00); // SCY
        memory.write(0xFF43, 0x00); // SCX
        memory.write(0xFF45, 0x00); // LYC
        memory.write(0xFF47, 0xFC); // BGP
        memory.write(0xFF48, 0xFF); // OBP0
        memory.write(0xFF49, 0xFF); // OBP1
        memory.write(0xFF4A, 0x00); // WY
        memory.write(0xFF4B, 0x00); // WX
        memory.write(0xFFFF, 0x00); // IE
        return;
    }
    void step() {
        uint8_t opcode = memory.read(cpu.PC);
        handle_instruction_metadata(opcode);  

        // Log registers and state
        printf("PC: %04X  A: %02X  F: %02X  B: %02X  C: %02X  D: %02X  E: %02X  H: %02X  L: %02X  SP: %04X  Opcode: %02X\n",
            cpu.PC, cpu.A, cpu.F, cpu.B, cpu.C, cpu.D, cpu.E, cpu.H, cpu.L, cpu.STACK_P, opcode);
    }
    void load_rom(const std::string& path) {
        std::ifstream rom(path, std::ios::binary);
        if (!rom.is_open()) {
            printf("Failed to open ROM\n");
            exit(1);
        }
        std::vector<uint8_t> rom_data((std::istreambuf_iterator<char>(rom)), {});
        for (size_t i = 0; i < rom_data.size() && i < 0x8000; i++) {
           memory.write(i, rom_data[i]);
       }
    }


    // ========================== MAIN ============================
   
    int main() {
        Intial_cpu_stage();
        std::ifstream f("opcodes.json");
        if (!f.is_open()) {
            printf("Failed to open opcodes.json\n");
            return 1;
        }

        data = json::parse(f);
        generate_rom_data();
        load_rom();
        cpu.PC = 0x00;

       
        while (true) {
            uint8_t opcode = memory.read(cpu.PC);
            
            printf("\nFetching opcode at PC=0x%04X: 0x%02X\n", cpu.PC, opcode);

           int inst_length = handle_instruction_metadata(opcode); 
           
            
            if (opcode == 0xCB) {
                
                 inst_length = 2;
            }
            if (!cpu.PC == inst_length) {
                cpu.PC += inst_length;
            }

            
            cpu.PC += inst_length;

            printf("PC after execution: 0x%04X\n", cpu.PC);

            // Handle IME delayed enabling
            if (cpu.IME_Pending) {
                cpu.IME = true;
                cpu.IME_Pending = false;
            }

            // Handle halt mode
            if (cpu.halted) {
                if (is_interrupt_pending()) {
                    cpu.halted = false;
                }
                else {
                    // HALT skips executing instructions, but clocks may still run
                    continue;
                }
            }

            // Service interrupts if any
            service_interrupts();

            // Add break condition to prevent infinite loop (optional)
            if (opcode == 0x76) { // HALT
                printf("CPU halted. Exiting loop.\n");
                break;
            }
            if (cpu.PC >= 0x8000)
                break;
        }
        return 0;
    }
