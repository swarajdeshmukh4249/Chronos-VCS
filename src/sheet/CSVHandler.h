#pragma once

#include <string>
#include <vector>

#include "sheet/Cell.h"
#include "sheet/Grid.h"

// Reads and writes sheets as CSV (RFC 4180), which Excel and Google Sheets open.
// Each field becomes a Cell, so "42" is a number and "=A1*2" is a formula.
class CSVHandler {
public:
    using Row = std::vector<std::string>;
    using Rows = std::vector<Row>;

    // The fields exactly as written, row by row, with quotes removed. Unlike parse,
    // nothing is interpreted, so "1.50" stays "1.50". Diff and merge use this so a
    // merge never rewrites cells nobody touched.
    static Rows parseRows(const std::string& csv);

    // Writes rows as they are (each keeps its own length), quoting where needed.
    static std::string formatRows(const Rows& rows, const std::string& lineEnd = "\r\n");

    // Accepts LF, CRLF or CR line endings and a UTF-8 byte order mark. Fields may be
    // quoted, with "" for a quote inside. A quote that is never closed runs to the end
    // of the text, as in Excel.
    static Grid<Cell> parse(const std::string& csv);

    // Writes every row and column up to the last one used, with CRLF line endings.
    // Formulas use Excel's spelling, so AVG is written as AVERAGE.
    static std::string format(const Grid<Cell>& grid);
};
