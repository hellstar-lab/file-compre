// Windows-only GUI implementation. Guard to prevent diagnostics on non-Windows systems.
#ifdef _WIN32
#include "../include/compressor.h"
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

// Window controls IDs
#define ID_BUTTON_BROWSE_INPUT      1001
#define ID_BUTTON_BROWSE_OUTPUT     1002
#define ID_BUTTON_COMPRESS          1003
#define ID_BUTTON_DECOMPRESS        1004
#define ID_COMBO_ALGORITHM          1005
#define ID_COMBO_LEVEL              1006
#define ID_EDIT_INPUT_PATH          1007
#define ID_EDIT_OUTPUT_PATH         1008
#define ID_PROGRESS_BAR             1009
#define ID_LISTVIEW_STATS           1010
#define ID_BUTTON_CLEAR_STATS       1011
#define ID_BUTTON_EXPORT_STATS      1012

// Window dimensions
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define MARGIN          20
#define BUTTON_HEIGHT   30
#define EDIT_HEIGHT     25
#define COMBO_HEIGHT    25

// Global variables
HWND g_hMainWindow;
HWND g_hInputEdit, g_hOutputEdit;
HWND g_hAlgorithmCombo, g_hLevelCombo;
HWND g_hProgressBar, g_hStatsListView;
HWND g_hCompressButton, g_hDecompressButton;
HFONT g_hFont, g_hBoldFont;

// Statistics storage
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char algorithm[32];
    char level[16];
    long original_size;
    long compressed_size;
    double ratio;
    double speed;
    double memory;
    char timestamp[32];
} StatsEntry;

StatsEntry g_stats[100];
int g_stats_count = 0;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void BrowseForFile(HWND hwnd, BOOL isInput);
void PerformCompression(HWND hwnd);
void PerformDecompression(HWND hwnd);
void UpdateStatsDisplay();
void AddStatsEntry(const CompressionStats* stats);
void ClearStats();
void ExportStats();

// EnumChildWindows callback to set font on all child controls
BOOL CALLBACK SetChildFontProc(HWND hwndChild, LPARAM lParam) {
    (void)lParam;
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return TRUE;
}

// Main GUI entry point
int StartGUI() {
    WNDCLASSEX wc = {0};
    MSG msg;
    
    // Initialize common controls (use legacy initialization for broader header compatibility)
    InitCommonControls();
    
    // Register window class
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FileCompressorGUI";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassEx(&wc)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Create main window
    g_hMainWindow = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"FileCompressorGUI",
        L"Professional File Compressor v2.0",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (!g_hMainWindow) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    ShowWindow(g_hMainWindow, SW_SHOWDEFAULT);
    UpdateWindow(g_hMainWindow);
    
    // Message loop
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return msg.wParam;
}

// Entry point for GUI executable
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    return StartGUI();
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            break;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_BUTTON_BROWSE_INPUT:
                    BrowseForFile(hwnd, TRUE);
                    break;
                    
                case ID_BUTTON_BROWSE_OUTPUT:
                    BrowseForFile(hwnd, FALSE);
                    break;
                    
                case ID_BUTTON_COMPRESS:
                    PerformCompression(hwnd);
                    break;
                    
                case ID_BUTTON_DECOMPRESS:
                    PerformDecompression(hwnd);
                    break;
                    
                case ID_BUTTON_CLEAR_STATS:
                    ClearStats();
                    break;
                    
                case ID_BUTTON_EXPORT_STATS:
                    ExportStats();
                    break;
            }
            break;
            
        case WM_SIZE:
            // Handle window resizing
            if (wParam != SIZE_MINIMIZED) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                // Resize controls proportionally
                MoveWindow(g_hStatsListView, MARGIN, 200, rect.right - 2*MARGIN, rect.bottom - 250, TRUE);
            }
            break;
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            DeleteObject(g_hFont);
            DeleteObject(g_hBoldFont);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
 

