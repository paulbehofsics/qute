#ifndef parser_hh
#define parser_hh

#include <istream>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include "pcnf_container.hh"
#include "solver_types.hh"

namespace Qute {

inline bool isQCIRNameChar(char c) {
    return c == '_' || ('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
}

inline void skip_space(const std::string& str, size_t& idx) {
    while (idx < str.size() && std::isspace(str[idx])) {
        ++idx;
    }
}

class Parser {
    PCNFContainer& pcnf;
    bool use_model_generation;
    std::map<std::string, int32_t> qcir_var_conversion_map;
    int32_t nr_vars;
    uint32_t current_line;

    // helper methods
    char* uintToCharArray(uint32_t x);
    void addQCIRVars(const std::string& vars, char qtype);
    void pushQCIRVar(const std::string& var_name, char qtype, bool auxiliary);
    void addQCIRGate(std::string gate_name, uint32_t gate_type, std::vector<std::string>& inputs);

public:
    Parser(PCNFContainer& pcnf, bool use_model_generation): pcnf(pcnf), use_model_generation(use_model_generation) {}

    // IO methods
    auto& getline(std::istream& ifs, std::string& str);
    void readAUTO(std::istream& ifs = std::cin);
    void readQCIR(std::istream& ifs = std::cin);
    void readQDIMACS(std::istream& ifs = std::cin);
    void writeQDIMACS();

    inline void unexpected_char_error(const char c, size_t col) {
        std::cerr << "Error: Unexpected character '" << c << "' at line " << current_line << " column " << col << std::endl;
        exit(1);
    }

    inline void unexpected_eol_error() {
        std::cerr << "Error: Unexpected end of line at line " << current_line << std::endl;
        exit(1);
    }

    inline void empty_identifier_error() {
        std::cerr << "Error: Empty identifier at line " << current_line << std::endl;
        exit(1);
    }

    inline void unknown_identifier_error(const std::string& identifier) {
        std::cerr << "Error: Unknown identifier '" << identifier << "' at line " << current_line << std::endl;
        exit(1);
    }

    /* checks whether pos < str.size() and str[pos] is contained in the string chars,
     * and if not prints the required errors */
    inline void assert_string_has_char(const std::string& str, size_t pos, const std::string& chars) {
        if (pos >= str.size()) {
            unexpected_eol_error();
        } else if (chars.find(str[pos]) == std::string::npos) {
            unexpected_char_error(str[pos], pos+1);
        }
    } 

    inline std::string extract_next(const std::string& str, size_t& idx, const std::string& delimiters) {
        skip_space(str, idx);
        size_t begin = idx;
        size_t length = 0;
        while (idx < str.size() && delimiters.find(str[idx]) == std::string::npos) {
            if (isspace(str[idx])) {
                skip_space(str, idx);
                assert_string_has_char(str, idx, delimiters);
                break;
            } else if(isQCIRNameChar(str[idx])) {
                ++length;
                ++idx;
            } else {
                unexpected_char_error(str[idx], idx+1);
            }
        }
        if (idx == str.size()) {
            unexpected_eol_error();
        }
        if (length == 0) {
            empty_identifier_error();
        }
        //std::cerr << "extracted token: " << token << std::endl;
        return str.substr(begin, length);
    }

    inline std::string extract_lit(const std::string& str, size_t& idx) {
        skip_space(str, idx);
        size_t begin = idx;
        size_t length = 0;
        while (idx < str.size() && str[idx] != ',' && str[idx] != ')') {
            if (isspace(str[idx])) {
                skip_space(str, idx);
                assert_string_has_char(str, idx, ",)");
                break;
            } else if(isQCIRNameChar(str[idx])) {
                ++length;
                ++idx;
            } else if(str[idx] == '-') {
                if (length > 0) {
                    unexpected_char_error('-', idx+1);
                } else {
                    ++length;
                    ++idx;
                }
            } else {
                unexpected_char_error(str[idx], idx+1);
            }
        }
        if (idx == str.size()) {
            unexpected_eol_error();
        }
        if (str[begin] == '-' && length == 1) {
            std::cerr << "Error: Wild negation operator at line " << current_line << std::endl;
            exit(1);
        }
        if (length == 0 && str[idx] != ')') {
            unexpected_char_error(str[idx], idx+1);
        }
        //std::cerr << "extracted literal: " << token << std::endl;
        return str.substr(begin, length);
    }
};

}

#endif
