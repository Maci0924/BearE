#include "editor.hpp"

HydroTextPosition::HydroTextPosition()
    : line(0), column(0) {
}

HydroTextPosition::HydroTextPosition(
    int lineValue,
    int columnValue
)
    : line(lineValue), column(columnValue) {
}

HydroEditor::HydroEditor()
    : lines(),
      cursorLine(0),
      cursorColumn(0),
      firstVisibleLine(0),
      firstVisibleColumn(0),
      modified(false),
      selectionActive(false),
      selectionAnchor(),
      selectionCaret() {
    lines.push_back("");
}

void HydroEditor::ensureDocument() {
    if (lines.empty()) {
        lines.push_back("");
    }
}

void HydroEditor::clampCursor() {
    ensureDocument();

    cursorLine = hydroClampInt(
        cursorLine,
        0,
        static_cast<int>(lines.size()) - 1
    );

    cursorColumn = hydroClampInt(
        cursorColumn,
        0,
        static_cast<int>(lines[cursorLine].size())
    );
}

HydroTextPosition HydroEditor::clampPosition(
    const HydroTextPosition& position
) const {
    HydroTextPosition result = position;

    if (lines.empty()) {
        result.line = 0;
        result.column = 0;
        return result;
    }

    result.line = hydroClampInt(
        result.line,
        0,
        static_cast<int>(lines.size()) - 1
    );

    result.column = hydroClampInt(
        result.column,
        0,
        static_cast<int>(lines[result.line].size())
    );

    return result;
}

bool HydroEditor::positionLess(
    const HydroTextPosition& left,
    const HydroTextPosition& right
) {
    if (left.line != right.line) {
        return left.line < right.line;
    }

    return left.column < right.column;
}

void HydroEditor::prepareSelection(bool selecting) {
    if (selecting) {
        if (!selectionActive) {
            beginSelection();
        }
    }
    else {
        clearSelection();
    }
}

bool HydroEditor::loadFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);

    if (!input.is_open()) {
        return false;
    }

    lines.clear();

    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }

        lines.push_back(line);
    }

    ensureDocument();

    cursorLine = 0;
    cursorColumn = 0;
    firstVisibleLine = 0;
    firstVisibleColumn = 0;
    modified = false;
    clearSelection();

    return true;
}

bool HydroEditor::saveFile(const std::string& path) const {
    std::ofstream output(path.c_str(), std::ios::binary);

    if (!output.is_open()) {
        return false;
    }

    std::size_t index;

    for (index = 0; index < lines.size(); index++) {
        output << lines[index];

        if (index + 1 < lines.size()) {
            output << "\r\n";
        }
    }

    return true;
}

void HydroEditor::newDocument() {
    lines.clear();
    lines.push_back("");

    cursorLine = 0;
    cursorColumn = 0;
    firstVisibleLine = 0;
    firstVisibleColumn = 0;
    modified = false;
    clearSelection();
}

std::string HydroEditor::getText() const {
    std::stringstream output;
    std::size_t index;

    for (index = 0; index < lines.size(); index++) {
        output << lines[index];

        if (index + 1 < lines.size()) {
            output << "\n";
        }
    }

    return output.str();
}

const std::vector<std::string>&
HydroEditor::getLines() const {
    return lines;
}

int HydroEditor::getCursorLine() const {
    return cursorLine;
}

int HydroEditor::getCursorColumn() const {
    return cursorColumn;
}

int HydroEditor::getFirstVisibleLine() const {
    return firstVisibleLine;
}

int HydroEditor::getFirstVisibleColumn() const {
    return firstVisibleColumn;
}

int HydroEditor::getMaximumLineLength() const {
    int maximum = 0;
    std::size_t index;

    for (index = 0; index < lines.size(); index++) {
        maximum = hydroMaxInt(
            maximum,
            static_cast<int>(lines[index].size())
        );
    }

    return maximum;
}

bool HydroEditor::isModified() const {
    return modified;
}

void HydroEditor::setModified(bool value) {
    modified = value;
}

