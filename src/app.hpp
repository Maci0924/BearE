#pragma once

#include "common.hpp"
#include "editor.hpp"

class HydroApplication {
private:
    HWND window;
    HFONT normalFont;
    HFONT headingFont;

    HydroEditor editor;
    std::vector<std::string> outputLines;

    int clientWidth;
    int clientHeight;
    int charWidth;
    int charHeight;

    bool cursorVisible;
    bool mouseSelecting;
    bool draggingHorizontalScroll;
    bool draggingVerticalScroll;
    int scrollDragOffset;

    std::string currentFilePath;

    struct Layout {
        RECT topBar;
        RECT infoBar;
        RECT editor;
        RECT editorContent;
        RECT horizontalScroll;
        RECT verticalScroll;
        RECT rightPanel;
        RECT output;
        RECT status;
    };

    Layout calculateLayout() const;

    void paint();
    void drawScene(HDC dc);

    void drawTopBar(HDC dc, const Layout& layout);
    void drawInfoBar(HDC dc, const Layout& layout);
    void drawEditor(HDC dc, const Layout& layout);
    void drawEditorScrollbars(
        HDC dc,
        const Layout& layout
    );
    void drawRightPanel(HDC dc, const Layout& layout);
    void drawOutput(HDC dc, const Layout& layout);
    void drawStatus(HDC dc, const Layout& layout);

    void drawCodeLine(
        HDC dc,
        int x,
        int y,
        int lineIndex,
        const std::string& line
    );

    void fillRectColor(
        HDC dc,
        const RECT& rect,
        COLORREF color
    );

    void drawBorder(
        HDC dc,
        const RECT& rect,
        COLORREF color,
        int thickness
    );

    void setOutput(const std::string& text);
    void appendOutput(const std::string& text);
    void appendCapturedOutput(const std::string& text);

    bool confirmDiscardChanges();
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();

    void copySelection();
    void cutSelection();
    void pasteClipboard();

    std::string displayFileName() const;

    void buildCurrentSource();
    void runProgram();
    void consumeRuntimeAnnotations();

    void handleKeyDown(WPARAM key);
    void handleCharacter(WPARAM character);
    void handleMouseDown(int x, int y);
    void handleMouseMove(int x, int y, WPARAM buttons);
    void handleMouseUp();
    void handleMouseWheel(short delta);

    RECT horizontalScrollThumb(
        const Layout& layout
    ) const;

    RECT verticalScrollThumb(
        const Layout& layout
    ) const;

    void updateHorizontalScrollFromMouse(
        int x,
        const Layout& layout
    );

    void updateVerticalScrollFromMouse(
        int y,
        const Layout& layout
    );

    void pointToEditorPosition(
        int x,
        int y,
        int& line,
        int& column
    ) const;

    void loadInitialDocument();
    void updateTitle();
    void ensureCursorVisible();

public:
    HydroApplication();
    ~HydroApplication();

    bool initialize(
        HINSTANCE instance,
        int showCommand
    );

    int run();

    static LRESULT CALLBACK staticWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT windowProcedure(
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );
};
