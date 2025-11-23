#include <stdio.h>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cctype>
#include <iostream>
#include "memory.h"
#include "CPU.h"
#include "PPU.h"
#include "video.h"
#include <sstream>
#define SDL_MAIN_HANDLED

extern uint8_t framebuffer[144][160]; // match your global framebuffer

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}


#define MEMORY_SIZE 0x10000 // 64KB

using json = nlohmann::json;



// ======================= GLOBALS ==========================
 CPU cpu;
json data;

PPU ppu;
Memory memory;
bool is_interrupt_pending() {
    uint8_t IE = memory.read(0xFFFF);  
    uint8_t IF = memory.read(0xFF0F);  
    return (IE & IF & 0x1F) != 0;       
}
std::vector<uint8_t> rom_data;


bool load_rom(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open ROM file: " << filename << "\n";
        return false;
    }
    for (uint16_t addr = 0x0038; addr <= 0x003F; ++addr)
        memory.write(addr, 0xC9);  // RETI or use 0xC9 for RET


    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size == 0) {
        std::cerr << "ROM is empty!\n";
        return false;
    }

    std::vector<uint8_t> rom_data(size);
    if (!file.read(reinterpret_cast<char*>(rom_data.data()), size)) {
        std::cerr << "Failed to read ROM contents.\n";
        return false;
    }

    // Load up to 32KB max into memory (no MBC)
 
    for (size_t i = 0; i  < 0x8000; ++i) {
        memory.write(static_cast<uint16_t>(0x0000 + i), rom_data[i]);
    }

    // Print the last byte written:
    size_t last_offset = rom_data.size() - 0x7ffd;
    uint16_t last_addr = static_cast<uint16_t>(0x0000 + last_offset);
    uint8_t last_value = memory.read(last_addr);

    printf("Last ROM byte loaded at 0x%04X = 0x%02X (ROM size = %zu bytes)\n",
        last_addr, last_value, rom_data.size());



    printf("Loaded ROM: %s (%lld bytes)\n", filename.c_str(), size);
    printf("MBC type: 0x%02X\n", memory.read(0x0147));

    return true;
}

static void load_test_rom() {
    for (int i = 0; i < rom_data.size(); ++i) {
        memory.write(0x0100 +i, rom_data[i]);
        if (rom_data.empty()) {
            std::cerr << "ROM data is empty!\n";
            exit(1);
        }
        printf("Loaded rom_data 0x%02X at 0x%04X\n", rom_data[i], 0x0100 + i);
    }
    
}



     
bool enableIMEAfterNextInstruction = false;






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