void HydroEditor::setFirstVisibleLine(int value) {
    firstVisibleLine = hydroMaxInt(0, value);
}

void HydroEditor::setFirstVisibleColumn(int value) {
    firstVisibleColumn = hydroMaxInt(0, value);
}

void HydroEditor::setCursor(int line, int column) {
    cursorLine = line;
    cursorColumn = column;
    clampCursor();
}

bool HydroEditor::hasSelection() const {
    if (!selectionActive) {
        return false;
    }

    return !(
        selectionAnchor.line == selectionCaret.line &&
        selectionAnchor.column == selectionCaret.column
    );
}

void HydroEditor::clearSelection() {
    selectionActive = false;
    selectionAnchor = HydroTextPosition(
        cursorLine,
        cursorColumn
    );
    selectionCaret = selectionAnchor;
}

void HydroEditor::selectAll() {
    ensureDocument();

    selectionActive = true;
    selectionAnchor = HydroTextPosition(0, 0);

    int lastLine = static_cast<int>(lines.size()) - 1;

    selectionCaret = HydroTextPosition(
        lastLine,
        static_cast<int>(lines[lastLine].size())
    );

    cursorLine = selectionCaret.line;
    cursorColumn = selectionCaret.column;
}

void HydroEditor::beginSelection() {
    selectionActive = true;

    selectionAnchor = HydroTextPosition(
        cursorLine,
        cursorColumn
    );

    selectionCaret = selectionAnchor;
}

void HydroEditor::updateSelectionToCursor() {
    if (!selectionActive) {
        beginSelection();
    }

    selectionCaret = HydroTextPosition(
        cursorLine,
        cursorColumn
    );
}

void HydroEditor::getSelectionRange(
    HydroTextPosition& start,
    HydroTextPosition& end
) const {
    start = clampPosition(selectionAnchor);
    end = clampPosition(selectionCaret);

    if (positionLess(end, start)) {
        HydroTextPosition temporary = start;
        start = end;
        end = temporary;
    }
}

bool HydroEditor::isCharacterSelected(
    int line,
    int column
) const {
    if (!hasSelection()) {
        return false;
    }

    HydroTextPosition start;
    HydroTextPosition end;

    getSelectionRange(start, end);

    HydroTextPosition position(line, column);

    return
        !positionLess(position, start) &&
        positionLess(position, end);
}

std::string HydroEditor::getSelectedText() const {
    if (!hasSelection()) {
        return "";
    }

    HydroTextPosition start;
    HydroTextPosition end;

    getSelectionRange(start, end);

    std::stringstream output;

    if (start.line == end.line) {
        output << lines[start.line].substr(
            start.column,
            end.column - start.column
        );

        return output.str();
    }

    output << lines[start.line].substr(start.column);
    output << "\r\n";

    int line;

    for (line = start.line + 1; line < end.line; line++) {
        output << lines[line] << "\r\n";
    }

    output << lines[end.line].substr(0, end.column);

    return output.str();
}

bool HydroEditor::deleteSelection() {
    if (!hasSelection()) {
        return false;
    }

    HydroTextPosition start;
    HydroTextPosition end;

    getSelectionRange(start, end);

    if (start.line == end.line) {
        lines[start.line].erase(
            start.column,
            end.column - start.column
        );
    }
    else {
        std::string merged =
            lines[start.line].substr(0, start.column) +
            lines[end.line].substr(end.column);

        lines.erase(
            lines.begin() + start.line,
            lines.begin() + end.line + 1
        );

        lines.insert(
            lines.begin() + start.line,
            merged
        );
    }

    cursorLine = start.line;
    cursorColumn = start.column;

    clearSelection();
    modified = true;

    ensureDocument();
    clampCursor();

    return true;
}

