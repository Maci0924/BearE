#include "app.hpp"


static int hydroHexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    if (character >= 'A' && character <= 'F') {
        return 10 + character - 'A';
    }

    if (character >= 'a' && character <= 'f') {
        return 10 + character - 'a';
    }

    return -1;
}

static bool hydroDecodeHex(
    const std::string& encoded,
    std::string& decoded
) {
    if ((encoded.size() % 2) != 0) {
        return false;
    }

    decoded.clear();

    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        int high = hydroHexDigit(encoded[i]);
        int low = hydroHexDigit(encoded[i + 1]);

        if (high < 0 || low < 0) {
            return false;
        }

        decoded.push_back(
            static_cast<char>((high << 4) | low)
        );
    }

    return true;
}


#include "compiler.hpp"
#include "process.hpp"

#include <commdlg.h>

static const char* WINDOW_CLASS_NAME =
    "ETempleIDEV44Window";

static const int TOP_BAR_HEIGHT = 24;
static const int INFO_BAR_HEIGHT = 40;
static const int STATUS_BAR_HEIGHT = 22;
static const int RIGHT_PANEL_WIDTH = 380;
static const int OUTPUT_PANEL_HEIGHT = 190;

static const COLORREF COL_BLUE = RGB(0, 0, 170);
static const COLORREF COL_RED = RGB(190, 0, 0);
static const COLORREF COL_GREEN = RGB(0, 145, 0);
static const COLORREF COL_MAGENTA = RGB(170, 0, 170);
static const COLORREF COL_CYAN = RGB(0, 140, 140);
static const COLORREF COL_BLACK = RGB(0, 0, 0);
static const COLORREF COL_WHITE = RGB(255, 255, 255);

HydroApplication::HydroApplication()
    : window(NULL),
      normalFont(NULL),
      headingFont(NULL),
      editor(),
      outputLines(),
      clientWidth(1280),
      clientHeight(820),
      charWidth(9),
      charHeight(18),
      cursorVisible(true),
      mouseSelecting(false),
      draggingHorizontalScroll(false),
      draggingVerticalScroll(false),
      scrollDragOffset(0),
      currentFilePath("") {
}

HydroApplication::~HydroApplication() {
    if (normalFont != NULL) DeleteObject(normalFont);
    if (headingFont != NULL) DeleteObject(headingFont);
}

HydroApplication::Layout
HydroApplication::calculateLayout() const {
    Layout layout;

    layout.topBar.left = 0;
    layout.topBar.top = 0;
    layout.topBar.right = clientWidth;
    layout.topBar.bottom = TOP_BAR_HEIGHT;

    layout.infoBar.left = 0;
    layout.infoBar.top = TOP_BAR_HEIGHT;
    layout.infoBar.right = clientWidth;
    layout.infoBar.bottom =
        TOP_BAR_HEIGHT + INFO_BAR_HEIGHT;

    layout.status.left = 0;
    layout.status.top =
        clientHeight - STATUS_BAR_HEIGHT;
    layout.status.right = clientWidth;
    layout.status.bottom = clientHeight;

    layout.output.left = 0;
    layout.output.top =
        layout.status.top - OUTPUT_PANEL_HEIGHT;
    layout.output.right = clientWidth;
    layout.output.bottom = layout.status.top;

    layout.editor.left = 0;
    layout.editor.top = layout.infoBar.bottom;
    layout.editor.right =
        hydroMaxInt(400, clientWidth - RIGHT_PANEL_WIDTH);
    layout.editor.bottom = layout.output.top;

    const int scrollSize = 18;

    layout.editorContent = layout.editor;
    layout.editorContent.right -= scrollSize;
    layout.editorContent.bottom -= scrollSize;

    layout.horizontalScroll.left = layout.editor.left + 2;
    layout.horizontalScroll.top = layout.editorContent.bottom;
    layout.horizontalScroll.right = layout.editorContent.right;
    layout.horizontalScroll.bottom = layout.editor.bottom - 2;

    layout.verticalScroll.left = layout.editorContent.right;
    layout.verticalScroll.top = layout.editor.top + 2;
    layout.verticalScroll.right = layout.editor.right - 2;
    layout.verticalScroll.bottom = layout.editorContent.bottom;

    layout.rightPanel.left = layout.editor.right;
    layout.rightPanel.top = layout.infoBar.bottom;
    layout.rightPanel.right = clientWidth;
    layout.rightPanel.bottom = layout.output.top;

    return layout;
}

void HydroApplication::fillRectColor(
    HDC dc,
    const RECT& rect,
    COLORREF color
) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void HydroApplication::drawBorder(
    HDC dc,
    const RECT& rect,
    COLORREF color,
    int thickness
) {
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush =
        SelectObject(dc, GetStockObject(NULL_BRUSH));

    Rectangle(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom
    );

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void HydroApplication::setOutput(const std::string& text) {
    outputLines.clear();

    std::stringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }

        outputLines.push_back(line);
    }

    if (outputLines.empty()) {
        outputLines.push_back("");
    }
}

void HydroApplication::appendOutput(const std::string& text) {
    outputLines.push_back(text);
}

void HydroApplication::appendCapturedOutput(
    const std::string& text
) {
    std::stringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }

        outputLines.push_back(line);
    }
}

std::string HydroApplication::displayFileName() const {
    if (currentFilePath.empty()) {
        return "Nevtelen.e";
    }

    std::size_t slash =
        currentFilePath.find_last_of("\\/");

    if (slash == std::string::npos) {
        return currentFilePath;
    }

    return currentFilePath.substr(slash + 1);
}

bool HydroApplication::confirmDiscardChanges() {
    if (!editor.isModified()) {
        return true;
    }

    int answer = MessageBoxA(
        window,
        "A fajl nincs elmentve. Elmented a valtozasokat?",
        "E Temple IDE",
        MB_YESNOCANCEL | MB_ICONWARNING
    );

    if (answer == IDCANCEL) {
        return false;
    }

    if (answer == IDYES) {
        return saveFile();
    }

    return true;
}