uint8_t getFlagBytes(const std::vector<std::string>& flags, uint8_t currentF = cpu.F) {
    uint8_t f = currentF & 0xF0;

   
    if (flags.size() == 1 && flags[0] == "-") {
        return f;
    }

   
    if (flags.size() != 4) {
        return f; 
    }

    // Bit positions for flags: Z (7), N (6), H (5), C (4)
    const uint8_t flagBits[4] = { 0x80, 0x40, 0x20, 0x10 };

    for (size_t i = 0; i < 4; ++i) {
        const std::string& flag = flags[i];

        if (flag == "-" || flag == "") {
            continue;
        }
        else if (flag == "1" || flag == "Z" || flag == "N" || flag == "H" || flag == "C") {
            f |= flagBits[i];  // Set flag bit
        }
        else if (flag == "0") {
            f &= ~flagBits[i]; // Clear flag bit
        }
        
    }

    return f; 
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

            cpu.clock_cycles += 16;
          
        }
        else {
            val = resolve_register(operand1);

            uint8_t bit7 = (val & 0x80) >> 7;
            uint8_t result = ((val << 1) | bit7) & 0xFF;

            resolve_register(operand1)= result;

            
            cpu.F = 0;
            if (result == 0) cpu.F |= 0x80;  
            if (bit7)         cpu.F |= 0x10; 

            cpu.clock_cycles += 8;
            
        }
    }

    if (mnemonic == "INC" && operand1 == "BC") {
        uint16_t x1 = cpu.getBC();
        x1 = x1 + 1;
        cpu.setBC(x1);
        cpu.clock_cycles += cycles[0];
       
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "INC" && operand1 == "DE") {
        uint16_t x1 = cpu.getDE();
        x1 = x1 + 1;
        cpu.setDE(x1);
        cpu.clock_cycles += cycles[0];
     
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "INC" && operand1 == "SP") {
        cpu.STACK_P += 1;
        cpu.clock_cycles += cycles[0];
        
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), cpu.STACK_P);
    }
    else if (mnemonic == "INC" && operand1 == "(HL)") {
        uint16_t addr = cpu.getHL();
        uint8_t val = memory.read(addr);
        val += 1;
        memory.write( addr,val);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), val, cpu.F);
    }
    
    else if (mnemonic == "INC" && operand1 == "BC") {
        cpu.setBC(cpu.getBC() + 1);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), cpu.getBC(), cpu.F);
    }
    else if (mnemonic == "INC" && operand1 == "HL") {
        cpu.setHL(cpu.getHL() + 1);
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), cpu.getHL(), cpu.F);

    }
    else if (mnemonic == "INC") {
        uint8_t& x1 = resolve_register(operand1);
        x1 += 1;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), x1, cpu.F);

    }

    // --------------------- DEC ----------------------------------------------

   


    if (mnemonic == "DEC" && operand1 == "BC") {
        uint16_t x1 = cpu.getBC();
        x1 = x1 - 1;
        cpu.setBC(x1);       
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "DE") {
        uint16_t x1 = cpu.getDE();
        x1 = x1 - 1;
        cpu.setDE(x1);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "HL") {
        uint16_t x1 = cpu.getHL();
        x1 = x1 - 1;
        cpu.setHL(x1);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), x1);
    }
    else if (mnemonic == "DEC" && operand1 == "SP") {
        cpu.STACK_P -= 1;
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%04X\n", mnemonic.c_str(), operand1.c_str(), cpu.STACK_P);
    }
    else if (mnemonic == "DEC" && operand1 == "(HL)") {
        uint16_t addr = cpu.getHL();
        uint8_t val = memory.read(addr);
        uint8_t before = val;
        val -= 1;
        memory.write(addr, val);
        cpu.F &= 0x10;  // Preserve unused bits or unaffected Carry (optional)

        cpu.F = 0;
        if (val == 0) cpu.F |= 0x80;       // Z: Set if result is 0
        cpu.F |= 0x40;                    // N: Always set for DEC
        if ((before & 0x0F) == 0x00)      // H: Set if borrow from bit 4
            cpu.F |= 0x20;
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), val, cpu.F);
    }
    else if (mnemonic == "DEC") {
        uint8_t& x1 = resolve_register(operand1);
        uint8_t before = x1;
        x1 -= 1;

        cpu.F &= 0x10;  // Preserve unused bits or unaffected Carry (optional)

        cpu.F = 0;
        if (x1 == 0) cpu.F |= 0x80;       // Z: Set if result is 0
        cpu.F |= 0x40;                    // N: Always set for DEC
        if ((before & 0x0F) == 0x00)      // H: Set if borrow from bit 4
            cpu.F |= 0x20;

        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), x1, cpu.F);
    }
    


    if (mnemonic == "NOP") {
        
        cpu.clock_cycles += cycles[0];
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
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s (HL) → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), result, cpu.F);
    }
    else if  (mnemonic == "RRC") {
        uint8_t bit0 = resolve_register(operand1) & 0x01;
        uint8_t result = (resolve_register(operand1) >> 1) | (bit0 << 7);
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
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
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
    else if (mnemonic == "RL") {
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit7 = (resolve_register(operand1) & 0x80) >> 7;
        uint8_t result = (resolve_register(operand1) << 1) | oldCarry;
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
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
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
        
    }
    else if (mnemonic == "RR") {
        uint8_t oldCarry = cpu.getFlagC() ? 1 : 0;
        uint8_t bit0 = resolve_register(operand1) & 0x01;
        uint8_t result = (resolve_register(operand1) >> 1) | (oldCarry << 7);
        resolve_register(operand1) = result;
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
        // = length[0];
        printf("Executed: %s %s → 0x%02X, Flags = 0x%02X\n", mnemonic.c_str(), operand1.c_str(), result, cpu.F);
    }
    if (mnemonic == "JR" && operand1 =="NZ") {
        if (!(cpu.F & 0x80)) {
            int8_t offset = (int8_t)memory.read(cpu.PC+1);
            cpu.PC += 2 + offset;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2 ;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            // = length[0];
        }     
    } 
    else if (mnemonic == "JR" && operand1 == "NC") {
        if (!(cpu.F & 0x10)) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "Z" && operand2 =="r8") {
        if (cpu.F & 0x80) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2 ;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "C") {
        if (cpu.F & 0x10) {
            int8_t offset = (int8_t)memory.read(cpu.PC +1);
            cpu.PC += 2 + offset;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else {
            cpu.PC += 2 ;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            // = length[0];
        }
    }
    else if (mnemonic == "JR" && operand1 == "r8") {
        int8_t offset = (int8_t)memory.read(cpu.PC+1);
        cpu.PC += 2 + offset;
        cpu.pc_modified = true;
        cpu.clock_cycles += cycles[0];
        // = length[0];
    }
  
    if (mnemonic == "JP" && operand1 == "NZ") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);
        uint16_t addr = (hi << 8) | lo;

        if (!(cpu.F & 0x80)) { // If Zero flag is not set
            cpu.PC = addr;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0]; // Taken path
        }
        else {
            cpu.PC += 3;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1]; // Not taken
        }

        printf("Executed: JP NZ, 0x%04X -> PC=0x%04X\n", addr, cpu.PC);
    }

    if (mnemonic == "JP" && operand1 == "NC") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);
        uint16_t addr = (hi << 8) | lo;

        if (!(cpu.F & 0x10)) {
            cpu.PC = addr;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
           
            // = length[0];
        }
        else {
            cpu.PC += 3;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            
            // = length[0];
        }
    }
    if (mnemonic == "JP" && operand1 == "a16") {
        uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
        cpu.PC = addr;
        cpu.pc_modified = true;
        cpu.clock_cycles += cycles[0];
        
        // = length[0];
    }
    if (mnemonic == "JP" && operand1 == "Z") {
        if (cpu.F & 0x80){
            uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
            cpu.PC = addr;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
            
            // = length[0];
        }
        else {
            cpu.PC += 3;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];

            // = length[0];
        }
        
    }
    if (mnemonic == "JP" && operand1 == "C") {
        if (cpu.F & 0x10) {
            uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);
            cpu.PC = addr;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];
           
            // = length[0];

        }
        else {
            cpu.PC += 3;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[1];
            // = length[0];
        }

    }
    if (mnemonic == "JP" && operand1 == "HL") {
        cpu.PC = cpu.getHL();
        cpu.pc_modified = true;
        cpu.clock_cycles += cycles[0];
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
                cpu.clock_cycles += 4;
                // = length[0];
            }
        }
        else {  // After subtraction
            if (h) correction |= 0x06;
            if (c) correction |= 0x60;
            a -= correction;
            cpu.clock_cycles += 4;
            // = length[0];

        }

        if (!n) a += correction;

       
        cpu.F &= 0x40;               // Keep only N flag, clear Z, H, C temporarily
        if (a == 0) cpu.F |= 0x80;   // Set Z if result is zero
        if (correction & 0x60) cpu.F |= 0x10; // Set C if high nibble corrected
        cpu.F &= ~0x20;              // Always clear H after DAA

        cpu.A = a;
        cpu.clock_cycles += 4;
        // = length[0];
    }
    if (mnemonic == "SCF") {
        cpu.F = getFlagBytes(flags);
        cpu.clock_cycles += cycles[0];
    }

    // ------- ADD ----------------
    if (mnemonic == "ADD") {
        if (operand1 == "A" && operand2 == "(HL)") {
            uint16_t x1 = cpu.A + memory.read(cpu.getHL());
         cpu.A = x1 & 0xff;
         printf("Executed ADD A (HL) : 0x%02X\n", cpu.A);
         cpu.F = getFlagBytes(flags);
         cpu.clock_cycles += cycles[0];
         // = length[0];
        }
        else if (operand2 == "A") {
            uint16_t x1 = cpu.A + cpu.A;
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else if (operand2 == "B") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else if (operand2 == "C") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
            // = length[0];
        }
        else if (operand2 == "D") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
        }
        else if (operand2 == "E") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
        }
        else if (operand2 == "H") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
        }
        else if (operand2 == "L") {
            uint16_t x1 = cpu.A + resolve_register(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "d8") {
            uint16_t x1 = cpu.A + resolve_value(operand2);
            cpu.A = x1 & 0xff;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
        }
        if (operand1 == "i8") {
            int8_t offset = (int8_t)memory.read(cpu.PC + 1);
            uint16_t sp = cpu.STACK_P;              
            int32_t result = sp + offset;      
            cpu.STACK_P = result & 0xFFFF;
            cpu.F = getFlagBytes(flags);
            cpu.clock_cycles += cycles[0];
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
            cpu.clock_cycles += cycles[0];
           
        }
        if (operand1 == "SP"){
        uint16_t sp = cpu.STACK_P;
        int8_t imm = (int8_t)memory.read(cpu.PC + 1);
        uint16_t result = sp + imm;

        cpu.setFlagZ(false);
        cpu.setFlagN(false);
        cpu.setFlagH(((sp & 0xF) + (imm & 0xF)) > 0xF);
        cpu.setFlagC(((sp & 0xFF) + (imm & 0xFF)) > 0xFF);

        cpu.STACK_P = result;
        cpu.clock_cycles += cycles[0];  // Usually 16
        }

        
        

    }
    
   

   