void HydroEditor::insertText(const std::string& text) {
    deleteSelection();
    // Beilleszteskor pontosan a vagolap sortoreseit tartjuk meg,
    // es nem futtatjuk az Enter automatikus behuzasat.
    std::size_t index = 0;

    while (index < text.size()) {
        char character = text[index];

        if (character == '\r') {
            if (
                index + 1 < text.size() &&
                text[index + 1] == '\n'
            ) {
                index++;
            }

            std::string right=lines[cursorLine].substr(cursorColumn); lines[cursorLine].erase(cursorColumn); lines.insert(lines.begin()+cursorLine+1,right); cursorLine++; cursorColumn=0; modified=true;
        }
        else if (character == '\n') {
            std::string right=lines[cursorLine].substr(cursorColumn); lines[cursorLine].erase(cursorColumn); lines.insert(lines.begin()+cursorLine+1,right); cursorLine++; cursorColumn=0; modified=true;
        }
        else if (character == '\t') {
            insertTab();
        }
        else if (
            static_cast<unsigned char>(character) >= 32
        ) {
            insertCharacter(character);
        }

        index++;
    }
}

void HydroEditor::insertCharacter(char character) {
    deleteSelection();
    clampCursor();

    lines[cursorLine].insert(
        lines[cursorLine].begin() + cursorColumn,
        character
    );

    cursorColumn++;
    modified = true;
}

void HydroEditor::insertNewLine() {
    deleteSelection();
    clampCursor();

    std::string indentation;
    std::size_t index = 0;

    while (
        index < lines[cursorLine].size() &&
        lines[cursorLine][index] == ' '
    ) {
        indentation.push_back(' ');
        index++;
    }

    std::string left =
        lines[cursorLine].substr(0, cursorColumn);

    std::string right =
        lines[cursorLine].substr(cursorColumn);

    lines[cursorLine] = left;

    if (!left.empty() && left[left.size() - 1] == '{') {
        indentation += "    ";
    }

    lines.insert(
        lines.begin() + cursorLine + 1,
        indentation + right
    );

    cursorLine++;
    cursorColumn = static_cast<int>(indentation.size());
    modified = true;
}

void HydroEditor::insertTab() {
    deleteSelection();

    int spaces = 4 - (cursorColumn % 4);

    if (spaces == 0) {
        spaces = 4;
    }

    int index;

    for (index = 0; index < spaces; index++) {
        insertCharacter(' ');
    }
}

void HydroEditor::backspace() {
    if (deleteSelection()) {
        return;
    }

    clampCursor();

    if (cursorColumn > 0) {
        int deleteCount = 1;

        if (cursorColumn >= 4) {
            bool fourSpaces = true;
            int index;

            for (
                index = cursorColumn - 4;
                index < cursorColumn;
                index++
            ) {
                if (lines[cursorLine][index] != ' ') {
                    fourSpaces = false;
                    break;
                }
            }

            if (fourSpaces) {
                deleteCount = 4;
            }
        }

        lines[cursorLine].erase(
            cursorColumn - deleteCount,
            deleteCount
        );

        cursorColumn -= deleteCount;
    }
    else if (cursorLine > 0) {
        int previousLength =
            static_cast<int>(
                lines[cursorLine - 1].size()
            );

        lines[cursorLine - 1] +=
            lines[cursorLine];

        lines.erase(
            lines.begin() + cursorLine
        );

        cursorLine--;
        cursorColumn = previousLength;
    }

    modified = true;
}

void HydroEditor::deleteCharacter() {
    if (deleteSelection()) {
        return;
    }

    clampCursor();

    if (
        cursorColumn <
        static_cast<int>(lines[cursorLine].size())
    ) {
        lines[cursorLine].erase(cursorColumn, 1);
    }
    else if (
        cursorLine + 1 <
        static_cast<int>(lines.size())
    ) {
        lines[cursorLine] += lines[cursorLine + 1];

        lines.erase(
            lines.begin() + cursorLine + 1
        );
    }

    modified = true;
}