void HydroApplication::newFile() {
    if (!confirmDiscardChanges()) {
        return;
    }

    editor.newDocument();
    currentFilePath = "";

    setOutput("Uj .e fajl letrehozva.");
    updateTitle();

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::openFile() {
    if (!confirmDiscardChanges()) {
        return;
    }

    char fileName[MAX_PATH];
    ZeroMemory(fileName, sizeof(fileName));

    OPENFILENAMEA dialog;
    ZeroMemory(&dialog, sizeof(dialog));

    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter =
        "E forrasfajlok (*.e)\0*.e\0"
        "Minden fajl (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY;
    dialog.lpstrDefExt = "e";

    if (!GetOpenFileNameA(&dialog)) {
        return;
    }

    if (!editor.loadFile(fileName)) {
        MessageBoxA(
            window,
            "A fajlt nem sikerult megnyitni.",
            "E Temple IDE",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    currentFilePath = fileName;

    setOutput("Megnyitva: " + currentFilePath);
    updateTitle();

    InvalidateRect(window, NULL, FALSE);
}

bool HydroApplication::saveFileAs() {
    char fileName[MAX_PATH];
    ZeroMemory(fileName, sizeof(fileName));

    if (!currentFilePath.empty()) {
        std::strncpy(
            fileName,
            currentFilePath.c_str(),
            MAX_PATH - 1
        );
    }
    else {
        std::strncpy(
            fileName,
            "program.e",
            MAX_PATH - 1
        );
    }

    OPENFILENAMEA dialog;
    ZeroMemory(&dialog, sizeof(dialog));

    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter =
        "E forrasfajlok (*.e)\0*.e\0"
        "Minden fajl (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags =
        OFN_PATHMUSTEXIST |
        OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = "e";

    if (!GetSaveFileNameA(&dialog)) {
        return false;
    }

    std::string chosenPath = fileName;

    if (
        chosenPath.size() < 2 ||
        chosenPath.substr(chosenPath.size() - 2) != ".e"
    ) {
        chosenPath += ".e";
    }

    if (!editor.saveFile(chosenPath)) {
        MessageBoxA(
            window,
            "A fajlt nem sikerult elmenteni.",
            "E Temple IDE",
            MB_OK | MB_ICONERROR
        );

        return false;
    }

    currentFilePath = chosenPath;
    editor.setModified(false);

    setOutput("Elmentve: " + currentFilePath);
    updateTitle();

    InvalidateRect(window, NULL, FALSE);

    return true;
}

bool HydroApplication::saveFile() {
    if (currentFilePath.empty()) {
        return saveFileAs();
    }

    if (!editor.saveFile(currentFilePath)) {
        MessageBoxA(
            window,
            "A fajlt nem sikerult elmenteni.",
            "E Temple IDE",
            MB_OK | MB_ICONERROR
        );

        return false;
    }

    editor.setModified(false);

    setOutput("Elmentve: " + currentFilePath);
    updateTitle();

    InvalidateRect(window, NULL, FALSE);

    return true;
}

void HydroApplication::copySelection() {
    std::string selected = editor.getSelectedText();

    if (selected.empty()) {
        return;
    }

    if (!OpenClipboard(window)) {
        return;
    }

    EmptyClipboard();

    HGLOBAL memory = GlobalAlloc(
        GMEM_MOVEABLE,
        selected.size() + 1
    );

    if (memory != NULL) {
        char* destination =
            static_cast<char*>(GlobalLock(memory));

        if (destination != NULL) {
            std::memcpy(
                destination,
                selected.c_str(),
                selected.size() + 1
            );

            GlobalUnlock(memory);

            if (SetClipboardData(CF_TEXT, memory) != NULL) {
                memory = NULL;
            }
        }
    }

    if (memory != NULL) {
        GlobalFree(memory);
    }

    CloseClipboard();
}

void HydroApplication::cutSelection() {
    if (!editor.hasSelection()) {
        return;
    }

    copySelection();
    editor.deleteSelection();

    updateTitle();
    ensureCursorVisible();

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::pasteClipboard() {
    if (!OpenClipboard(window)) {
        return;
    }

    HANDLE data = GetClipboardData(CF_TEXT);

    if (data != NULL) {
        const char* text =
            static_cast<const char*>(GlobalLock(data));

        if (text != NULL) {
            editor.insertText(text);
            GlobalUnlock(data);
        }
    }

    CloseClipboard();

    updateTitle();
    ensureCursorVisible();

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::buildCurrentSource() {
    if (!saveFile()) {
        return;
    }

    setOutput(
        "F5 BUILD\r\n"
        "----------------------------------------"
    );

    InvalidateRect(window, NULL, FALSE);
    UpdateWindow(window);

    std::string sourceText = editor.getText();
    if (sourceText.find("#kontarb :IKONAIN2:") != std::string::npos) {
        std::string marker = hydroExecutableDirectory() + "\\ebooks\\IKONAIN2.installed";
        if (GetFileAttributesA(marker.c_str()) == INVALID_FILE_ATTRIBUTES) { setOutput("BUILD FAILED\r\nAz IKONAIN2 nincs letoltve. Nyisd meg az E Books fulet es telepitsd."); InvalidateRect(window,NULL,FALSE); return; }
    }
    HydroCompileResult result = hydroCompileSource(sourceText);

    std::size_t index;

    for (
        index = 0;
        index < result.logLines.size();
        index++
    ) {
        appendOutput(result.logLines[index]);
    }

    if (!result.success) {
        appendOutput("");
        appendOutput("BUILD FAILED");

        if (result.error.hasError) {
            std::stringstream location;

            location
                << result.error.line
                << ". sor, "
                << result.error.column
                << ". oszlop";

            appendOutput(location.str());
            appendOutput(result.error.message);

            editor.setCursor(
                result.error.line - 1,
                result.error.column - 1
            );

            editor.clearSelection();
            ensureCursorVisible();
        }

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    std::string directory =
        hydroExecutableDirectory();

    std::string cppPath =
        directory + "\\output.cpp";

    if (!hydroWriteTextFile(
        cppPath,
        result.generatedCpp
    )) {
        appendOutput(
            "HIBA: output.cpp nem hozhato letre."
        );

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    appendOutput("output.cpp: kesz");
    appendOutput("4. g++ fordito inditasa...");

    std::string captured;
    DWORD exitCode = 1;

    bool started = hydroRunProcessCapture(
        "g++ -std=c++11 output.cpp -o program.exe -lgdi32 -luser32 -lkernel32",
        directory,
        captured,
        exitCode
    );

    if (!captured.empty()) {
        appendCapturedOutput(captured);
    }

    if (!started) {
        appendOutput("HIBA: g++ nem indult el.");
        appendOutput(
            "Ellenorizd, hogy benne van-e a PATH-ban."
        );

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    if (exitCode != 0) {
        appendOutput("BUILD FAILED: g++ hiba.");

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    appendOutput("g++: sikeres");
    appendOutput("");
    appendOutput("BUILD COMPLETE");
    appendOutput("Elkeszult: program.exe");
    appendOutput(
        "F6 kulon konzolablakban futtatja, hogy az input is mukodjon."
    );

    InvalidateRect(window, NULL, FALSE);
}


void HydroApplication::consumeRuntimeAnnotations() {
    std::string directory = hydroExecutableDirectory();
    std::string path = directory + "\\e_annotations.tmp";

    std::ifstream input(path.c_str(), std::ios::binary);

    if (!input.is_open()) {
        return;
    }

    std::vector<std::pair<int, std::string> > annotations;
    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }

        std::size_t separator = line.find('|');

        if (separator == std::string::npos) {
            continue;
        }

        std::string lineText = line.substr(0, separator);
        std::string encoded = line.substr(separator + 1);
        int sourceLine = std::atoi(lineText.c_str());
        std::string comment;

        if (
            sourceLine > 0 &&
            hydroDecodeHex(encoded, comment)
        ) {
            annotations.push_back(
                std::make_pair(sourceLine, comment)
            );
        }
    }

    input.close();

    if (annotations.empty()) {
        return;
    }

    // A fajlt nem toroljuk itt. A futtatott program mindig egy teljes,
    // atomikusan cserelt pillanatkepet ir bele. A korabbi torlesnel
    // versenyhelyzet miatt elveszhetett a var? vagy a var??? eredmenye.
    bool changed = false;

    for (std::size_t i = 0; i < annotations.size(); ++i) {
        if (editor.setRuntimeAnnotation(
            annotations[i].first,
            annotations[i].second
        )) {
            changed = true;
        }
    }

    if (changed) {
        updateTitle();
        ensureCursorVisible();
        InvalidateRect(window, NULL, FALSE);
    }
}

void HydroApplication::runProgram() {
    std::string directory =
        hydroExecutableDirectory();

    std::string programPath =
        directory + "\\program.exe";

    DWORD attributes =
        GetFileAttributesA(programPath.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        setOutput(
            "F6 RUN\r\n"
            "----------------------------------------\r\n"
            "HIBA: program.exe nem talalhato.\r\n"
            "Nyomj elobb F5-ot."
        );

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    hydroWriteTextFile(
        directory + "\\e_annotations.tmp",
        ""
    );

    DWORD windowsError = 0;

    if (!hydroRunProgramVisible(
        programPath,
        directory,
        windowsError
    )) {
        std::stringstream message;

        message
            << "F6 RUN\r\n"
            << "----------------------------------------\r\n"
            << "HIBA: A program nem indult el.\r\n"
            << "Windows hibakod: "
            << windowsError;

        setOutput(message.str());

        InvalidateRect(window, NULL, FALSE);
        return;
    }

    setOutput(
        "F6 RUN\r\n"
        "----------------------------------------\r\n"
        "A program kulon konzolablakban elindult.\r\n"
        "Ott jelenik meg a tascuF kimenet, es ott mukodik a skiyF input.\r\n"
        "A konzol a program vege utan nyitva marad."
    );

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::drawTopBar(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.topBar, COL_BLUE);

    SelectObject(dc, headingFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_WHITE);

    SYSTEMTIME time;
    GetLocalTime(&time);

    char text[320];

    std::sprintf(
        text,
        " Ctrl+N New  Ctrl+O Open  Ctrl+S Save  F5 Build  F6 Run   E Books     %02d/%02d/%04d %02d:%02d:%02d ",
        time.wMonth,
        time.wDay,
        time.wYear,
        time.wHour,
        time.wMinute,
        time.wSecond
    );

    TextOutA(
        dc,
        3,
        3,
        text,
        static_cast<int>(std::strlen(text))
    );
}

void HydroApplication::drawInfoBar(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.infoBar, COL_WHITE);
    drawBorder(dc, layout.infoBar, COL_BLUE, 2);

    SelectObject(dc, headingFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_RED);

    const char* heading =
        "E Programming Environment";

    TextOutA(
        dc,
        12,
        layout.infoBar.top + 3,
        heading,
        static_cast<int>(std::strlen(heading))
    );

    SelectObject(dc, normalFont);
    SetTextColor(dc, COL_BLUE);

    std::string subtitle =
        "File: " +
        displayFileName() +
        " | .e source | mouse/Shift selection | Ctrl+C/X/V/A";

    TextOutA(
        dc,
        12,
        layout.infoBar.top + 21,
        subtitle.c_str(),
        static_cast<int>(subtitle.size())
    );
}

static bool isIdentifierCharacter(char character) {
    return
        std::isalnum(
            static_cast<unsigned char>(character)
        ) ||
        character == '_' ||
        character == '&';
}

static COLORREF identifierColor(
    const std::string& identifier
) {
    if (
        identifier == "tascuF" ||
        identifier == "skiyF" ||
        identifier == "ahf" ||
        identifier == "nib"
    ) {
        return COL_RED;
    }

    if (
        identifier == "kocsi" ||
        identifier == "hajo" ||
        identifier == "let" ||
        identifier == "felho" ||
        identifier == "hang" ||
        identifier == "kontarb" ||
        identifier == "studio"
    ) {
        return COL_MAGENTA;
    }

    return COL_BLACK;
}

void HydroApplication::drawCodeLine(
    HDC dc,
    int x,
    int y,
    int lineIndex,
    const std::string& line
) {
    std::vector<COLORREF> colors(
        line.size(),
        COL_BLACK
    );

    std::size_t index = 0;

    while (index < line.size()) {
        if (line[index] == '"') {
            std::size_t start = index;
            index++;

            while (index < line.size()) {
                if (
                    line[index] == '"' &&
                    line[index - 1] != '\\'
                ) {
                    index++;
                    break;
                }

                index++;
            }

            std::size_t colorIndex;

            for (
                colorIndex = start;
                colorIndex < index;
                colorIndex++
            ) {
                colors[colorIndex] = COL_GREEN;
            }
        }
        else if (line[index] == '\'') {
            std::size_t start = index;
            index++;

            while (index < line.size()) {
                if (line[index] == '\'') {
                    index++;
                    break;
                }

                index++;
            }

            std::size_t colorIndex;

            for (
                colorIndex = start;
                colorIndex < index;
                colorIndex++
            ) {
                colors[colorIndex] = COL_CYAN;
            }
        }
        else if (
            std::isdigit(
                static_cast<unsigned char>(line[index])
            )
        ) {
            std::size_t start = index;

            while (
                index < line.size() &&
                (
                    std::isdigit(
                        static_cast<unsigned char>(
                            line[index]
                        )
                    ) ||
                    line[index] == '.'
                )
            ) {
                index++;
            }

            std::size_t colorIndex;

            for (
                colorIndex = start;
                colorIndex < index;
                colorIndex++
            ) {
                colors[colorIndex] = COL_CYAN;
            }
        }
        else if (
            std::isalpha(
                static_cast<unsigned char>(line[index])
            ) ||
            line[index] == '_'
        ) {
            std::size_t start = index;
            std::string identifier;

            while (
                index < line.size() &&
                isIdentifierCharacter(line[index])
            ) {
                identifier.push_back(line[index]);
                index++;
            }

            COLORREF color =
                identifierColor(identifier);

            std::size_t colorIndex;

            for (
                colorIndex = start;
                colorIndex < index;
                colorIndex++
            ) {
                colors[colorIndex] = color;
            }
        }
        else {
            if (
                line[index] == '.' ||
                line[index] == '(' ||
                line[index] == ')' ||
                line[index] == '{' ||
                line[index] == '}'
            ) {
                colors[index] = COL_BLUE;
            }
            else if (
                line[index] == '+' ||
                line[index] == '-' ||
                line[index] == '*' ||
                line[index] == '/' ||
                line[index] == '%' ||
                line[index] == '=' ||
                line[index] == '<' ||
                line[index] == '>' ||
                line[index] == '!'
            ) {
                colors[index] = COL_MAGENTA;
            }

            index++;
        }
    }

    for (index = 0; index < line.size(); index++) {
        if (
            static_cast<int>(index) <
            editor.getFirstVisibleColumn()
        ) {
            continue;
        }

        bool selected =
            editor.isCharacterSelected(
                lineIndex,
                static_cast<int>(index)
            );

        RECT cell;

        cell.left =
            x +
            (
                static_cast<int>(index) -
                editor.getFirstVisibleColumn()
            ) *
            charWidth;

        cell.top = y;
        cell.right = cell.left + charWidth;
        cell.bottom = y + charHeight;

        if (selected) {
            fillRectColor(dc, cell, COL_BLUE);
            SetTextColor(dc, COL_WHITE);
        }
        else {
            SetTextColor(dc, colors[index]);
        }

        char character = line[index];

        TextOutA(
            dc,
            cell.left,
            y,
            &character,
            1
        );
    }
}

void HydroApplication::drawEditor(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.editor, COL_WHITE);
    drawBorder(dc, layout.editor, COL_BLUE, 2);

    SelectObject(dc, normalFont);
    SetBkMode(dc, TRANSPARENT);

    HRGN editorClip = CreateRectRgn(
        layout.editorContent.left + 2,
        layout.editorContent.top + 2,
        layout.editorContent.right - 2,
        layout.editorContent.bottom - 2
    );

    SelectClipRgn(dc, editorClip);

    int textStartX =
        layout.editor.left + 52;

    int visibleLines = hydroMaxInt(
        1,
        (
            layout.editorContent.bottom -
            layout.editorContent.top -
            10
        ) /
        charHeight
    );

    int y = layout.editor.top + 5;

    const std::vector<std::string>& lines =
        editor.getLines();

    int row;

    for (row = 0; row < visibleLines; row++) {
        int lineIndex =
            editor.getFirstVisibleLine() + row;

        if (
            lineIndex >=
            static_cast<int>(lines.size())
        ) {
            break;
        }

        char number[16];

        std::sprintf(
            number,
            "%04d",
            lineIndex + 1
        );

        SetTextColor(dc, COL_BLUE);

        TextOutA(
            dc,
            layout.editor.left + 5,
            y,
            number,
            4
        );

        drawCodeLine(
            dc,
            textStartX,
            y,
            lineIndex,
            lines[lineIndex]
        );

        y += charHeight;
    }

    if (cursorVisible) {
        int screenLine =
            editor.getCursorLine() -
            editor.getFirstVisibleLine();

        if (
            screenLine >= 0 &&
            screenLine < visibleLines
        ) {
            int cursorX =
                textStartX +
                (
                    editor.getCursorColumn() -
                    editor.getFirstVisibleColumn()
                ) *
                charWidth;

            int cursorY =
                layout.editor.top +
                5 +
                screenLine *
                charHeight;

            RECT cursorRect;

            cursorRect.left = cursorX;
            cursorRect.top = cursorY;
            cursorRect.right = cursorX + 2;
            cursorRect.bottom = cursorY + charHeight;

            fillRectColor(dc, cursorRect, COL_RED);
        }
    }

    SelectClipRgn(dc, NULL);
    DeleteObject(editorClip);

    drawEditorScrollbars(dc, layout);
}

void HydroApplication::drawEditorScrollbars(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.horizontalScroll, RGB(220, 220, 220));
    fillRectColor(dc, layout.verticalScroll, RGB(220, 220, 220));

    drawBorder(dc, layout.horizontalScroll, COL_BLUE, 1);
    drawBorder(dc, layout.verticalScroll, COL_BLUE, 1);

    RECT horizontalThumb =
        horizontalScrollThumb(layout);

    RECT verticalThumb =
        verticalScrollThumb(layout);

    fillRectColor(dc, horizontalThumb, COL_BLUE);
    fillRectColor(dc, verticalThumb, COL_BLUE);
}

void HydroApplication::drawRightPanel(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.rightPanel, COL_WHITE);
    drawBorder(dc, layout.rightPanel, COL_BLUE, 2);

    SelectObject(dc, headingFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_RED);

    const char* heading = "File & Edit";

    TextOutA(
        dc,
        layout.rightPanel.left + 8,
        layout.rightPanel.top + 5,
        heading,
        static_cast<int>(std::strlen(heading))
    );

    SelectObject(dc, normalFont);

    struct HelpEntry {
        const char* text;
        COLORREF color;
    };

    HelpEntry entries[] = {
        {"Ctrl+N       New .e file", COL_GREEN},
        {"Ctrl+O       Open .e file", COL_GREEN},
        {"Ctrl+S       Save", COL_GREEN},
        {"Ctrl+Shift+S Save As", COL_GREEN},
        {"", COL_BLACK},
        {"Selection", COL_RED},
        {"Mouse drag   Select", COL_BLUE},
        {"Shift+Arrows Select", COL_BLUE},
        {"Ctrl+A       Select all", COL_BLUE},
        {"Ctrl+C       Copy", COL_BLUE},
        {"Ctrl+X       Cut", COL_BLUE},
        {"Ctrl+V       Paste", COL_BLUE},
        {"", COL_BLACK},
        {"Editor", COL_RED},
        {"Tab          4 spaces", COL_CYAN},
        {"Enter        Auto indent", COL_CYAN},
        {"F5           Build", COL_CYAN},
        {"F6           Run console", COL_CYAN},
        {"", COL_BLACK},
        {"E source", COL_RED},
        {"*.e", COL_MAGENTA}
    };

    int count =
        static_cast<int>(
            sizeof(entries) /
            sizeof(entries[0])
        );

    int y = layout.rightPanel.top + 29;
    int index;

    for (index = 0; index < count; index++) {
        SetTextColor(dc, entries[index].color);

        TextOutA(
            dc,
            layout.rightPanel.left + 8,
            y,
            entries[index].text,
            static_cast<int>(
                std::strlen(entries[index].text)
            )
        );

        y += charHeight;
    }
}

void HydroApplication::drawOutput(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.output, COL_WHITE);
    drawBorder(dc, layout.output, COL_BLUE, 2);

    SelectObject(dc, headingFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_RED);

    const char* heading = "Compiler Output";

    TextOutA(
        dc,
        layout.output.left + 8,
        layout.output.top + 4,
        heading,
        static_cast<int>(std::strlen(heading))
    );

    SelectObject(dc, normalFont);

    int visibleLines = hydroMaxInt(
        1,
        (
            layout.output.bottom -
            layout.output.top -
            30
        ) /
        charHeight
    );

    int start = 0;

    if (
        static_cast<int>(outputLines.size()) >
        visibleLines
    ) {
        start =
            static_cast<int>(outputLines.size()) -
            visibleLines;
    }

    int y = layout.output.top + 27;
    int row;

    for (row = 0; row < visibleLines; row++) {
        int index = start + row;

        if (
            index >=
            static_cast<int>(outputLines.size())
        ) {
            break;
        }

        COLORREF color = COL_BLACK;

        if (
            outputLines[index].find("HIBA") !=
            std::string::npos ||
            outputLines[index].find("FAILED") !=
            std::string::npos
        ) {
            color = COL_RED;
        }
        else if (
            outputLines[index].find("sikeres") !=
            std::string::npos ||
            outputLines[index].find("COMPLETE") !=
            std::string::npos ||
            outputLines[index].find("Elkeszult") !=
            std::string::npos ||
            outputLines[index].find("elindult") !=
            std::string::npos
        ) {
            color = COL_GREEN;
        }
        else if (
            outputLines[index].find("F5") !=
            std::string::npos ||
            outputLines[index].find("F6") !=
            std::string::npos
        ) {
            color = COL_BLUE;
        }

        SetTextColor(dc, color);

        TextOutA(
            dc,
            layout.output.left + 8,
            y,
            outputLines[index].c_str(),
            static_cast<int>(
                outputLines[index].size()
            )
        );

        y += charHeight;
    }
}

void HydroApplication::drawStatus(
    HDC dc,
    const Layout& layout
) {
    fillRectColor(dc, layout.status, COL_BLUE);

    SelectObject(dc, headingFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_WHITE);

    char text[320];

    std::sprintf(
        text,
        " Ln %d  Col %d  Lines %d  %s  %s ",
        editor.getCursorLine() + 1,
        editor.getCursorColumn() + 1,
        static_cast<int>(editor.getLines().size()),
        editor.isModified()
            ? "MODIFIED"
            : "SAVED",
        editor.hasSelection()
            ? "SELECTED"
            : ""
    );

    TextOutA(
        dc,
        4,
        layout.status.top + 2,
        text,
        static_cast<int>(std::strlen(text))
    );
}

void HydroApplication::drawScene(HDC dc) {
    RECT whole;

    whole.left = 0;
    whole.top = 0;
    whole.right = clientWidth;
    whole.bottom = clientHeight;

    fillRectColor(dc, whole, COL_WHITE);

    Layout layout = calculateLayout();

    drawTopBar(dc, layout);
    drawInfoBar(dc, layout);
    drawEditor(dc, layout);
    drawRightPanel(dc, layout);
    drawOutput(dc, layout);
    drawStatus(dc, layout);
}

void HydroApplication::paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);

    HDC memory = CreateCompatibleDC(dc);

    HBITMAP bitmap = CreateCompatibleBitmap(
        dc,
        hydroMaxInt(1, clientWidth),
        hydroMaxInt(1, clientHeight)
    );

    HGDIOBJ oldBitmap =
        SelectObject(memory, bitmap);

    drawScene(memory);

    BitBlt(
        dc,
        0,
        0,
        clientWidth,
        clientHeight,
        memory,
        0,
        0,
        SRCCOPY
    );

    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);

    EndPaint(window, &paint);
}