// Create all GUI controls
void CreateControls(HWND hwnd) {
    // Create fonts
    g_hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
    g_hBoldFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
    int y = MARGIN;
    
    // Title
    HWND hTitle = CreateWindow(L"STATIC", L"Professional File Compressor",
                              WS_VISIBLE | WS_CHILD | SS_CENTER,
                              MARGIN, y, WINDOW_WIDTH - 2*MARGIN, 30,
                              hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
    y += 40;
    
    // Input file section
    CreateWindow(L"STATIC", L"Input File:",
                WS_VISIBLE | WS_CHILD,
                MARGIN, y, 100, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
    
    g_hInputEdit = CreateWindow(L"EDIT", L"",
                               WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                               MARGIN + 100, y, 400, EDIT_HEIGHT,
                               hwnd, (HMENU)ID_EDIT_INPUT_PATH, GetModuleHandle(NULL), NULL);
    
    CreateWindow(L"BUTTON", L"Browse...",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                MARGIN + 520, y, 80, BUTTON_HEIGHT,
                hwnd, (HMENU)ID_BUTTON_BROWSE_INPUT, GetModuleHandle(NULL), NULL);
    y += 40;
    
    // Output file section
    CreateWindow(L"STATIC", L"Output File:",
                WS_VISIBLE | WS_CHILD,
                MARGIN, y, 100, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
    
    g_hOutputEdit = CreateWindow(L"EDIT", L"",
                                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                MARGIN + 100, y, 400, EDIT_HEIGHT,
                                hwnd, (HMENU)ID_EDIT_OUTPUT_PATH, GetModuleHandle(NULL), NULL);
    
    CreateWindow(L"BUTTON", L"Browse...",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                MARGIN + 520, y, 80, BUTTON_HEIGHT,
                hwnd, (HMENU)ID_BUTTON_BROWSE_OUTPUT, GetModuleHandle(NULL), NULL);
    y += 40;
    
    // Algorithm selection
    CreateWindow(L"STATIC", L"Algorithm:",
                WS_VISIBLE | WS_CHILD,
                MARGIN, y, 100, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
    
    g_hAlgorithmCombo = CreateWindow(L"COMBOBOX", L"",
                                    WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
                                    MARGIN + 100, y, 150, 200,
                                    hwnd, (HMENU)ID_COMBO_ALGORITHM, GetModuleHandle(NULL), NULL);
    
    // Add algorithm options
    SendMessage(g_hAlgorithmCombo, CB_ADDSTRING, 0, (LPARAM)L"Huffman Coding");
    SendMessage(g_hAlgorithmCombo, CB_ADDSTRING, 0, (LPARAM)L"LZ77");
    SendMessage(g_hAlgorithmCombo, CB_ADDSTRING, 0, (LPARAM)L"LZW");
    SendMessage(g_hAlgorithmCombo, CB_ADDSTRING, 0, (LPARAM)L"Advanced Audio");
    SendMessage(g_hAlgorithmCombo, CB_ADDSTRING, 0, (LPARAM)L"Advanced Image");
    SendMessage(g_hAlgorithmCombo, CB_SETCURSEL, 0, 0);
    
    // Compression level
    CreateWindow(L"STATIC", L"Level:",
                WS_VISIBLE | WS_CHILD,
                MARGIN + 270, y, 50, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
    
    g_hLevelCombo = CreateWindow(L"COMBOBOX", L"",
                                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
                                MARGIN + 320, y, 100, 200,
                                hwnd, (HMENU)ID_COMBO_LEVEL, GetModuleHandle(NULL), NULL);
    
    // Add level options
    SendMessage(g_hLevelCombo, CB_ADDSTRING, 0, (LPARAM)L"Fast");
    SendMessage(g_hLevelCombo, CB_ADDSTRING, 0, (LPARAM)L"Normal");
    SendMessage(g_hLevelCombo, CB_ADDSTRING, 0, (LPARAM)L"High");
    SendMessage(g_hLevelCombo, CB_ADDSTRING, 0, (LPARAM)L"Ultra");
    SendMessage(g_hLevelCombo, CB_SETCURSEL, 1, 0);
    y += 40;
    
    // Action buttons
    g_hCompressButton = CreateWindow(L"BUTTON", L"Compress",
                                    WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                    MARGIN, y, 100, BUTTON_HEIGHT,
                                    hwnd, (HMENU)ID_BUTTON_COMPRESS, GetModuleHandle(NULL), NULL);
    
    g_hDecompressButton = CreateWindow(L"BUTTON", L"Decompress",
                                      WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                      MARGIN + 120, y, 100, BUTTON_HEIGHT,
                                      hwnd, (HMENU)ID_BUTTON_DECOMPRESS, GetModuleHandle(NULL), NULL);
    
    // Progress bar
    g_hProgressBar = CreateWindow(PROGRESS_CLASS, NULL,
                                 WS_VISIBLE | WS_CHILD,
                                 MARGIN + 240, y, 200, BUTTON_HEIGHT,
                                 hwnd, (HMENU)ID_PROGRESS_BAR, GetModuleHandle(NULL), NULL);
    y += 50;
    
    // Statistics section
    CreateWindow(L"STATIC", L"Compression Statistics:",
                WS_VISIBLE | WS_CHILD,
                MARGIN, y, 200, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
    
    CreateWindow(L"BUTTON", L"Clear",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                MARGIN + 220, y - 5, 60, 25,
                hwnd, (HMENU)ID_BUTTON_CLEAR_STATS, GetModuleHandle(NULL), NULL);
    
    CreateWindow(L"BUTTON", L"Export",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                MARGIN + 290, y - 5, 60, 25,
                hwnd, (HMENU)ID_BUTTON_EXPORT_STATS, GetModuleHandle(NULL), NULL);
    y += 30;
    
    // Statistics list view
    g_hStatsListView = CreateWindow(WC_LISTVIEW, L"",
                                   WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
                                   MARGIN, y, WINDOW_WIDTH - 2*MARGIN, 250,
                                   hwnd, (HMENU)ID_LISTVIEW_STATS, GetModuleHandle(NULL), NULL);
    
    // Set up list view columns
    LVCOLUMN lvc;
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    
    lvc.iSubItem = 0; lvc.cx = 120; lvc.pszText = L"Filename";
    ListView_InsertColumn(g_hStatsListView, 0, &lvc);
    
    lvc.iSubItem = 1; lvc.cx = 80; lvc.pszText = L"Algorithm";
    ListView_InsertColumn(g_hStatsListView, 1, &lvc);
    
    lvc.iSubItem = 2; lvc.cx = 50; lvc.pszText = L"Level";
    ListView_InsertColumn(g_hStatsListView, 2, &lvc);
    
    lvc.iSubItem = 3; lvc.cx = 80; lvc.pszText = L"Original (KB)";
    ListView_InsertColumn(g_hStatsListView, 3, &lvc);
    
    lvc.iSubItem = 4; lvc.cx = 80; lvc.pszText = L"Compressed (KB)";
    ListView_InsertColumn(g_hStatsListView, 4, &lvc);
    
    lvc.iSubItem = 5; lvc.cx = 60; lvc.pszText = L"Ratio %";
    ListView_InsertColumn(g_hStatsListView, 5, &lvc);
    
    lvc.iSubItem = 6; lvc.cx = 70; lvc.pszText = L"Speed (MB/s)";
    ListView_InsertColumn(g_hStatsListView, 6, &lvc);
    
    lvc.iSubItem = 7; lvc.cx = 70; lvc.pszText = L"Memory (MB)";
    ListView_InsertColumn(g_hStatsListView, 7, &lvc);
    
    lvc.iSubItem = 8; lvc.cx = 120; lvc.pszText = L"Timestamp";
    ListView_InsertColumn(g_hStatsListView, 8, &lvc);
    
    // Set fonts for all controls
    EnumChildWindows(hwnd, (WNDENUMPROC)SetChildFontProc, 0);
}

// Browse for input/output files
void BrowseForFile(HWND hwnd, BOOL isInput) {
    OPENFILENAME ofn;
    wchar_t szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    
    if (isInput) {
        ofn.lpstrFilter = L"All Files\0*.*\0Audio Files\0*.wav\0Image Files\0*.bmp\0Text Files\0*.txt\0";
        ofn.lpstrTitle = L"Select Input File";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        
        if (GetOpenFileName(&ofn)) {
            SetWindowText(g_hInputEdit, szFile);
            
            // Auto-generate output filename
            wchar_t outputPath[260];
            wcsncpy(outputPath, szFile, 259);
            outputPath[259] = L'\0';
            size_t len = wcslen(outputPath);
            wcsncat(outputPath, L".comp", 259 - len);
            SetWindowText(g_hOutputEdit, outputPath);
        }
    } else {
        ofn.lpstrFilter = L"Compressed Files\0*.comp\0All Files\0*.*\0";
        ofn.lpstrTitle = L"Select Output Location";
        ofn.Flags = OFN_PATHMUSTEXIST;
        
        if (GetSaveFileName(&ofn)) {
            SetWindowText(g_hOutputEdit, szFile);
        }
    }
}

// Perform compression operation
void PerformCompression(HWND hwnd) {
    wchar_t inputPath[260], outputPath[260];
    char inputPathA[260], outputPathA[260];
    
    GetWindowTextW(g_hInputEdit, inputPath, 260);
    GetWindowTextW(g_hOutputEdit, outputPath, 260);
    
    if (wcslen(inputPath) == 0 || wcslen(outputPath) == 0) {
        MessageBox(hwnd, L"Please select input and output files.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Convert to ANSI
    WideCharToMultiByte(CP_ACP, 0, inputPath, -1, inputPathA, 260, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, outputPath, -1, outputPathA, 260, NULL, NULL);
    
    // Get selected algorithm and level
    int algoIndex = SendMessage(g_hAlgorithmCombo, CB_GETCURSEL, 0, 0);
    int levelIndex = SendMessage(g_hLevelCombo, CB_GETCURSEL, 0, 0);
    
    CompressionAlgorithm algo = (CompressionAlgorithm)algoIndex;
    CompressionLevel level = (CompressionLevel)(levelIndex + 1);
    
    // Disable controls during compression
    EnableWindow(g_hCompressButton, FALSE);
    EnableWindow(g_hDecompressButton, FALSE);
    #ifdef PBM_SETMARQUEE
    SendMessage(g_hProgressBar, PBM_SETMARQUEE, TRUE, 50);
    #endif
    
    // Build command line: universal_comp.exe <input> <output_dir>
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "universal_comp.exe \"%s\" \"output\"", inputPathA);

    // Run compression synchronously
    FILE* pipe = _popen(cmd, "r");
    int result = -1;
    if (pipe) {
      // wait for completion
      int exitCode = _pclose(pipe);
      result = (exitCode == 0) ? 0 : -1;
    }

    // Fill dummy stats (GUI expects them)
    CompressionStats stats = {0};
    strncpy(stats.original_filename, inputPathA, sizeof(stats.original_filename)-1);
    stats.algorithm_used = algo;
    stats.compression_level = level;
    stats.original_size = 0;
    stats.compressed_size = 0;
    stats.compression_ratio = 0.0;
    stats.compression_speed = 0.0;
    stats.memory_usage = 0.0;
    stats.compression_time = time(NULL);
    
    // Re-enable controls
    EnableWindow(g_hCompressButton, TRUE);
    EnableWindow(g_hDecompressButton, TRUE);
    #ifdef PBM_SETMARQUEE
    SendMessage(g_hProgressBar, PBM_SETMARQUEE, FALSE, 0);
    #endif
    
    if (result == 0) {
        MessageBoxW(hwnd, L"Compression completed successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
        AddStatsEntry(&stats);
        UpdateStatsDisplay();
    } else {
        MessageBoxW(hwnd, L"Compression failed!", L"Error", MB_OK | MB_ICONERROR);
    }
}

// Perform decompression operation
void PerformDecompression(HWND hwnd) {
    wchar_t inputPath[260], outputPath[260];
    char inputPathA[260], outputPathA[260];
    
    GetWindowTextW(g_hInputEdit, inputPath, 260);
    GetWindowTextW(g_hOutputEdit, outputPath, 260);
    
    if (wcslen(inputPath) == 0 || wcslen(outputPath) == 0) {
        MessageBoxW(hwnd, L"Please select input and output files.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Convert to ANSI
    WideCharToMultiByte(CP_ACP, 0, inputPath, -1, inputPathA, 260, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, outputPath, -1, outputPathA, 260, NULL, NULL);
    
    // Disable controls during decompression
    EnableWindow(g_hCompressButton, FALSE);
    EnableWindow(g_hDecompressButton, FALSE);
    #ifdef PBM_SETMARQUEE
    SendMessage(g_hProgressBar, PBM_SETMARQUEE, TRUE, 50);
    #endif
    
    // Build command line: universal_comp.exe -d <input.comp> <output_dir>
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "universal_comp.exe -d \"%s\" \"decompressed\"", inputPathA);

    // Run decompression synchronously
    FILE* pipe = _popen(cmd, "r");
    int result = -1;
    if (pipe) {
      int exitCode = _pclose(pipe);
      result = (exitCode == 0) ? 0 : -1;
    }
    
    // Re-enable controls
    EnableWindow(g_hCompressButton, TRUE);
    EnableWindow(g_hDecompressButton, TRUE);
    #ifdef PBM_SETMARQUEE
    SendMessage(g_hProgressBar, PBM_SETMARQUEE, FALSE, 0);
    #endif
    
    if (result == 0) {
        MessageBoxW(hwnd, L"Decompression completed successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, L"Decompression failed!", L"Error", MB_OK | MB_ICONERROR);
    }
}

// Add statistics entry
void AddStatsEntry(const CompressionStats* stats) {
    if (g_stats_count >= 100) return;
    
    StatsEntry* entry = &g_stats[g_stats_count++];
    strncpy(entry->filename, stats->original_filename, sizeof(entry->filename) - 1);
    entry->filename[sizeof(entry->filename) - 1] = '\0';
    
    // Convert algorithm to string
    switch (stats->algorithm_used) {
        case ALGO_HUFFMAN: strncpy(entry->algorithm, "Huffman", sizeof(entry->algorithm) - 1); break;
        case ALGO_LZ77: strncpy(entry->algorithm, "LZ77", sizeof(entry->algorithm) - 1); break;
        case ALGO_LZW: strncpy(entry->algorithm, "LZW", sizeof(entry->algorithm) - 1); break;
        case ALGO_AUDIO_ADVANCED: strncpy(entry->algorithm, "Audio Adv", sizeof(entry->algorithm) - 1); break;
        case ALGO_IMAGE_ADVANCED: strncpy(entry->algorithm, "Image Adv", sizeof(entry->algorithm) - 1); break;
        default: strncpy(entry->algorithm, "Unknown", sizeof(entry->algorithm) - 1); break;
    }
    entry->algorithm[sizeof(entry->algorithm) - 1] = '\0';
    
    snprintf(entry->level, sizeof(entry->level), "%d", stats->compression_level);
    entry->original_size = stats->original_size;
    entry->compressed_size = stats->compressed_size;
    entry->ratio = stats->compression_ratio;
    entry->speed = stats->compression_speed;
    entry->memory = stats->memory_usage;
    
    // Format timestamp
    struct tm* timeinfo = localtime(&stats->compression_time);
    strftime(entry->timestamp, sizeof(entry->timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
}

// Update statistics display
void UpdateStatsDisplay() {
    ListView_DeleteAllItems(g_hStatsListView);
    
    for (int i = 0; i < g_stats_count; i++) {
        LVITEM lvi;
        wchar_t buffer[256];
        
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        
        // Filename
        MultiByteToWideChar(CP_ACP, 0, g_stats[i].filename, -1, buffer, 256);
        lvi.pszText = buffer;
        ListView_InsertItem(g_hStatsListView, &lvi);
        
        // Algorithm
        MultiByteToWideChar(CP_ACP, 0, g_stats[i].algorithm, -1, buffer, 256);
        ListView_SetItemText(g_hStatsListView, i, 1, buffer);
        
        // Level
        MultiByteToWideChar(CP_ACP, 0, g_stats[i].level, -1, buffer, 256);
        ListView_SetItemText(g_hStatsListView, i, 2, buffer);
        
        // Original size (KB)
        _snwprintf(buffer, 256, L"%.1f", g_stats[i].original_size / 1024.0);
        ListView_SetItemText(g_hStatsListView, i, 3, buffer);
        
        // Compressed size (KB)
        _snwprintf(buffer, 256, L"%.1f", g_stats[i].compressed_size / 1024.0);
        ListView_SetItemText(g_hStatsListView, i, 4, buffer);
        
        // Ratio
        _snwprintf(buffer, 256, L"%.1f", g_stats[i].ratio);
        ListView_SetItemText(g_hStatsListView, i, 5, buffer);
        
        // Speed
        _snwprintf(buffer, 256, L"%.2f", g_stats[i].speed);
        ListView_SetItemText(g_hStatsListView, i, 6, buffer);
        
        // Memory
        _snwprintf(buffer, 256, L"%.2f", g_stats[i].memory);
        ListView_SetItemText(g_hStatsListView, i, 7, buffer);
        
        // Timestamp
        MultiByteToWideChar(CP_ACP, 0, g_stats[i].timestamp, -1, buffer, 256);
        ListView_SetItemText(g_hStatsListView, i, 8, buffer);
    }
}

// Clear statistics
void ClearStats() {
    g_stats_count = 0;
    ListView_DeleteAllItems(g_hStatsListView);
}

// Export statistics to CSV
void ExportStats() {
    OPENFILENAME ofn;
    wchar_t szFile[260] = L"compression_stats.csv";
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
    ofn.lpstrTitle = L"Export Statistics";
    ofn.Flags = OFN_PATHMUSTEXIST;
    
    if (GetSaveFileName(&ofn)) {
        char szFileA[260];
        WideCharToMultiByte(CP_ACP, 0, szFile, -1, szFileA, sizeof(szFileA), NULL, NULL);
        FILE* file = fopen(szFileA, "w");
        if (file != NULL) {
            fprintf(file, "Filename,Algorithm,Level,Original Size (KB),Compressed Size (KB),Ratio %%,Speed (MB/s),Memory (MB),Timestamp\n");
            
            for (int i = 0; i < g_stats_count; i++) {
                fprintf(file, "%s,%s,%s,%.1f,%.1f,%.1f,%.2f,%.2f,%s\n",
                       g_stats[i].filename, g_stats[i].algorithm, g_stats[i].level,
                       g_stats[i].original_size / 1024.0, g_stats[i].compressed_size / 1024.0,
                       g_stats[i].ratio, g_stats[i].speed, g_stats[i].memory,
                       g_stats[i].timestamp);
            }
            
            fclose(file);
            MessageBoxW(g_hMainWindow, L"Statistics exported successfully!", L"Export Complete", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_hMainWindow, L"Failed to create export file!", L"Export Error", MB_OK | MB_ICONERROR);
        }
    }
}
#endif // _WIN32