void HydroEditor::moveLeft(bool selecting) {
    prepareSelection(selecting);
    clampCursor();

    if (cursorColumn > 0) {
        cursorColumn--;
    }
    else if (cursorLine > 0) {
        cursorLine--;
        cursorColumn =
            static_cast<int>(lines[cursorLine].size());
    }

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::moveRight(bool selecting) {
    prepareSelection(selecting);
    clampCursor();

    if (
        cursorColumn <
        static_cast<int>(lines[cursorLine].size())
    ) {
        cursorColumn++;
    }
    else if (
        cursorLine + 1 <
        static_cast<int>(lines.size())
    ) {
        cursorLine++;
        cursorColumn = 0;
    }

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::moveUp(bool selecting) {
    prepareSelection(selecting);

    if (cursorLine > 0) {
        cursorLine--;
        clampCursor();
    }

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::moveDown(bool selecting) {
    prepareSelection(selecting);

    if (
        cursorLine + 1 <
        static_cast<int>(lines.size())
    ) {
        cursorLine++;
        clampCursor();
    }

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::moveHome(bool selecting) {
    prepareSelection(selecting);
    cursorColumn = 0;

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::moveEnd(bool selecting) {
    prepareSelection(selecting);
    clampCursor();

    cursorColumn =
        static_cast<int>(lines[cursorLine].size());

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::movePageUp(bool selecting) {
    prepareSelection(selecting);

    cursorLine = hydroMaxInt(
        0,
        cursorLine - 10
    );

    clampCursor();

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::movePageDown(bool selecting) {
    prepareSelection(selecting);

    cursorLine = hydroMinInt(
        static_cast<int>(lines.size()) - 1,
        cursorLine + 10
    );

    clampCursor();

    if (selecting) {
        updateSelectionToCursor();
    }
}

void HydroEditor::ensureCursorVisible(
    int visibleLineCount,
    int visibleColumnCount
) {
    visibleLineCount = hydroMaxInt(1, visibleLineCount);
    visibleColumnCount = hydroMaxInt(1, visibleColumnCount);

    if (cursorLine < firstVisibleLine) {
        firstVisibleLine = cursorLine;
    }
    else if (
        cursorLine >=
        firstVisibleLine + visibleLineCount
    ) {
        firstVisibleLine =
            cursorLine - visibleLineCount + 1;
    }

    if (cursorColumn < firstVisibleColumn) {
        firstVisibleColumn = cursorColumn;
    }
    else if (
        cursorColumn >=
        firstVisibleColumn + visibleColumnCount
    ) {
        firstVisibleColumn =
            cursorColumn - visibleColumnCount + 1;
    }

    firstVisibleLine = hydroMaxInt(0, firstVisibleLine);
    firstVisibleColumn =
        hydroMaxInt(0, firstVisibleColumn);
}


bool HydroEditor::setRuntimeAnnotation(
    int oneBasedLine,
    const std::string& comment
) {
    int index = oneBasedLine - 1;

    if (index < 0 || index >= static_cast<int>(lines.size())) {
        return false;
    }

    std::string original = lines[index];
    std::string trimmedLine = original;

    while (!trimmedLine.empty() && (
        trimmedLine[trimmedLine.size() - 1] == ' ' ||
        trimmedLine[trimmedLine.size() - 1] == '\t'
    )) {
        trimmedLine.erase(trimmedLine.size() - 1);
    }

    std::size_t annotationStart = trimmedLine.rfind("/*");

    if (
        annotationStart != std::string::npos &&
        trimmedLine.size() >= 2 &&
        trimmedLine.substr(trimmedLine.size() - 2) == "*\\"
    ) {
        std::string before = trimmedLine.substr(0, annotationStart);

        while (!before.empty() && (
            before[before.size() - 1] == ' ' ||
            before[before.size() - 1] == '\t'
        )) {
            before.erase(before.size() - 1);
        }

        trimmedLine = before;
    }

    std::string updated = trimmedLine;

    if (!updated.empty()) {
        updated += "    ";
    }

    updated += comment;

    if (updated == original) {
        return false;
    }

    lines[index] = updated;
    modified = true;

    if (cursorLine == index) {
        cursorColumn = hydroClampInt(
            cursorColumn,
            0,
            static_cast<int>(lines[index].size())
        );
    }

    clearSelection();
    return true;
}
