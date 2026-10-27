#include "sheet/CSVHandler.h"

#include <cctype>
#include <utility>

namespace {

bool endsField(char c) {
    return c == ',' || c == '\n' || c == '\r';
}

bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// Excel has no AVG function, so a saved =AVG(...) would show #NAME?. Renames AVG calls
// outside "text" literals; both spellings load back the same.
std::string excelSpelling(const std::string& formula) {
    std::string out;
    bool inText = false;
    for (std::size_t i = 0; i < formula.size();) {
        const char c = formula[i];
        if (c == '"') {
            inText = !inText;  // "" inside text toggles twice, so it stays inside
        } else if (!inText && isAlpha(c) && (i == 0 || !isAlnum(formula[i - 1]))) {
            std::size_t end = i;
            while (end < formula.size() && isAlpha(formula[end])) ++end;
            std::size_t next = end;
            while (next < formula.size() && std::isspace(static_cast<unsigned char>(formula[next]))) ++next;
            std::string name = formula.substr(i, end - i);
            std::string upper = name;
            for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            out += (upper == "AVG" && next < formula.size() && formula[next] == '(') ? "AVERAGE" : name;
            i = end;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

std::string quoteIfNeeded(const std::string& field) {
    if (field.find_first_of(",\"\r\n") == std::string::npos) {
        return field;
    }
    std::string quoted = "\"";
    for (char c : field) {
        quoted += c;
        if (c == '"') quoted += '"';
    }
    return quoted + "\"";
}

}  // namespace

CSVHandler::Rows CSVHandler::parseRows(const std::string& csv) {
    Rows rows;
    std::size_t pos = csv.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;
    if (pos == csv.size()) {
        return rows;
    }

    rows.emplace_back();
    while (true) {
        std::string field;
        if (pos < csv.size() && csv[pos] == '"') {
            ++pos;
            while (pos < csv.size()) {
                if (csv[pos] != '"') {
                    field += csv[pos++];
                } else if (pos + 1 < csv.size() && csv[pos + 1] == '"') {
                    field += '"';
                    pos += 2;
                } else {
                    ++pos;
                    break;
                }
            }
        }
        // Unquoted text, or anything after a closing quote, runs to the next separator.
        while (pos < csv.size() && !endsField(csv[pos])) {
            field += csv[pos++];
        }
        rows.back().push_back(std::move(field));

        if (pos == csv.size()) {
            break;
        }
        if (csv[pos] == ',') {
            ++pos;
            continue;
        }
        if (csv[pos] == '\r' && pos + 1 < csv.size() && csv[pos + 1] == '\n') {
            ++pos;
        }
        ++pos;
        if (pos == csv.size()) {
            break;  // the newline that ends the last row
        }
        rows.emplace_back();
    }
    return rows;
}

Grid<Cell> CSVHandler::parse(const std::string& csv) {
    Grid<Cell> grid;
    const Rows rows = parseRows(csv);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t col = 0; col < rows[row].size(); ++col) {
            if (!rows[row][col].empty()) {
                grid.set(CellRef{static_cast<int>(row), static_cast<int>(col)}, Cell(rows[row][col]));
            }
        }
    }
    return grid;
}

std::string CSVHandler::formatRows(const Rows& rows, const std::string& lineEnd) {
    std::string out;
    for (const Row& row : rows) {
        for (std::size_t col = 0; col < row.size(); ++col) {
            if (col > 0) {
                out += ',';
            }
            out += quoteIfNeeded(row[col]);
        }
        out += lineEnd;
    }
    return out;
}

std::string CSVHandler::format(const Grid<Cell>& grid) {
    std::string out;
    for (int row = 0; row < grid.rowCount(); ++row) {
        for (int col = 0; col < grid.colCount(); ++col) {
            if (col > 0) {
                out += ',';
            }
            const Cell& cell = grid.get(CellRef{row, col});
            out += quoteIfNeeded(cell.isFormula() ? excelSpelling(cell.getText()) : cell.getRaw());
        }
        out += "\r\n";
    }
    return out;
}