void HydroApplication::ensureCursorVisible() {
    Layout layout = calculateLayout();

    int visibleLines = hydroMaxInt(
        1,
        (
            layout.editorContent.bottom -
            layout.editorContent.top -
            10
        ) /
        charHeight
    );

    int visibleColumns = hydroMaxInt(
        1,
        (
            layout.editorContent.right -
            layout.editorContent.left -
            58
        ) /
        charWidth
    );

    editor.ensureCursorVisible(
        visibleLines,
        visibleColumns
    );
}

void HydroApplication::handleKeyDown(WPARAM key) {
    bool controlPressed =
        (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    bool shiftPressed =
        (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    bool altPressed =
        (GetKeyState(VK_MENU) & 0x8000) != 0;

    bool rightAltPressed =
        (GetKeyState(VK_RMENU) & 0x8000) != 0;

    // Windows az AltGr-t gyakran Ctrl+Alt kombinaciokent jelenti.
    // AltGr hasznalatakor nem szabad Ctrl gyorsbillentyukent kezelni
    // az adott billentyut, kulonben peldaul AltGr+C masolast inditana
    // a & karakter beirasa helyett.
    bool altGrPressed =
        rightAltPressed ||
        (controlPressed && altPressed);

    bool shortcutControl =
        controlPressed && !altGrPressed;

    if (shortcutControl && key == 'N') {
        newFile();
        return;
    }

    if (shortcutControl && key == 'O') {
        openFile();
        return;
    }

    if (
        shortcutControl &&
        shiftPressed &&
        key == 'S'
    ) {
        saveFileAs();
        return;
    }

    if (shortcutControl && key == 'S') {
        saveFile();
        return;
    }

    if (shortcutControl && key == 'A') {
        editor.selectAll();
        ensureCursorVisible();
        InvalidateRect(window, NULL, FALSE);
        return;
    }

    if (shortcutControl && key == 'C') {
        copySelection();
        return;
    }

    if (shortcutControl && key == 'X') {
        cutSelection();
        return;
    }

    if (shortcutControl && key == 'V') {
        pasteClipboard();
        return;
    }

    if (key == VK_F5) {
        buildCurrentSource();
        return;
    }

    if (key == VK_F6) {
        runProgram();
        return;
    }

    if (key == VK_LEFT) {
        editor.moveLeft(shiftPressed);
    }
    else if (key == VK_RIGHT) {
        editor.moveRight(shiftPressed);
    }
    else if (key == VK_UP) {
        editor.moveUp(shiftPressed);
    }
    else if (key == VK_DOWN) {
        editor.moveDown(shiftPressed);
    }
    else if (key == VK_HOME) {
        editor.moveHome(shiftPressed);
    }
    else if (key == VK_END) {
        editor.moveEnd(shiftPressed);
    }
    else if (key == VK_PRIOR) {
        editor.movePageUp(shiftPressed);
    }
    else if (key == VK_NEXT) {
        editor.movePageDown(shiftPressed);
    }
    else if (key == VK_DELETE) {
        editor.deleteCharacter();
        updateTitle();
    }

    ensureCursorVisible();
    cursorVisible = true;

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::handleCharacter(
    WPARAM character
) {
    bool controlPressed =
        (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    bool altPressed =
        (GetKeyState(VK_MENU) & 0x8000) != 0;

    bool rightAltPressed =
        (GetKeyState(VK_RMENU) & 0x8000) != 0;

    // Az AltGr magyar billentyuzeten Ctrl+Alt-kent is megjelenhet.
    // A karaktert ilyenkor at kell engedni a szerkesztonek.
    bool altGrPressed =
        rightAltPressed ||
        (controlPressed && altPressed);

    if (controlPressed && !altGrPressed) {
        return;
    }

    if (character == 8) {
        editor.backspace();
    }
    else if (character == 13) {
        editor.insertNewLine();
    }
    else if (character == 9) {
        editor.insertTab();
    }
    else if (
        character >= 32 &&
        character != 127
    ) {
        editor.insertCharacter(
            static_cast<char>(character)
        );
    }

    ensureCursorVisible();

    cursorVisible = true;
    updateTitle();

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::pointToEditorPosition(
    int x,
    int y,
    int& line,
    int& column
) const {
    Layout layout = calculateLayout();

    const std::vector<std::string>& lines =
        editor.getLines();

    line =
        editor.getFirstVisibleLine() +
        (
            y -
            layout.editor.top -
            5
        ) /
        charHeight;

    line = hydroClampInt(
        line,
        0,
        static_cast<int>(lines.size()) - 1
    );

    int textStartX =
        layout.editor.left + 52;

    column =
        editor.getFirstVisibleColumn() +
        (
            x -
            textStartX +
            charWidth / 2
        ) /
        charWidth;

    column = hydroClampInt(
        column,
        0,
        static_cast<int>(lines[line].size())
    );
}

void HydroApplication::handleMouseDown(
    int x,
    int y
) {
    Layout layout = calculateLayout();

    RECT horizontalThumb =
        horizontalScrollThumb(layout);

    RECT verticalThumb =
        verticalScrollThumb(layout);

    POINT mousePoint;
    mousePoint.x = x;
    mousePoint.y = y;

    if (PtInRect(&horizontalThumb, mousePoint)) {
        draggingHorizontalScroll = true;
        scrollDragOffset = x - horizontalThumb.left;
        SetCapture(window);
        return;
    }

    if (PtInRect(&verticalThumb, mousePoint)) {
        draggingVerticalScroll = true;
        scrollDragOffset = y - verticalThumb.top;
        SetCapture(window);
        return;
    }

    if (PtInRect(&layout.horizontalScroll, mousePoint)) {
        draggingHorizontalScroll = true;
        scrollDragOffset =
            (horizontalThumb.right - horizontalThumb.left) / 2;
        updateHorizontalScrollFromMouse(x, layout);
        SetCapture(window);
        return;
    }

    if (PtInRect(&layout.verticalScroll, mousePoint)) {
        draggingVerticalScroll = true;
        scrollDragOffset =
            (verticalThumb.bottom - verticalThumb.top) / 2;
        updateVerticalScrollFromMouse(y, layout);
        SetCapture(window);
        return;
    }

    if (
        x < layout.editorContent.left ||
        x >= layout.editorContent.right ||
        y < layout.editorContent.top ||
        y >= layout.editorContent.bottom
    ) {
        return;
    }

    int line = 0;
    int column = 0;

    pointToEditorPosition(
        x,
        y,
        line,
        column
    );

    editor.clearSelection();
    editor.setCursor(line, column);
    editor.beginSelection();

    mouseSelecting = true;
    SetCapture(window);

    cursorVisible = true;

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::handleMouseMove(
    int x,
    int y,
    WPARAM buttons
) {
    Layout layout = calculateLayout();

    if (
        draggingHorizontalScroll &&
        (buttons & MK_LBUTTON) != 0
    ) {
        updateHorizontalScrollFromMouse(x, layout);
        InvalidateRect(window, NULL, FALSE);
        return;
    }

    if (
        draggingVerticalScroll &&
        (buttons & MK_LBUTTON) != 0
    ) {
        updateVerticalScrollFromMouse(y, layout);
        InvalidateRect(window, NULL, FALSE);
        return;
    }

    if (
        !mouseSelecting ||
        (buttons & MK_LBUTTON) == 0
    ) {
        return;
    }

    int line = 0;
    int column = 0;

    pointToEditorPosition(
        x,
        y,
        line,
        column
    );

    editor.setCursor(line, column);
    editor.updateSelectionToCursor();

    ensureCursorVisible();

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::handleMouseUp() {
    if (
        draggingHorizontalScroll ||
        draggingVerticalScroll
    ) {
        draggingHorizontalScroll = false;
        draggingVerticalScroll = false;
        ReleaseCapture();
        InvalidateRect(window, NULL, FALSE);
        return;
    }

    if (!mouseSelecting) {
        return;
    }

    mouseSelecting = false;
    ReleaseCapture();

    if (!editor.hasSelection()) {
        editor.clearSelection();
    }

    InvalidateRect(window, NULL, FALSE);
}

void HydroApplication::handleMouseWheel(
    short delta
) {
    Layout layout = calculateLayout();
    int steps = delta / WHEEL_DELTA;

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        int visibleColumns = hydroMaxInt(
            1,
            (
                layout.editorContent.right -
                layout.editorContent.left -
                58
            ) /
            charWidth
        );

        int maximum = hydroMaxInt(
            0,
            editor.getMaximumLineLength() +
            1 -
            visibleColumns
        );

        editor.setFirstVisibleColumn(
            hydroClampInt(
                editor.getFirstVisibleColumn() -
                steps * 4,
                0,
                maximum
            )
        );
    }
    else {
        int visibleLines = hydroMaxInt(
            1,
            (
                layout.editorContent.bottom -
                layout.editorContent.top -
                10
            ) /
            charHeight
        );

        int maximum = hydroMaxInt(
            0,
            static_cast<int>(editor.getLines().size()) -
            visibleLines
        );

        editor.setFirstVisibleLine(
            hydroClampInt(
                editor.getFirstVisibleLine() -
                steps * 3,
                0,
                maximum
            )
        );
    }

    InvalidateRect(window, NULL, FALSE);
}

RECT HydroApplication::horizontalScrollThumb(
    const Layout& layout
) const {
    RECT thumb = layout.horizontalScroll;

    int trackWidth =
        layout.horizontalScroll.right -
        layout.horizontalScroll.left;

    int visibleColumns = hydroMaxInt(
        1,
        (
            layout.editorContent.right -
            layout.editorContent.left -
            58
        ) /
        charWidth
    );

    int totalColumns = hydroMaxInt(
        visibleColumns,
        editor.getMaximumLineLength() + 1
    );

    int thumbWidth = hydroMaxInt(
        28,
        trackWidth * visibleColumns / totalColumns
    );

    thumbWidth = hydroClampInt(
        thumbWidth,
        1,
        trackWidth
    );

    int maximumScroll =
        hydroMaxInt(0, totalColumns - visibleColumns);

    int travel = hydroMaxInt(0, trackWidth - thumbWidth);
    int offset = 0;

    if (maximumScroll > 0) {
        offset =
            travel *
            hydroClampInt(
                editor.getFirstVisibleColumn(),
                0,
                maximumScroll
            ) /
            maximumScroll;
    }

    thumb.left =
        layout.horizontalScroll.left + offset;
    thumb.right = thumb.left + thumbWidth;
    thumb.top += 2;
    thumb.bottom -= 2;

    return thumb;
}

RECT HydroApplication::verticalScrollThumb(
    const Layout& layout
) const {
    RECT thumb = layout.verticalScroll;

    int trackHeight =
        layout.verticalScroll.bottom -
        layout.verticalScroll.top;

    int visibleLines = hydroMaxInt(
        1,
        (
            layout.editorContent.bottom -
            layout.editorContent.top -
            10
        ) /
        charHeight
    );

    int totalLines = hydroMaxInt(
        visibleLines,
        static_cast<int>(editor.getLines().size())
    );

    int thumbHeight = hydroMaxInt(
        28,
        trackHeight * visibleLines / totalLines
    );

    thumbHeight = hydroClampInt(
        thumbHeight,
        1,
        trackHeight
    );

    int maximumScroll =
        hydroMaxInt(0, totalLines - visibleLines);

    int travel =
        hydroMaxInt(0, trackHeight - thumbHeight);

    int offset = 0;

    if (maximumScroll > 0) {
        offset =
            travel *
            hydroClampInt(
                editor.getFirstVisibleLine(),
                0,
                maximumScroll
            ) /
            maximumScroll;
    }

    thumb.top =
        layout.verticalScroll.top + offset;
    thumb.bottom = thumb.top + thumbHeight;
    thumb.left += 2;
    thumb.right -= 2;

    return thumb;
}

void HydroApplication::updateHorizontalScrollFromMouse(
    int x,
    const Layout& layout
) {
    RECT thumb = horizontalScrollThumb(layout);

    int thumbWidth = thumb.right - thumb.left;
    int trackWidth =
        layout.horizontalScroll.right -
        layout.horizontalScroll.left;

    int travel = hydroMaxInt(1, trackWidth - thumbWidth);

    int position =
        x -
        scrollDragOffset -
        layout.horizontalScroll.left;

    position = hydroClampInt(position, 0, travel);

    int visibleColumns = hydroMaxInt(
        1,
        (
            layout.editorContent.right -
            layout.editorContent.left -
            58
        ) /
        charWidth
    );

    int maximumScroll = hydroMaxInt(
        0,
        editor.getMaximumLineLength() +
        1 -
        visibleColumns
    );

    editor.setFirstVisibleColumn(
        maximumScroll * position / travel
    );
}

void HydroApplication::updateVerticalScrollFromMouse(
    int y,
    const Layout& layout
) {
    RECT thumb = verticalScrollThumb(layout);

    int thumbHeight = thumb.bottom - thumb.top;
    int trackHeight =
        layout.verticalScroll.bottom -
        layout.verticalScroll.top;

    int travel = hydroMaxInt(1, trackHeight - thumbHeight);

    int position =
        y -
        scrollDragOffset -
        layout.verticalScroll.top;

    position = hydroClampInt(position, 0, travel);

    int visibleLines = hydroMaxInt(
        1,
        (
            layout.editorContent.bottom -
            layout.editorContent.top -
            10
        ) /
        charHeight
    );

    int maximumScroll = hydroMaxInt(
        0,
        static_cast<int>(editor.getLines().size()) -
        visibleLines
    );

    editor.setFirstVisibleLine(
        maximumScroll * position / travel
    );
}

void HydroApplication::loadInitialDocument() {
    std::string samplePath =
        hydroExecutableDirectory() +
        "\\pelda.e";

    if (editor.loadFile(samplePath)) {
        currentFilePath = samplePath;
    }
    else {
        editor.newDocument();
        currentFilePath = "";
    }

    setOutput(
        "E Temple IDE v4.17 keszen all.\r\n"
        "Ctrl+N uj .e fajl, Ctrl+O megnyitas, Ctrl+S mentes.\r\n"
        "Ctrl+A/C/X/V es egeres kijeloles mukodik.\r\n"
        "Az E nyelv uj rnd, adcu, tiw, elz, logikai operator es var? elemei aktivak."
    );
}

void HydroApplication::updateTitle() {
    std::string title =
        "E Temple IDE v4.17 - " +
        displayFileName();

    if (editor.isModified()) {
        title += " *";
    }

    SetWindowTextA(
        window,
        title.c_str()
    );
}

bool HydroApplication::initialize(
    HINSTANCE instance,
    int showCommand
) {
    WNDCLASSEXA windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));

    windowClass.cbSize = sizeof(windowClass);
    windowClass.style =
        CS_HREDRAW |
        CS_VREDRAW;

    windowClass.lpfnWndProc =
        HydroApplication::staticWindowProcedure;

    windowClass.hInstance = instance;
    windowClass.hCursor =
        LoadCursor(NULL, IDC_IBEAM);

    windowClass.hIcon =
        LoadIconA(instance, MAKEINTRESOURCEA(101));
    if (windowClass.hIcon == NULL) windowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;

    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1
        );

    windowClass.lpszClassName =
        WINDOW_CLASS_NAME;

    if (!RegisterClassExA(&windowClass)) {
        return false;
    }

    window = CreateWindowExA(
        0,
        WINDOW_CLASS_NAME,
        "E Temple IDE v4.17",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        820,
        NULL,
        NULL,
        instance,
        this
    );

    if (window == NULL) {
        return false;
    }

    normalFont = CreateFontA(
        -18,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,
        FIXED_PITCH | FF_MODERN,
        "Terminal"
    );

    headingFont = CreateFontA(
        -18,
        0,
        0,
        0,
        FW_HEAVY,
        FALSE,
        FALSE,
        FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,
        FIXED_PITCH | FF_MODERN,
        "Terminal"
    );

    HDC dc = GetDC(window);

    SelectObject(dc, normalFont);

    TEXTMETRICA metrics;
    GetTextMetricsA(dc, &metrics);

    charWidth = metrics.tmAveCharWidth;
    charHeight = metrics.tmHeight;

    ReleaseDC(window, dc);

    SetTimer(window, 1, 500, NULL);
    SetTimer(window, 2, 1000, NULL);

    loadInitialDocument();
    updateTitle();

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetFocus(window);

    return true;
}

int HydroApplication::run() {
    MSG message;

    while (
        GetMessageA(
            &message,
            NULL,
            0,
            0
        ) > 0
    ) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK
HydroApplication::staticWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    HydroApplication* application = NULL;

    if (message == WM_NCCREATE) {
        CREATESTRUCTA* create =
            reinterpret_cast<CREATESTRUCTA*>(
                lParam
            );

        application =
            reinterpret_cast<HydroApplication*>(
                create->lpCreateParams
            );

        SetWindowLongPtrA(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                application
            )
        );

        application->window = window;
    }
    else {
        application =
            reinterpret_cast<HydroApplication*>(
                GetWindowLongPtrA(
                    window,
                    GWLP_USERDATA
                )
            );
    }

    if (application != NULL) {
        return application->windowProcedure(
            message,
            wParam,
            lParam
        );
    }

    return DefWindowProcA(
        window,
        message,
        wParam,
        lParam
    );
}

LRESULT HydroApplication::windowProcedure(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == WM_SIZE) {
        clientWidth = LOWORD(lParam);
        clientHeight = HIWORD(lParam);

        ensureCursorVisible();

        InvalidateRect(window, NULL, FALSE);
        return 0;
    }

    if (message == WM_ERASEBKGND) {
        return 1;
    }

    if (message == WM_PAINT) {
        paint();
        return 0;
    }

    if (message == WM_TIMER) {
        if (wParam == 1) {
            cursorVisible = !cursorVisible;
            InvalidateRect(window, NULL, FALSE);
        }
        else if (wParam == 2) {
            consumeRuntimeAnnotations();
        }

        return 0;
    }

    if (message == WM_KEYDOWN) {
        handleKeyDown(wParam);
        return 0;
    }

    if (
        message == WM_CHAR ||
        message == WM_SYSCHAR
    ) {
        handleCharacter(wParam);
        return 0;
    }

    if (message == WM_LBUTTONDOWN) {
        int mx=GET_X_LPARAM(lParam), my=GET_Y_LPARAM(lParam);
        if (my < TOP_BAR_HEIGHT && mx >= 500 && mx <= 650) {
            int answer=MessageBoxA(window,"IKONAIN2 grafikus konyvtar\n\nTelepited most?","E Books",MB_YESNO|MB_ICONINFORMATION);
            if(answer==IDYES){std::string dir=hydroExecutableDirectory()+"\\ebooks";CreateDirectoryA(dir.c_str(),NULL);hydroWriteTextFile(dir+"\\IKONAIN2.installed","IKONAIN2 1.0 installed");MessageBoxA(window,"Az IKONAIN2 telepitve. Most mar hasznalhatod: #kontarb :IKONAIN2:","E Books",MB_OK|MB_ICONINFORMATION);}
            return 0;
        }
        handleMouseDown(
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        );

        return 0;
    }

    if (message == WM_MOUSEMOVE) {
        handleMouseMove(
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            wParam
        );

        return 0;
    }

    if (message == WM_LBUTTONUP) {
        handleMouseUp();
        return 0;
    }

    if (message == WM_MOUSEWHEEL) {
        handleMouseWheel(
            GET_WHEEL_DELTA_WPARAM(wParam)
        );

        return 0;
    }

    if (message == WM_CLOSE) {
        if (!confirmDiscardChanges()) {
            return 0;
        }

        DestroyWindow(window);
        return 0;
    }

    if (message == WM_DESTROY) {
        KillTimer(window, 1);
        KillTimer(window, 2);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(
        window,
        message,
        wParam,
        lParam
    );
}
