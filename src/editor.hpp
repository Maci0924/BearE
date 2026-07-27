#pragma once

#include "common.hpp"

struct HydroTextPosition {
    int line;
    int column;

    HydroTextPosition();
    HydroTextPosition(int lineValue, int columnValue);
};

class HydroEditor {
private:
    std::vector<std::string> lines;
    int cursorLine;
    int cursorColumn;
    int firstVisibleLine;
    int firstVisibleColumn;
    bool modified;

    bool selectionActive;
    HydroTextPosition selectionAnchor;
    HydroTextPosition selectionCaret;

    void ensureDocument();
    void clampCursor();

    HydroTextPosition clampPosition(
        const HydroTextPosition& position
    ) const;

    static bool positionLess(
        const HydroTextPosition& left,
        const HydroTextPosition& right
    );

    void prepareSelection(bool selecting);

public:
    HydroEditor();

    bool loadFile(const std::string& path);
    bool saveFile(const std::string& path) const;
    void newDocument();

    std::string getText() const;
    const std::vector<std::string>& getLines() const;

    int getCursorLine() const;
    int getCursorColumn() const;
    int getFirstVisibleLine() const;
    int getFirstVisibleColumn() const;
    int getMaximumLineLength() const;
    bool isModified() const;

    void setModified(bool value);
    void setFirstVisibleLine(int value);
    void setFirstVisibleColumn(int value);
    void setCursor(int line, int column);

    bool hasSelection() const;
    void clearSelection();
    void selectAll();
    void beginSelection();
    void updateSelectionToCursor();

    void getSelectionRange(
        HydroTextPosition& start,
        HydroTextPosition& end
    ) const;

    bool isCharacterSelected(int line, int column) const;
    std::string getSelectedText() const;
    bool deleteSelection();

    void insertText(const std::string& text);
    void insertCharacter(char character);
    void insertNewLine();
    void insertTab();
    void backspace();
    void deleteCharacter();

    void moveLeft(bool selecting);
    void moveRight(bool selecting);
    void moveUp(bool selecting);
    void moveDown(bool selecting);
    void moveHome(bool selecting);
    void moveEnd(bool selecting);
    void movePageUp(bool selecting);
    void movePageDown(bool selecting);

    void ensureCursorVisible(
        int visibleLineCount,
        int visibleColumnCount
    );

    bool setRuntimeAnnotation(
        int oneBasedLine,
        const std::string& comment
    );
};