// ---------------- ADC (Add with Carry) ----------------
   if (mnemonic == "ADC" && operand2 == "(HL)") {
        uint16_t addr = cpu.getHL();                // HL is the address
        uint8_t value = memory.read(addr);          // Read value from memory[HL]
        uint8_t carry = cpu.getFlagC() ? 1 : 0;

        uint16_t result = cpu.A + value + carry;

        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(((cpu.A & 0xF) + (value & 0xF) + carry) > 0xF);
        cpu.setFlagC(result > 0xFF);

        cpu.A = result & 0xFF;

        cpu.clock_cycles += cycles[0];  // Use metadata cycles
        printf("Executed: ADC A, (HL) -> 0x%02X\n", cpu.A);
   }
   else if (mnemonic == "ADC") {
        uint8_t value = resolve_value(operand2);
        uint8_t carry = cpu.getFlagC() ? 1 : 0;
        uint16_t result = cpu.A + value + carry;
        cpu.setFlagZ((result & 0xFF) == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(((cpu.A & 0xF) + (value & 0xF) + carry) > 0xF);
        cpu.setFlagC(result > 0xFF);
        cpu.A = result & 0xFF;
        cpu.clock_cycles += cycles[0];
        printf("Executed: ADC A, %s -> 0x%02X\n", operand2.c_str(), cpu.A);
   }
    


    // ---------------- SUB (Subtract) ----------------
    
   if (mnemonic == "SUB") {
       if (operand1 == "(HL)") {
           uint16_t x1 = cpu.A - memory.read(cpu.getHL());
           cpu.A = x1 & 0xff;
           printf("Executed SUB A (HL) : 0x%02X\n", cpu.A);
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
           // = length[0];
       }
       else if (operand1 == "A") {
           uint16_t x1 = cpu.A - cpu.A;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
           // = length[0];
       }
       else if (operand1 == "B") {
           uint16_t x1 = cpu.A - cpu.B;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
           // = length[0];
       }
       else if (operand1 == "C") {
           uint16_t x1 = cpu.A - cpu.C;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
           // = length[0];
       }
       else if (operand1 == "D") {
           uint16_t x1 = cpu.A - cpu.D;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "E") {
           uint16_t x1 = cpu.A - cpu.E;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "H") {
           uint16_t x1 = cpu.A - cpu.H;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "L") {
           uint16_t x1 = cpu.A - cpu.L;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "d8") {
           uint16_t x1 = cpu.A - memory.read(cpu.PC +1);
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
   }
    // ---------------- AND (Bitwise AND) ----------------
   
   if (mnemonic == "AND") {
       if (operand1 == "(HL)") {
           uint16_t x1 = cpu.A & memory.read(cpu.getHL());
           cpu.A = x1 & 0xFF;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "A") {
           uint16_t x1 = cpu.A & cpu.A;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "B") {
           uint16_t x1 = cpu.A & cpu.B;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "C") {
           uint16_t x1 = cpu.A & cpu.C;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "D") {
           uint16_t x1 = cpu.A & cpu.D;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "E") {
           uint16_t x1 = cpu.A & cpu.E;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "H") {
           uint16_t x1 = cpu.A & cpu.H;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "L") {
           uint16_t x1 = cpu.A & cpu.L;
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
       else if (operand1 == "d8") {
           uint16_t x1 = cpu.A & memory.read(cpu.PC + 1);
           cpu.A = x1 & 0xff;
           cpu.F = getFlagBytes(flags);
           cpu.clock_cycles += cycles[0];
       }
   }

    // ---------------- XOR (Bitwise XOR) ----------------
    
    else if (mnemonic == "XOR" && operand1 == "(HL)") {
        uint8_t value = memory.read(cpu.getHL());
        cpu.A ^= value;
        cpu.setFlagZ(cpu.A == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(false);
        cpu.clock_cycles += cycles[0];
        printf("Executed: XOR A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }
    else if (mnemonic == "XOR" && operand1 == "d8") {
       uint8_t value = memory.read(cpu.PC+1);
       cpu.A ^= value;
       cpu.setFlagZ(cpu.A == 0);
       cpu.setFlagN(false);
       cpu.setFlagH(false);
       cpu.setFlagC(false);
       cpu.clock_cycles += cycles[0];
       printf("Executed: XOR A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }
    else if (mnemonic == "XOR") {
        uint8_t value = resolve_register(operand1);
        cpu.A ^= value;
        cpu.setFlagZ(cpu.A == 0);
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(false);
        cpu.clock_cycles += cycles[0];
        printf("Executed: XOR A, %s -> 0x%02X\n", operand1.c_str(), cpu.A);
    }

    // ---------------- OR (Bitwise OR) ----------------

    else if (mnemonic == "OR") {
       uint8_t value = 0;

       if (operand1 == "A") {
           value = cpu.A;
       }
       else if (operand1 == "B" || operand1 == "C" || operand1 == "D" ||
           operand1 == "E" || operand1 == "H" || operand1 == "L") {
           value = resolve_register(operand1);
       }
       else if (operand1 == "(HL)") {
           value = memory.read(cpu.getHL());
       }
       else if (operand1 == "d8") {
           value = memory.read(cpu.PC++);
       }

       cpu.A |= value;
       cpu.F = 0;
       if (cpu.A == 0) cpu.F |= 0x80;
       cpu.clock_cycles += cycles[0];
   }


    // ---------------- CP (Compare) ----------------
   if (mnemonic == "CP") {

       if (operand1 == "(HL)") {
           uint8_t value = memory.read(cpu.getHL());
           uint16_t result = cpu.A - value;
           cpu.setFlagZ((result & 0xFF) == 0);
           cpu.setFlagN(true);
           cpu.setFlagH((cpu.A & 0xF) < (value & 0xF));
           cpu.setFlagC(cpu.A < value);
           cpu.clock_cycles += cycles[0];
           printf("Executed: CP A, %s\n", operand1.c_str());
       }
       else if (operand1 == "d8") {
           uint8_t value = memory.read(cpu.PC+1);
           uint8_t result = cpu.A - value;
           cpu.setFlagZ((result & 0xFF) == 0);
           cpu.setFlagN(true);
           cpu.setFlagH((cpu.A & 0xF) < (value & 0xF));
           cpu.setFlagC(cpu.A < value);
           cpu.clock_cycles += cycles[0];
           printf("Executed: CP A, %s\n", operand1.c_str());
           
           printf("A - 0x%02X, value - 0x%02X ,result - 0x%02X\n", cpu.A,value, result);
           printf("pc value is 0x%04X\n", cpu.PC +1);
       }
       else if (mnemonic == "CP") {
           uint8_t value = resolve_register(operand1);
           uint16_t result = cpu.A - value;
           cpu.setFlagZ((result & 0xFF) == 0);
           cpu.setFlagN(true);
           cpu.setFlagH((cpu.A & 0xF) < (value & 0xF));
           cpu.setFlagC(cpu.A < value);
           cpu.clock_cycles += cycles[0];
           printf("Executed: CP A, %s\n", operand1.c_str());
       }
   }
    

    // ---------------- SCF (Set Carry Flag) ----------------
    else if (mnemonic == "SCF") {
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(true);
        cpu.clock_cycles += cycles[0];
        printf("Executed: SCF\n");
    }

    // ---------------- CCF (Complement Carry Flag) ----------------
    else if (mnemonic == "CCF") {
        cpu.setFlagN(false);
        cpu.setFlagH(false);
        cpu.setFlagC(!cpu.getFlagC());
        cpu.clock_cycles += cycles[0];
        printf("Executed: CCF\n");
    }

    // ---------------- CPL (Complement A) ----------------
    else if (mnemonic == "CPL") {
        cpu.A = ~cpu.A;
        cpu.setFlagN(true);
        cpu.setFlagH(true);
        cpu.clock_cycles += cycles[0];
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
        cpu.clock_cycles += cycles[0];
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
        cpu.clock_cycles += cycles[0];
        printf("Executed: POP %s\n", operand1.c_str());
    }

    // ---------------- CALL (Call Subroutine) ----------------
     else if (mnemonic == "CALL" && operand1 == "a16") {
        // Read target address from the next 2 bytes after opcode
        uint16_t addr = memory.read(cpu.PC + 1) | (memory.read(cpu.PC + 2) << 8);

        // Push return address (PC + 3) to the stack
        cpu.STACK_P -= 2;
        memory.write(cpu.STACK_P, (cpu.PC + 3) & 0xFF);        // Low byte
        memory.write(cpu.STACK_P + 1, ((cpu.PC + 3) >> 8));    // High byte

        // Jump to address
        cpu.PC = addr;
        cpu.pc_modified = true;
        cpu.clock_cycles += cycles[0];

        printf("Executed: CALL 0x%04X → return address = 0x%04X\n", addr, cpu.PC);
        return;
     }
     else if (mnemonic == "CALL") {
        bool do_call = true;
        if (operand1 == "NZ") do_call = !(cpu.F & 0x80);
        else if (operand1 == "Z") do_call = cpu.F & 0x80;
        else if (operand1 == "NC") do_call = !(cpu.F & 0x10);
        else if (operand1 == "C") do_call = cpu.F & 0x10;

        if (operand1 == "NZ" || operand1 == "Z" || operand1 == "NC" || operand1 == "C") {
            if (!do_call) {
                cpu.PC += 3;
                cpu.clock_cycles += cycles[0];
                return;
            }
        }

        uint16_t addr = memory.read(cpu.PC + (operand2.empty() ? 1 : 2)) |
            (memory.read(cpu.PC + (operand2.empty() ? 2 : 3)) << 8);
        cpu.STACK_P -= 2;
        memory.write(cpu.STACK_P, (cpu.PC + (operand2.empty() ? 3 : 3)) & 0xFF);
        memory.write(cpu.STACK_P + 1, (cpu.PC + (operand2.empty() ? 3 : 3)) >> 8);
        cpu.PC = addr;
        cpu.pc_modified = true;
        cpu.clock_cycles += cycles[1];
        printf("Executed: CALL %s 0x%04X\n", operand1.c_str(), addr);
    }
   


    // ---------------- RET (Return from Subroutine) ----------------
    if (mnemonic == "RET") {
        bool do_return = true;

        // Check condition flags for conditional RET
        if (operand1 == "NZ") do_return = !(cpu.F & 0x80);
        else if (operand1 == "Z")  do_return = cpu.F & 0x80;
        else if (operand1 == "NC") do_return = !(cpu.F & 0x10);
        else if (operand1 == "C")  do_return = cpu.F & 0x10;

        // If condition not met, skip return
        if (!do_return && !operand1.empty()) {
            cpu.PC += 1;
            cpu.pc_modified = true;
            cpu.clock_cycles += cycles[0];  // cycles[0] typically for not taken
            printf("Skipped RET %s (condition not met)\n", operand1.c_str());
            return;
        }

        // Perform the return from stack
        uint8_t lo = memory.read(cpu.STACK_P);
        uint8_t hi = memory.read(cpu.STACK_P+1);
        cpu.STACK_P += 2;
        cpu.PC = (hi << 8) | lo;
        cpu.pc_modified = true;
        cpu.clock_cycles += operand1.empty() ? cycles[0] : cycles[1];  // cycles[1] if condition met
        printf("Executed: RET %s to 0x%04X\n", operand1.c_str(), cpu.PC);
    }


// ---------------- RST (Restart to fixed addr) ----------------
    else if (mnemonic == "RST") {
    uint16_t rst_val = 0;
    if (operand1 == "00H") rst_val = 0x00;
    else if (operand1 == "08H") rst_val = 0x0008;
    else if (operand1 == "10H") rst_val = 0x0010;
    else if (operand1 == "18H") rst_val = 0x0018;
    else if (operand1 == "20H") rst_val = 0x0020;
    else if (operand1 == "28H") rst_val = 0x0028;
    else if (operand1 == "30H") rst_val = 0x0030;
    else if (operand1 == "38H") rst_val = 0x0038;
    cpu.STACK_P -= 2;
    memory.write(cpu.STACK_P, cpu.PC & 0xFF);
    memory.write(cpu.STACK_P + 1, cpu.PC >> 8);
    cpu.PC = rst_val;
    cpu.pc_modified = true;
    cpu.clock_cycles += cycles[0];
    printf("Executed: RST %s\n", operand1.c_str());
    }
    
    // ---------------- HALT ----------------
     if (mnemonic == "HALT") {
         uint8_t IE = memory.read(0xFFFF);  // Interrupt Enable
         uint8_t IF = memory.read(0xFF0F);  // Interrupt Flag
         uint8_t pending = IE & IF;

         if (cpu.IME) {
             // IME is enabled: CPU halts until an interrupt fires and is serviced
             cpu.halted = true;
         }
         else {
             if (pending) {
                 // HALT bug: IME is disabled, but there is a pending interrupt
                 // CPU continues execution with next instruction (does NOT halt)
                 // (Optional: simulate HALT bug side-effect, but for now we just skip halt)
                 cpu.halt_bug = true;
                 cpu.halted = false;
             }
             else {
                 // No interrupts pending and IME is disabled: enter HALT
                 cpu.halted = true;
             }
         }

         cpu.clock_cycles += 4;
         
         
     }

        // ---------------- RETI (Return + Enable Interrupts) ----------------
    else if (mnemonic == "RETI") {
        uint8_t low = memory.read(cpu.STACK_P);
        uint8_t high = memory.read(cpu.STACK_P + 1);
        cpu.PC = (high << 8) | low;
        cpu.STACK_P += 2;

        cpu.IME = true;  // Enable interrupts after RETI
        cpu.clock_cycles += cycles[0];  // Add RETI cycles (usually 16)
        }


            // ---------------- STOP ----------------
    else if (mnemonic == "STOP") {
                // Would normally wait for button press or reset event
                printf("Executed: STOP (emulation would freeze here)\n");
                cpu.clock_cycles += cycles[0];
    }

                // ---------------- LDH (High RAM I/O) ----------------
    else if (mnemonic == "LDH") {
                   
                     if (operand1 == "A") {
                        uint8_t addr = memory.read(cpu.PC + 1);
                        uint8_t result= memory.read(0xFF00 + addr);
                        cpu.A = result;
                        printf("Executed: LDH A, (0xFF%02X) = 0x%02X\n", addr, result);
                     }
                     else if (operand1 == "(a8)") {
                        uint8_t val = cpu.A;
                        uint8_t addr = memory.read(cpu.PC + 1);
                        memory.write(0xFF00 + addr, val);
                        printf("Executed: LDH (0xFF%02X), A = 0x%02X\n", addr, val);
                     }
                    cpu.clock_cycles += cycles[0];
    }

    //------------------ LD HL, d16 ------------
    else if (mnemonic == "LD" && operand1 == "HL" && operand2 == "d16") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);

        // Combine into 16-bit value (little endian)
        uint16_t value = (hi << 8) | lo;
        cpu.setHL(value);
        cpu.clock_cycles += cycles[0];

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
        cpu.clock_cycles += cycles[0];
        printf("Executed: LD HL, SP+%d → 0x%04X\n", offset, result);
}

// ---------------- LD (C), A ----------------
    else if (mnemonic == "LD" && operand1 == "(C)" && operand2 == "A") {
        memory.write(0xFF00 + cpu.C, cpu.A);
        cpu.clock_cycles += cycles[0];
        printf("Executed: LD (0xFF%02X), A = 0x%02X\n", cpu.C, cpu.A);
        }

        // ---------------- LD A, (C) ----------------
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(C)") {
            cpu.A = memory.read(0xFF00 + cpu.C);
            cpu.clock_cycles += cycles[0];
            printf("Executed: LD A, (0xFF%02X) = 0x%02X\n", cpu.C, cpu.A);
            }

    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(a8)") {
        cpu.A = memory.read(0xFF00 + memory.read(cpu.PC + 1));
        cpu.clock_cycles += cycles[0];
        printf("Executed: LD (0xFF%02X), A = 0x%02X\n", cpu.C, cpu.A);
            }
    else if (mnemonic == "LD" && operand1 == "(a8)" && operand2 == "A") {
        memory.write(0xFF00 + memory.read(cpu.PC + 1), cpu.A);
        cpu.clock_cycles += cycles[0];
        printf("Executed: LD a8, (0xFF%02X) = 0x%02X\n", cpu.C, cpu.A);
    }

            // ---------------- LD SP, HL ----------------
    else if (mnemonic == "LD" && operand1 == "SP" && operand2 == "HL") {
                cpu.STACK_P = cpu.getHL();
                cpu.clock_cycles += cycles[0];
                printf("Executed: LD SP, HL = 0x%04X\n", cpu.STACK_P);
                }

                // ---------------- LD (a16), A ----------------
    else if (mnemonic == "LD" && operand1 == "(a16)" && operand2 == "A") {
                    uint8_t lo = memory.read(cpu.PC + 1);
                    uint8_t hi = memory.read(cpu.PC + 2);
                    uint16_t addr = (hi << 8) | lo;
                    memory.write(addr, cpu.A);
                    cpu.clock_cycles += cycles[0];
                    printf("Executed: LD (0x%04X), A = 0x%02X\n", addr, cpu.A);
                    }

                    // ---------------- LD A, (a16) ----------------
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(a16)") {
                        uint8_t lo = memory.read(cpu.PC + 1);
                        uint8_t hi = memory.read(cpu.PC + 2);
                        uint16_t addr = (hi << 8) | lo;
                        cpu.A = memory.read(addr);
                        cpu.clock_cycles += cycles[0];
                        printf("Executed: LD A, (0x%04X) = 0x%02X\n", addr, cpu.A);
    }

    // ------------------ LD (U16) , SP -------------------------
    else if (mnemonic == "LD" && operand1 == "(a16)" && operand2 == "SP") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);
        uint16_t addr = (hi << 8) | lo;

        // Write SP in little-endian format
        memory.write(addr, cpu.STACK_P & 0xFF);        // lower byte
        memory.write(addr + 1, (cpu.STACK_P >> 8));    // upper byte

        cpu.clock_cycles += 20;  // This instruction takes 20 cycles
    }
    // -------------------------- LD SP U16 ------------------------
    else if (mnemonic == "LD" && operand1 == "SP" && operand2 == "d16") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);

        // Combine into 16-bit value (little endian)
        uint16_t value = (hi << 8) | lo;

        // Load into SP register
        cpu.STACK_P = value;

        

        // Add cycles
        cpu.clock_cycles += 12;
    }
     // ---------------- LD remaing -------------------------------

    else if (mnemonic == "LD" && operand1 == "BC" && operand2 == "d16") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);
        uint16_t x1 = (hi << 8) | lo;
        cpu.setBC(x1);
        printf("Executed: LD BC = 0x%04X\n", cpu.getBC());
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "DE" && operand2 == "d16") {
        uint8_t lo = memory.read(cpu.PC + 1);
        uint8_t hi = memory.read(cpu.PC + 2);

        // Combine into 16-bit value (little endian)
        uint16_t value = (hi << 8) | lo;
        cpu.setDE(value);
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "DE")
    {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.setBC(x1);
        printf("Executed: LD DE = 0x%04X\n", cpu.getDE());
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "HL") {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.setBC(x1);
        printf("Executed: LD HL = 0x%04X\n", cpu.getHL());
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "SP" && operand2 == "d16") {
        uint16_t x1 = memory.read(cpu.PC + 1);
        cpu.STACK_P =x1;
        printf("Executed: LD BC = 0x%04X\n", cpu.getBC());
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "d8") {
        uint8_t imm = memory.read(cpu.PC + 1);
        cpu.A = imm;
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "B") {
        if (operand2 == "B") { cpu.B = cpu.B; cpu.clock_cycles += cycles[0];}
        if (operand2 == "C") { cpu.B = cpu.C;  cpu.clock_cycles += cycles[0];}
        if (operand2 == "D") { cpu.B = cpu.D; cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "E") { cpu.B = cpu.E; cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "H") { cpu.B = cpu.H; cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "L") { cpu.B = cpu.L; cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "A") { cpu.B = cpu.A; cpu.clock_cycles += cycles[0];
        }
        if (operand2 == "(HL)") { cpu.B = memory.read(cpu.getHL()); cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.B = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    else if (mnemonic == "LD" && operand1 == "C") {
        if (operand2 == "(HL)") {
            cpu.C = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.C = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    
    else if (mnemonic == "LD" && operand1 == "D") {
        if (operand2 == "(HL)") {
            cpu.D = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.D = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    else if (mnemonic == "LD" && operand1 == "E") {
        if (operand2 == "(HL)") {
            cpu.E = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.E = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    else if (mnemonic == "LD" && operand1 == "H") {
        if (operand2 == "(HL)") {
            cpu.H = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.H = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    else if (mnemonic == "LD" && operand1 == "L") {
        if (operand2 == "(HL)") {
            cpu.L = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.L = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
        }
    }
    else if (mnemonic == "LD" && operand1 == "(HL)") {
        memory.write(cpu.getHL(), resolve_value(operand2));
        cpu.clock_cycles += cycles[0];
    }

    else if (mnemonic == "LD" && operand2 == "A") {
        if (operand1 == "(BC)") { memory.write(cpu.getBC(), cpu.A); cpu.clock_cycles += cycles[0];
        }
        if (operand1 == "(DE)") { memory.write(cpu.getDE(), cpu.A); cpu.clock_cycles += cycles[0];
        }
        if (operand1 == "(HL+)") {
            uint16_t addr = cpu.getHL();
            memory.write(addr, cpu.A);
            cpu.setHL(addr + 1);

            cpu.clock_cycles += 8;
        }
        if (operand1 == "(HL-)") {
            uint16_t addr = cpu.getHL();
            memory.write(addr, cpu.A);
            cpu.setHL(addr - 1);

            cpu.clock_cycles += 8;
        }
      


    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(BC)") {
        uint16_t addr = cpu.getBC();
        cpu.A = memory.read(addr);
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(DE)") {
        uint16_t addr = cpu.getDE();
        cpu.A = memory.read(addr);
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(HL+)") {
        uint16_t addr = cpu.getBC();
        cpu.A = ((memory.read(addr + 1)) & 0xff);
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A" && operand2 == "(HL-)") {
        uint16_t addr = cpu.getBC();
        cpu.A = ((memory.read(addr - 1)) & 0xff);
        cpu.clock_cycles += cycles[0];
    }
    else if (mnemonic == "LD" && operand1 == "A") {
        if (operand2 == "(HL)") {
            cpu.A = memory.read(cpu.getHL());
            cpu.clock_cycles += cycles[0];
        }
        else {
            cpu.A = resolve_value(operand2);
            cpu.clock_cycles += cycles[0];
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

            cpu.clock_cycles += 12;

        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t val = resolve_value(operand2);
            cpu.setFlagZ(!(val & (1 << bit)));
            cpu.setFlagN(false);
            cpu.setFlagH(true);
            cpu.clock_cycles += cycles[0];
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

            cpu.clock_cycles += 16;
        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t& reg = resolve_register(operand2);
            reg &= ~(1 << bit);
            cpu.clock_cycles += cycles[0];
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

            cpu.clock_cycles += 16;
        }
        else {
            uint8_t bit = std::stoi(operand1);
            uint8_t& reg = resolve_register(operand2);
            reg |= (1 << bit);
            cpu.clock_cycles += cycles[0];
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

            cpu.clock_cycles += 16;

        }
        else {
            uint8_t& reg = resolve_register(operand1);
            reg = ((reg & 0xF) << 4) | ((reg & 0xF0) >> 4);
            cpu.setFlagZ(reg == 0);
            cpu.setFlagN(false);
            cpu.setFlagH(false);
            cpu.setFlagC(false);
            cpu.clock_cycles += cycles[0];
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

                cpu.clock_cycles += 16;
            }

            else {
                uint8_t& reg = resolve_register(operand1);
                cpu.setFlagC((reg & 0x80) != 0);
                reg <<= 1;
                cpu.setFlagZ(reg == 0);
                cpu.setFlagN(false);
                cpu.setFlagH(false);
                cpu.clock_cycles += cycles[0];
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

                    cpu.clock_cycles += 16;
                }
                else {
                    uint8_t& reg = resolve_register(operand1);
                    cpu.setFlagC(reg & 0x01);
                    reg = (reg >> 1) | (reg & 0x80);
                    cpu.setFlagZ(reg == 0);
                    cpu.setFlagN(false);
                    cpu.setFlagH(false);
                    cpu.clock_cycles += cycles[0];
                    printf("Executed: SRA %s -> 0x%02X\n", operand1.c_str(), reg);
                }
     }
    // ---------------- SBC (Subtract with Carry) ----------------
     else if (mnemonic == "SBC") {
         if (operand2 == "(HL)") {
             uint8_t value = memory.read(cpu.getHL());
             uint8_t carry = cpu.getFlagC() ? 1 : 0;
             uint16_t result = cpu.A - value - carry;
             cpu.setFlagZ((result & 0xFF) == 0);
             cpu.setFlagN(true);
             cpu.setFlagH(((cpu.A & 0xF) - (value & 0xF) - carry) < 0);
             cpu.setFlagC(cpu.A < (value + carry));
             cpu.A = result & 0xFF;
             cpu.clock_cycles += cycles[0];
             // = length[0];
             printf("Executed: SBC A, %s -> 0x%02X\n", operand2.c_str(), cpu.A);
         }
         else {
             uint8_t value = resolve_value(operand2);
             uint8_t carry = cpu.getFlagC() ? 1 : 0;
             uint16_t result = cpu.A - value - carry;
             cpu.setFlagZ((result & 0xFF) == 0);
             cpu.setFlagN(true);
             cpu.setFlagH(((cpu.A & 0xF) - (value & 0xF) - carry) < 0);
             cpu.setFlagC(cpu.A < (value + carry));
             cpu.A = result & 0xFF;
             cpu.clock_cycles += cycles[0];
             // = length[0];
             printf("Executed: SBC A, %s -> 0x%02X\n", operand2.c_str(), cpu.A);
         }
     }

        // ---------------- DI (Disable Interrupts) ----------------
    else if (mnemonic == "DI") {
            cpu.IME = false;
            printf("Executed: DI (Interrupts disabled)\n");
            cpu.clock_cycles += cycles[0];
            }

            // ---------------- EI (Enable Interrupts) ----------------
    else if (mnemonic == "EI") {
         enableIMEAfterNextInstruction = true; 
        cpu.justExecutedEI = true;

        printf("Executed: EI (Interrupts enabled) , enableIMEAfterNextInstruction = %d\n", enableIMEAfterNextInstruction);
        cpu.clock_cycles += cycles[0];
    }
    // ----------- RLCA ----------------
    else if (mnemonic == "RLCA") {
        uint8_t bit7 = (cpu.A & 0x80) >> 7;
        cpu.A = ((cpu.A << 1) | bit7) & 0xFF;

        cpu.F = 0;  
        if (bit7) cpu.F |= 0x10;  
        printf(" Executed RLCA : 0x%02X\n", cpu.A);
        cpu.clock_cycles += cycles[0];
        
    }
    // ------------- RRCA --------------------
    else if (mnemonic == "RRCA") {
        uint8_t bit0 = cpu.A & 0x01;
        cpu.A = ((bit0 << 7) | (cpu.A >> 1)) & 0xFF;

        cpu.F = 0;  
        if (bit0) cpu.F |= 0x10;  
        printf("Executed RRCA cpu.A : 0x%02X\n", cpu.A);
        cpu.clock_cycles += cycles[0];
        
    }
    //---------------RLA -------------------
    else if (mnemonic == "RLA") {
        uint8_t old_carry = (cpu.F & 0x10) ? 1 : 0;
        uint8_t bit7 = (cpu.A & 0x80) >> 7;

        cpu.A = ((cpu.A << 1) | old_carry) & 0xFF;
        printf("Executed RLA cpu.A : 0x%02X\n", cpu.A);
        cpu.F = 0;  
        if (bit7) cpu.F |= 0x10;  
        cpu.clock_cycles += cycles[0];
    }
    // ------------- RRA -------------------
    else if (mnemonic == "RRA") {
        uint8_t old_carry = (cpu.F & 0x10) ? 1 : 0;
        uint8_t bit0 = cpu.A & 0x01;

        cpu.A = ((old_carry << 7) | (cpu.A >> 1)) & 0xFF;
        printf("Executed RRA cpu.A : 0x%02X\n", cpu.A);
        if (bit0) cpu.F |= 0x10;  

        cpu.clock_cycles += cycles[0];

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
                length = info["length"][0].get<std::vector<int>>();
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
            int inst_length = length[0];
            // int clock_cycles = cycles[0];
            execute_instruction(mnemonic, operand1, operand2, flags, cycles);
            return inst_length; //clock_cycles//
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
        if (info.contains("length") && info["length"].is_array()) {
            length = info["length"][0].get<std::vector<int>>();
        }
        else {
            length = { info["length"].get<int>() };
        }

        // Handle "cycles" as either array or single int
        std::vector<int> cycles;
        if (info.contains("cycles")) {
            if (info["cycles"].is_array()) {
                cycles = info["cycles"].get<std::vector<int>>();
            }
            else {
                cycles = { info["cycles"].get<int>() };
            }
        }

        if (!flags.empty())
            cpu.F = getFlagBytes(flags);
        int inst_length = length[0];
        //int clock_cycles = cycles[0];

        execute_instruction(mnemonic, operand1, operand2, flags, cycles);
        return  inst_length;

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
                static const uint16_t vector[5] = { 0x0040, 0x0048, 0x0050, 0x0058, 0x0060 };
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
        cpu.PC = 0x0100;

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

    static void load_logo_to_vram() {
        const uint8_t nintendo_logo[400] = {
            
            0xF0, 0x00, 0xF0, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0xF3, 0x00, 0xF3, 0x00,
            0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00,
            0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x00, 0xF3, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCF, 0x00, 0xCF, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x0F, 0x00, 0x0F, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x0F, 0x00, 0x0F, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0xF0, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x00, 0xF3, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0xC0, 0x00,
            0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xC0, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xC3, 0x00, 0xC3, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0xFC, 0x00,
            0xF3, 0x00, 0xF3, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00,
            0x3C, 0x00, 0x3C, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0x3C, 0x00, 0x3C, 0x00,
            0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00,
            0xF3, 0x00, 0xF3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00,
            0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00,
            0x3C, 0x00, 0x3C, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x0F, 0x00, 0x0F, 0x00,
            0x3C, 0x00, 0x3C, 0x00, 0xFC, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0xFC, 0x00,
            0xFC, 0x00, 0xFC, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00,
            0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF3, 0x00, 0xF0, 0x00, 0xF0, 0x00,
            0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xC3, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xCF, 0x00, 0xC3, 0x00, 0xC3, 0x00,
            0x0F, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0xFC, 0x00, 0xFC, 0x00,
            0x3C, 0x00, 0x42, 0x00, 0xB9, 0x00, 0xA5, 0x00, 0xB9, 0x00, 0xA5, 0x00, 0x42, 0x00, 0x3C, 0x00,


        };

        for (int i = 0; i < 400; ++i)
            memory.write(0x8010 + i, nintendo_logo[i]); // 0x8010 is where boot ROM writes it
    }

    void log() {
        uint8_t opcode = memory.read(cpu.PC);
       
         

        // Log registers and state
        printf("PC: %04X  A: %02X  F: %02X  B: %02X  C: %02X  D: %02X  E: %02X  H: %02X  L: %02X  SP: %04X  Opcode: %02X  IE: %02X  IF: %02X\n",
            cpu.PC, cpu.A, cpu.F, cpu.B, cpu.C, cpu.D, cpu.E, cpu.H, cpu.L, cpu.STACK_P, opcode,memory.read(0xFFFF), memory.read(0xFF0F));
        printf(" IME: %d | HALTED: %d\n", cpu.IME, cpu.halted);
        printf(" Pending: %d\n", (memory.read(0xFFFF) & memory.read(0xFF0F)) != 0);
        printf("ff44, LY = %04X\n ", memory.read(0xff44));
        printf("STAT = %0X\n", memory.read(0xff41));
        printf("lcd on = %0X\n", (memory.read(0xFF40) & 0x80));
    }
  
    void write_tile(uint16_t addr, const uint8_t pixel[8]) {
        for (int i = 0; i < 8; ++i) {
            uint8_t low = 0, high = 0;
            for (int bit = 0; bit < 8; ++bit) {
                uint8_t color = (pixel[i] >> (7 - bit)) & 0x01;
                low |= (color & 0x01) << (7 - bit);  // only 1-bit (black/white)
            }
            memory.write(addr + i * 2, low);
            memory.write(addr + i * 2 + 1, 0x00);  // no high bits → 2-color
        }
    }

    void render_custom_logo() {
        // Define pixel data for 'G', 'B'
        uint8_t tile_G[8] = {
            0b01111100,
            0b10000010,
            0b10000000,
            0b10011110,
            0b10000010,
            0b10000010,
            0b10000010,
            0b01111100
        };

        uint8_t tile_B[8] = {
            0b11111100,
            0b10000010,
            0b10000010,
            0b11111100,
            0b10000010,
            0b10000010,
            0b10000010,
            0b11111100
        };

        // Add more letters if you want: E, M, U, etc.

        // Write tiles to VRAM at 0x8000, 0x8010...
        write_tile(0x8000, tile_G);  // tile 0
        write_tile(0x8010, tile_B);  // tile 1

        // Fill BG tilemap to display "GB" at top-left
        memory.write(0x9800 + 0, 0x00);  // G
        memory.write(0x9800 + 1, 0x01);  // B

        // Set LCD registers
        memory.write(0xFF40, 0x91);  // LCDC: LCD on, BG on
        memory.write(0xFF42, 0x00);  // SCY
        memory.write(0xFF43, 0x00);  // SCX
        memory.write(0xFF47, 0xE4);  // BGP: 11 10 01 00

        std::cout << "Custom GB logo rendered.\n";
    }
    static void init_fake_bios_state() {
        // CPU Registers
        cpu.A = 0x01; cpu.F = 0xB0;
        printf("A & F are set as 0x%02X, 0x%02X\n", cpu.A, cpu.F);
        cpu.setBC(0x0013);
        cpu.setDE(0x00D8);
        
        cpu.STACK_P = 0xFFFE;
        cpu.PC = 0x0100;     // start of your ROM

        cpu.setHL(0x014D);

        cpu.PC = 0x0100;
        cpu.STACK_P = 0xFFFE;

        // Timers
        memory.write(0xFF05, 0x00);
        memory.write(0xFF06, 0x00);
        memory.write(0xFF07, 0x00);

        // Sound
        memory.write(0xFF10, 0x80);
        memory.write(0xFF11, 0xBF);
        memory.write(0xFF12, 0xF3);
        memory.write(0xFF14, 0xBF);
        memory.write(0xFF16, 0x3F);
        memory.write(0xFF17, 0x00);
        memory.write(0xFF19, 0xBF);
        memory.write(0xFF1A, 0x7F);
        memory.write(0xFF1B, 0xFF);
        memory.write(0xFF1C, 0x9F);
        memory.write(0xFF1E, 0xBF);
        memory.write(0xFF20, 0xFF);
        memory.write(0xFF21, 0x00);
        memory.write(0xFF22, 0x00);
        memory.write(0xFF23, 0xBF);
        memory.write(0xFF24, 0x77);
        memory.write(0xFF25, 0xF3);
        memory.write(0xFF26, 0xF1);

        // PPU
        memory.write(0xFF40, 0x91); // LCDC
        memory.write(0xFF42, 0x00); // SCY
        memory.write(0xFF43, 0x20); // SCX // offset after tunning
        memory.write(0xFF45, 0x00); // LYC
        memory.write(0xFF47, 0xFC); // BGP
        memory.write(0xFF48, 0xFF); // OBP0
        memory.write(0xFF49, 0xFF); // OBP1
        memory.write(0xFF4A, 0x00); // WY
        memory.write(0xFF4B, 0x00); // WX

        // Interrupts
        memory.write(0xFFFF, 0x00); // IE
        memory.write(0xFF0F, 0x00); // IF

      
    }
    void fake_load_tile_map() {
        const uint8_t nintendo_map[48] = {
            // Use the correct Nintendo logo bytes from official boot ROM (optional)
             0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
             0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,

        };

        for (int i = 0; i < 48; ++i)
            memory.write(0x9904 + i, nintendo_map[i]); // 0x8010 is where boot ROM writes it
    }
      



    

    // ========================== MAIN ============================
   
    int main() {
        memory.set_allow_rom_write(true);
        init_fake_bios_state();
        int netcycles = 0;
        memset(framebuffer, 0, sizeof(framebuffer));
        load_logo_to_vram();
        fake_load_tile_map();

       
       

        std::ifstream f("opcodes.json");
        if (!f.is_open()) {
            printf("Failed to open opcodes.json\n");
            return 1;
        }
        bool enter_examine = false;
      
        bool imeEnablePending = false;

        bool lcd_on = (memory.read(0xFF40) & 0x80);
        bool prev_lcd_on = false;
        

        init_video();

        data = json::parse(f);
        if (!load_rom("bgbtest.gb")) {
            return 1;
        }

        memory.write(0xFF47, 0xE4);
        memory.write(0x0039, 0x00);
        memory.write(0x003A, 0x00);
        memory.write(0x003B, 0x00);
        memory.write(0x003C, 0x00);
        memory.write(0x003D, 0x00);
        memory.write(0x003E, 0x00);
        memory.write(0x003F, 0x00);
        memory.set_allow_rom_write(false);

        bool running = true;
        SDL_Event e;

        while (running) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    running = false;
                }
            }


            // Handle HALT
            uint8_t IE = memory.read(0xFFFF);
            uint8_t IF = memory.read(0xFF0F);
            uint8_t pending = IE & IF;

            if (cpu.halted) {
               

                if (!cpu.IME && pending) {
                    cpu.halt_bug = true;
                    cpu.halted = false;
                }
                else if (cpu.IME && pending) {
                    cpu.halted = false;
                }
                else {
                    cpu.clock_cycles += 4;
                    ppu.step(4);
                    continue;
                }
            }

            if (enableIMEAfterNextInstruction) {

                imeEnablePending = true;
                enableIMEAfterNextInstruction = false;
            }
            else if (imeEnablePending && !cpu.justExecutedEI) {
                cpu.IME = true;
               
                imeEnablePending = false;
            }


            uint16_t old_pc = cpu.PC;
            uint8_t opcode;

            if (cpu.halt_bug) {
                opcode = memory.read(cpu.PC);  // Fetch same instruction again
                cpu.halt_bug = false;
            }
            else {
                opcode = memory.read(cpu.PC);
            }

          
            


            cpu.pc_modified = false;
            printf("\nFetching opcode at PC=0x%04X: 0x%02X\n", cpu.PC, opcode);

            int inst_length = handle_instruction_metadata(opcode);

            printf("F = 0x%02X | Z=%d N=%d H=%d C=%d\n",
                cpu.F,
                (cpu.F & 0x80) != 0,
                (cpu.F & 0x40) != 0,
                (cpu.F & 0x20) != 0,
                (cpu.F & 0x10) != 0);

            printf("cycles - %d\n", cpu.clock_cycles);
           

            cpu.last_opcode = opcode;



           
            

            if (opcode == 0xCB) inst_length = 2;
            
            uint8_t lcdc = memory.read(0xFF40);

            if(!prev_lcd_on && lcd_on) {
                printf("LCD TURNED ON\n");

                if (ppu.ppu_clock > 0) {
                    memory.write(0xFF44, 1);
                    ppu.scanline = 1;
                }
                else {
                    memory.write(0xFF44, 0);
                    ppu.scanline = 0;
                }

                ppu.ppu_clock = 0;
                ppu.mode = 2;
             }

            // Detect LCD turned OFF
            if (prev_lcd_on && !lcd_on) {
                printf("LCD TURNED OFF\n");

                memory.write(0xFF44, 0);
                memory.write(0xFF41, memory.read(0xFF41) & 0xFC);  // mode = 0
                ppu.ppu_clock = 0;
                ppu.scanline = 0;
                ppu.mode = 0;
            }

            prev_lcd_on = lcd_on;



            if (lcd_on) {
                ppu.step(cpu.clock_cycles);
            }
            else {
                // LCD is off → reset LY and PPU state.
                memory.write(0xFF44, 0x00); // LY = 0
                memory.write(0xFF41, memory.read(0xFF41) & 0xFC); // STAT mode = 0 (HBlank).
                ppu.ppu_clock = 0;
                ppu.scanline = 0;
                ppu.mode = 0;
                printf("entered here 33------\n");
            }

            cpu.clock_cycles = 0;

            cpu.justExecutedEI = false;

            // Moved this OUTSIDE the above if block — so it always checks for interrupts:
            IE = memory.read(0xFFFF);
            IF = memory.read(0xFF0F);
            pending = IE & IF;


            if (cpu.IME && pending) {
               
                
                cpu.STACK_P -= 2;
                memory.write(cpu.STACK_P, cpu.PC & 0xFF);
                memory.write(cpu.STACK_P + 1, cpu.PC >> 8);

                if (pending & 0x01) {
                    memory.write(0xFF0F, IF & ~0x01);
                    cpu.PC = 0x0040;
                    printf("Opcode at 0x0040 = 0x%02X\n", memory.read(0x0040));

                }
                else if (pending & 0x02) {
                    memory.write(0xFF0F, IF & ~0x02);
                    cpu.PC = 0x0048;

                }
                else if (pending & 0x04) {
                    memory.write(0xFF0F, IF & ~0x04);
                    cpu.PC = 0x0050;
                }
                else if (pending & 0x08) {
                    memory.write(0xFF0F, IF & ~0x08);
                    cpu.PC = 0x0058;
                }
                else if (pending & 0x10) {
                    memory.write(0xFF0F, IF & ~0x10);
                    cpu.PC = 0x0060;
                }
                cpu.pc_modified = true;
                cpu.clock_cycles += 20;
                cpu.IME = false;
            }
            printf("IME: %d | HALTED: %d | IE: 0x%02X | IF: 0x%02X | PENDING: 0x%02X\n",
                cpu.IME, cpu.halted, memory.read(0xFFFF), memory.read(0xFF0F),
                memory.read(0xFFFF) & memory.read(0xFF0F));

            if (!enter_examine && cpu.PC == 0x01AD) {
                enter_examine = true;
                getchar();
            }

            if (enter_examine) {
                getchar();
            }



            if (!cpu.pc_modified &&  !cpu.halted) {
                cpu.PC += inst_length;
                printf("Step: PC=0x%04X Opcode=0x%02X Next PC=0x%04X\n", old_pc, opcode, cpu.PC);
            }

           
            log();
           

        }
        cleanup_video();
            return 0;
    }
    
