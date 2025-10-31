#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <ctime>
#include <vector>
#include <queue>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <functional>

// 链接器指令 - 指定使用WinMain作为入口点
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
// 链接器指令 - 链接通用控件库
#pragma comment(lib, "comctl32.lib")

// 常量定义
const int CELL_SIZE = 20;
const int GRID_WIDTH = 40;
const int GRID_HEIGHT = 30;
const int WINDOW_WIDTH = GRID_WIDTH * CELL_SIZE + 520;
const int WINDOW_HEIGHT = GRID_HEIGHT * CELL_SIZE + 40;

// 单元格类型
enum CellType {
    CELL_EMPTY = 0,
    CELL_WALL = 1,
    CELL_START = 2,
    CELL_END = 3,
    CELL_PATH = 4,
    CELL_VISITED = 5,
    CELL_OPEN = 6
};

// 工具类型
enum ToolType {
    TOOL_WALL = 0,
    TOOL_START = 1,
    TOOL_END = 2,
    TOOL_ERASE = 3
};

// 节点结构
struct Node {
    int x, y;
    int g, h, f;
    Node* parent;

    Node(int x, int y) : x(x), y(y), g(0), h(0), f(0), parent(nullptr) {}

    bool operator>(const Node& other) const {
        return f > other.f;
    }
};

// 节点比较函数对象
struct NodeCompare {
    bool operator()(Node* a, Node* b) const {
        return a->f > b->f;
    }
};

// 全局变量
CellType grid[GRID_HEIGHT][GRID_WIDTH];
bool isRunning = false;
bool isPaused = false;
bool showVisited = true;
bool hasStart = false;
bool hasEnd = false;
bool pathFound = false;
int visualizationSpeed = 60;
POINT startPos = { -1, -1 };
POINT endPos = { -1, -1 };
HINSTANCE hInst;
HWND hMainWnd;
HWND hToolRadio[4];
HWND hStartButton, hStopButton, hPauseButton, hClearButton, hRandomButton, hSaveButton, hLoadButton;
HWND hSpeedTrackbar, hSpeedLabel;
ToolType currentTool = TOOL_WALL;
HANDLE hAStarThread = NULL;

// 颜色定义
COLORREF GetCellColor(CellType type) {
    switch (type) {
    case CELL_EMPTY: return RGB(255, 255, 255);
    case CELL_WALL: return RGB(0, 0, 0);
    case CELL_START: return RGB(0, 255, 0);
    case CELL_END: return RGB(255, 0, 0);
    case CELL_PATH: return RGB(255, 255, 0);
    case CELL_VISITED: return RGB(173, 216, 230);
    case CELL_OPEN: return RGB(144, 238, 144);
    default: return RGB(255, 255, 255);
    }
}

// 计算启发式距离（曼哈顿距离）
int CalculateHeuristic(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

// 停止A*算法
void StopAStar() {
    if (isRunning) {
        isRunning = false;
        isPaused = false;
        if (hAStarThread) {
            // 给线程一些时间正常退出
            DWORD waitResult = WaitForSingleObject(hAStarThread, 2000);
            if (waitResult == WAIT_TIMEOUT) {
                // 如果线程超时未退出，强制终止
                TerminateThread(hAStarThread, 0);
            }
            CloseHandle(hAStarThread);
            hAStarThread = NULL;
        }

        for (int y = 0; y < GRID_HEIGHT; y++) {
            for (int x = 0; x < GRID_WIDTH; x++) {
                if (grid[y][x] != CELL_WALL && grid[y][x] != CELL_START && grid[y][x] != CELL_END) {
                    grid[y][x] = CELL_EMPTY;
                }
            }
        }

        InvalidateRect(hMainWnd, NULL, TRUE);
    }
}

// 清理节点内存的辅助函数 - 使用模板支持不同类型的优先队列
template<typename QueueType>
void CleanupNodes(QueueType& openSet) {
    while (!openSet.empty()) {
        delete openSet.top();
        openSet.pop();
    }
}

// A*算法实现
DWORD WINAPI AStarSearch(LPVOID lpParam) {
    pathFound = false;

    // 重置非墙、起点、终点的格子
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (grid[y][x] != CELL_WALL && grid[y][x] != CELL_START && grid[y][x] != CELL_END) {
                grid[y][x] = CELL_EMPTY;
            }
        }
    }

    std::priority_queue<Node*, std::vector<Node*>, NodeCompare> openSet;
    bool closedSet[GRID_HEIGHT][GRID_WIDTH] = { false };

    Node* startNode = new Node(startPos.x, startPos.y);
    startNode->h = CalculateHeuristic(startPos.x, startPos.y, endPos.x, endPos.y);
    startNode->f = startNode->h;
    openSet.push(startNode);

    int directions[8][2] = { {0,1}, {1,0}, {0,-1}, {-1,0}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };

    while (!openSet.empty() && !isPaused && isRunning) {
        Node* current = openSet.top();
        openSet.pop();

        if (current->x == endPos.x && current->y == endPos.y) {
            // 找到路径，回溯并显示动画
            pathFound = true;
            std::vector<Node*> pathNodes;
            Node* pathNode = current;

            // 收集路径节点
            while (pathNode->parent != nullptr) {
                pathNodes.push_back(pathNode);
                pathNode = pathNode->parent;
            }

            // 反向绘制路径动画（从起点到终点）
            for (int i = (int)pathNodes.size() - 1; i >= 0 && isRunning && !isPaused; i--) {
                Node* node = pathNodes[i];
                if (grid[node->y][node->x] != CELL_END) {
                    grid[node->y][node->x] = CELL_PATH;
                }

                RECT rect;
                rect.left = node->x * CELL_SIZE;
                rect.top = node->y * CELL_SIZE;
                rect.right = rect.left + CELL_SIZE;
                rect.bottom = rect.top + CELL_SIZE;
                InvalidateRect(hMainWnd, &rect, FALSE);
                UpdateWindow(hMainWnd);

                // 使用滑块控制的速度
                Sleep(visualizationSpeed);

                if (isPaused) {
                    while (isPaused && isRunning) {
                        Sleep(100);
                    }
                }
            }

            // 清理内存
            CleanupNodes(openSet);
            delete current;

            isRunning = false;
            isPaused = false;

            // 添加UI重绘
            RECT uiRect;
            uiRect.left = GRID_WIDTH * CELL_SIZE;
            uiRect.top = 0;
            uiRect.right = WINDOW_WIDTH;
            uiRect.bottom = WINDOW_HEIGHT;
            InvalidateRect(hMainWnd, &uiRect, TRUE);

            return 0;
        }

        closedSet[current->y][current->x] = true;

        if (grid[current->y][current->x] != CELL_START && showVisited) {
            grid[current->y][current->x] = CELL_VISITED;

            RECT rect;
            rect.left = current->x * CELL_SIZE;
            rect.top = current->y * CELL_SIZE;
            rect.right = rect.left + CELL_SIZE;
            rect.bottom = rect.top + CELL_SIZE;
            InvalidateRect(hMainWnd, &rect, FALSE);
        }

        for (int i = 0; i < 8; i++) {
            int newX = current->x + directions[i][0];
            int newY = current->y + directions[i][1];

            if (newX < 0 || newX >= GRID_WIDTH || newY < 0 || newY >= GRID_HEIGHT)
                continue;

            if (grid[newY][newX] == CELL_WALL || closedSet[newY][newX])
                continue;

            int newG = current->g + ((i < 4) ? 10 : 14);

            // 检查是否已经在开放列表中
            bool inOpenSet = false;
            std::vector<Node*> tempNodes;

            // 临时存储并检查
            while (!openSet.empty()) {
                Node* node = openSet.top();
                openSet.pop();
                tempNodes.push_back(node);

                if (node->x == newX && node->y == newY) {
                    inOpenSet = true;
                    if (newG < node->g) {
                        node->g = newG;
                        node->f = node->g + node->h;
                        node->parent = current;
                    }
                    break;
                }
            }

            // 恢复优先队列
            for (size_t j = 0; j < tempNodes.size(); j++) {
                openSet.push(tempNodes[j]);
            }

            if (!inOpenSet) {
                Node* neighbor = new Node(newX, newY);
                neighbor->g = newG;
                neighbor->h = CalculateHeuristic(newX, newY, endPos.x, endPos.y);
                neighbor->f = neighbor->g + neighbor->h;
                neighbor->parent = current;

                if (grid[newY][newX] != CELL_START && grid[newY][newX] != CELL_END && showVisited) {
                    grid[newY][newX] = CELL_OPEN;

                    RECT rect;
                    rect.left = newX * CELL_SIZE;
                    rect.top = newY * CELL_SIZE;
                    rect.right = rect.left + CELL_SIZE;
                    rect.bottom = rect.top + CELL_SIZE;
                    InvalidateRect(hMainWnd, &rect, FALSE);
                }

                openSet.push(neighbor);
            }
        }

        UpdateWindow(hMainWnd);
        Sleep(visualizationSpeed);

        if (isPaused) {
            while (isPaused && isRunning) {
                Sleep(100);
            }
        }
    }

    // 清理内存
    CleanupNodes(openSet);

    // 检查是否没有找到路径
    if (!pathFound && isRunning) {
        PostMessage(hMainWnd, WM_USER + 1, 0, 0); // 0表示未找到路径
    }

    isRunning = false;
    isPaused = false;

    // 添加UI重绘
    RECT uiRect;
    uiRect.left = GRID_WIDTH * CELL_SIZE;
    uiRect.top = 0;
    uiRect.right = WINDOW_WIDTH;
    uiRect.bottom = WINDOW_HEIGHT;
    InvalidateRect(hMainWnd, &uiRect, TRUE);

    return 0;
}

// 生成随机地图
void GenerateRandomMap(int wallProbability = 30) {
    srand(static_cast<unsigned int>(time(NULL)));

    hasStart = false;
    hasEnd = false;
    startPos = { -1, -1 };
    endPos = { -1, -1 };

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            grid[y][x] = (rand() % 100 < wallProbability) ? CELL_WALL : CELL_EMPTY;
        }
    }

    int startX, startY, endX, endY;
    do {
        startX = rand() % (GRID_WIDTH / 3);
        startY = rand() % (GRID_HEIGHT / 3);
    } while (grid[startY][startX] == CELL_WALL);

    do {
        endX = GRID_WIDTH - 1 - rand() % (GRID_WIDTH / 3);
        endY = GRID_HEIGHT - 1 - rand() % (GRID_HEIGHT / 3);
    } while (grid[endY][endX] == CELL_WALL || (endX == startX && endY == startY));

    startPos = { startX, startY };
    endPos = { endX, endY };
    grid[startY][startX] = CELL_START;
    grid[endY][endX] = CELL_END;
    hasStart = true;
    hasEnd = true;
}

// 保存地图到文件
void SaveMap() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstringstream filename;
    filename << L"map_"
        << st.wYear << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wMonth << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wDay << L"_"
        << std::setw(2) << std::setfill(L'0') << st.wHour << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wMinute << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wSecond
        << L".bin";

    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };
    wcscpy_s(szFile, filename.str().c_str());

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Map Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        std::ofstream file(ofn.lpstrFile, std::ios::binary);
        if (file.is_open()) {
            for (int y = 0; y < GRID_HEIGHT; y++) {
                for (int x = 0; x < GRID_WIDTH; x++) {
                    file.write(reinterpret_cast<const char*>(&grid[y][x]), sizeof(CellType));
                }
            }
            file.write(reinterpret_cast<const char*>(&startPos), sizeof(POINT));
            file.write(reinterpret_cast<const char*>(&endPos), sizeof(POINT));
            file.close();
        }
    }
}

// 从文件加载地图
void LoadMap() {
    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Map Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        StopAStar();

        std::ifstream file(ofn.lpstrFile, std::ios::binary);
        if (file.is_open()) {
            for (int y = 0; y < GRID_HEIGHT; y++) {
                for (int x = 0; x < GRID_WIDTH; x++) {
                    file.read(reinterpret_cast<char*>(&grid[y][x]), sizeof(CellType));
                }
            }
            file.read(reinterpret_cast<char*>(&startPos), sizeof(POINT));
            file.read(reinterpret_cast<char*>(&endPos), sizeof(POINT));
            file.close();

            hasStart = (startPos.x != -1 && startPos.y != -1);
            hasEnd = (endPos.x != -1 && endPos.y != -1);

            InvalidateRect(hMainWnd, NULL, TRUE);
        }
    }
}

// 绘制网格
void DrawGrid(HDC hdc) {
    HBRUSH hBrush;
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            hBrush = CreateSolidBrush(GetCellColor(grid[y][x]));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

            Rectangle(hdc, x * CELL_SIZE, y * CELL_SIZE,
                (x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE);

            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);
        }
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

// 绘制UI控件
void DrawUI(HDC hdc) {
    int leftPanelX = GRID_WIDTH * CELL_SIZE + 20;
    int rightPanelX = GRID_WIDTH * CELL_SIZE + 280;

    // 使用系统字体确保兼容性
    HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, RGB(0, 0, 0));

    // 绘制标题
    HFONT hTitleFont = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
    HFONT hOldTitleFont = (HFONT)SelectObject(hdc, hTitleFont);

    // 标题居中显示
    const wchar_t* title = L"A*寻路算法可视化工具";
    TextOut(hdc, leftPanelX + 50, 20, title, (int)wcslen(title));

    SelectObject(hdc, hOldTitleFont);
    DeleteObject(hTitleFont);

    // 左侧面板：操作说明和图例
    int yPos = 60;
    TextOut(hdc, leftPanelX, yPos, L"操作说明:", 5);
    yPos += 25;
    TextOut(hdc, leftPanelX, yPos, L"- 选择工具后在地图上绘制", 13);
    yPos += 20;
    TextOut(hdc, leftPanelX, yPos, L"- 左键拖动: 绘制/擦除", 13);
    yPos += 20;
    TextOut(hdc, leftPanelX, yPos, L"- S键: 开始寻路", 10);
    yPos += 20;
    TextOut(hdc, leftPanelX, yPos, L"- P键: 暂停/继续", 11);
    yPos += 20;
    TextOut(hdc, leftPanelX, yPos, L"- T键: 停止寻路", 10);

    yPos += 30;
    TextOut(hdc, leftPanelX, yPos, L"图例:", 3);
    yPos += 25;

    int legendY = yPos;
    CellType legendTypes[] = { CELL_START, CELL_END, CELL_WALL, CELL_PATH, CELL_VISITED, CELL_OPEN };
    const wchar_t* legendText[] = { L"起点", L"终点", L"墙壁", L"路径", L"已访问", L"开放列表" };

    for (int i = 0; i < 6; i++) {
        HBRUSH hBrush = CreateSolidBrush(GetCellColor(legendTypes[i]));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        Rectangle(hdc, leftPanelX, legendY, leftPanelX + 18, legendY + 18);
        SelectObject(hdc, hOldBrush);
        DeleteObject(hBrush);

        TextOut(hdc, leftPanelX + 25, legendY, legendText[i], (int)wcslen(legendText[i]));
        legendY += 22;
    }

    // 状态信息
    legendY += 25;
    if (isRunning) {
        TextOut(hdc, leftPanelX, legendY, L"状态: 运行中", 7);
        if (isPaused) {
            TextOut(hdc, leftPanelX + 80, legendY, L"(已暂停)", 5);
        }
    }
    else {
        TextOut(hdc, leftPanelX, legendY, L"状态: 就绪", 6);
    }

    legendY += 22;
    TextOut(hdc, leftPanelX, legendY, hasStart ? L"起点: 已设置" : L"起点: 未设置",
        hasStart ? 7 : 7);
    legendY += 22;
    TextOut(hdc, leftPanelX, legendY, hasEnd ? L"终点: 已设置" : L"终点: 未设置",
        hasEnd ? 7 : 7);

    // 速度信息
    legendY += 25;
    std::wstringstream speedText;
    int speedLevel = (110 - visualizationSpeed) / 10;  // 修改计算公式
    speedText << L"速度等级: " << speedLevel << L"/10";
    std::wstring speedStr = speedText.str();
    TextOut(hdc, leftPanelX, legendY, speedStr.c_str(), (int)speedStr.length());

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// 处理地图点击
void HandleMapClick(int x, int y, bool isDragging) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;

    switch (currentTool) {
    case TOOL_WALL:
        if (grid[y][x] == CELL_EMPTY) {
            grid[y][x] = CELL_WALL;
        }
        break;

    case TOOL_START:
        if (!hasStart && grid[y][x] == CELL_EMPTY) {
            if (hasStart) {
                grid[startPos.y][startPos.x] = CELL_EMPTY;
                RECT rect;
                rect.left = startPos.x * CELL_SIZE;
                rect.top = startPos.y * CELL_SIZE;
                rect.right = rect.left + CELL_SIZE;
                rect.bottom = rect.top + CELL_SIZE;
                InvalidateRect(hMainWnd, &rect, FALSE);
            }
            startPos = { x, y };
            grid[y][x] = CELL_START;
            hasStart = true;
        }
        else if (hasStart && !isDragging) {
            MessageBox(hMainWnd, L"只能设置一个起点！", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        break;

    case TOOL_END:
        if (!hasEnd && grid[y][x] == CELL_EMPTY) {
            if (hasEnd) {
                grid[endPos.y][endPos.x] = CELL_EMPTY;
                RECT rect;
                rect.left = endPos.x * CELL_SIZE;
                rect.top = endPos.y * CELL_SIZE;
                rect.right = rect.left + CELL_SIZE;
                rect.bottom = rect.top + CELL_SIZE;
                InvalidateRect(hMainWnd, &rect, FALSE);
            }
            endPos = { x, y };
            grid[y][x] = CELL_END;
            hasEnd = true;
        }
        else if (hasEnd && !isDragging) {
            MessageBox(hMainWnd, L"只能设置一个终点！", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        break;

    case TOOL_ERASE:
        if (grid[y][x] == CELL_WALL) {
            grid[y][x] = CELL_EMPTY;
        }
        else if (grid[y][x] == CELL_START) {
            grid[y][x] = CELL_EMPTY;
            hasStart = false;
            startPos = { -1, -1 };
        }
        else if (grid[y][x] == CELL_END) {
            grid[y][x] = CELL_EMPTY;
            hasEnd = false;
            endPos = { -1, -1 };
        }
        break;
    }

    RECT rect;
    rect.left = x * CELL_SIZE;
    rect.top = y * CELL_SIZE;
    rect.right = rect.left + CELL_SIZE;
    rect.bottom = rect.top + CELL_SIZE;
    InvalidateRect(hMainWnd, &rect, FALSE);

    // 添加UI区域重绘
    RECT uiRect;
    uiRect.left = GRID_WIDTH * CELL_SIZE;
    uiRect.top = 0;
    uiRect.right = WINDOW_WIDTH;
    uiRect.bottom = WINDOW_HEIGHT;
    InvalidateRect(hMainWnd, &uiRect, TRUE);
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static bool isDragging = false;

    switch (message) {
    case WM_CREATE:
    {
        // 初始化网格为空
        for (int y = 0; y < GRID_HEIGHT; y++) {
            for (int x = 0; x < GRID_WIDTH; x++) {
                grid[y][x] = CELL_EMPTY;
            }
        }

        // 初始化通用控件
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&icex);

        int leftPanelX = GRID_WIDTH * CELL_SIZE + 20;
        int rightPanelX = GRID_WIDTH * CELL_SIZE + 280;
        int startY = 60;

        // 创建工具选择单选框
        hToolRadio[0] = CreateWindow(L"BUTTON", L"绘制墙壁", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            rightPanelX, startY, 180, 20, hWnd, (HMENU)100, hInst, NULL);
        hToolRadio[1] = CreateWindow(L"BUTTON", L"设置起点", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            rightPanelX, startY + 25, 180, 20, hWnd, (HMENU)101, hInst, NULL);
        hToolRadio[2] = CreateWindow(L"BUTTON", L"设置终点", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            rightPanelX, startY + 50, 180, 20, hWnd, (HMENU)102, hInst, NULL);
        hToolRadio[3] = CreateWindow(L"BUTTON", L"擦除工具", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            rightPanelX, startY + 75, 180, 20, hWnd, (HMENU)103, hInst, NULL);
        CheckRadioButton(hWnd, 100, 103, 100);

        // 创建速度调节滑块和标签
        hSpeedLabel = CreateWindow(L"STATIC", L"可视化速度:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            rightPanelX, startY + 105, 180, 20, hWnd, NULL, hInst, NULL);
        hSpeedTrackbar = CreateWindow(TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_BOTH,
            rightPanelX, startY + 125, 200, 30, hWnd, (HMENU)111, hInst, NULL);
        // 修改速度滑块范围为1-10
        SendMessage(hSpeedTrackbar, TBM_SETRANGE, TRUE, MAKELONG(1, 10));
        SendMessage(hSpeedTrackbar, TBM_SETPOS, TRUE, 5);  // 默认值5
        SendMessage(hSpeedTrackbar, TBM_SETTICFREQ, 1, 0);

        // 创建按钮
        int buttonY = startY + 165;
        hStartButton = CreateWindow(L"BUTTON", L"开始寻路", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY, 180, 28, hWnd, (HMENU)104, hInst, NULL);
        hStopButton = CreateWindow(L"BUTTON", L"停止", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 33, 180, 28, hWnd, (HMENU)105, hInst, NULL);
        hPauseButton = CreateWindow(L"BUTTON", L"暂停/继续", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 66, 180, 28, hWnd, (HMENU)106, hInst, NULL);
        hClearButton = CreateWindow(L"BUTTON", L"清空地图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 99, 180, 28, hWnd, (HMENU)107, hInst, NULL);
        hRandomButton = CreateWindow(L"BUTTON", L"随机地图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 132, 180, 28, hWnd, (HMENU)108, hInst, NULL);
        hSaveButton = CreateWindow(L"BUTTON", L"保存地图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 165, 180, 28, hWnd, (HMENU)109, hInst, NULL);
        hLoadButton = CreateWindow(L"BUTTON", L"加载地图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 198, 180, 28, hWnd, (HMENU)110, hInst, NULL);
    }
    break;

    case WM_USER + 1:
        // 处理寻路结果消息 - 只在失败时弹窗
        if (wParam == 0) {
            MessageBox(hWnd, L"无法找到从起点到终点的路径！", L"寻路结果", MB_OK | MB_ICONINFORMATION);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, WINDOW_WIDTH, WINDOW_HEIGHT);
        HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

        HBRUSH hBackground = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FillRect(hdcMem, &clientRect, hBackground);
        DeleteObject(hBackground);

        if (ps.rcPaint.left < GRID_WIDTH * CELL_SIZE) {
            DrawGrid(hdcMem);
        }
        if (ps.rcPaint.right > GRID_WIDTH * CELL_SIZE) {
            DrawUI(hdcMem);
        }

        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top,
            hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

        SelectObject(hdcMem, hOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
    }
    break;

    case WM_HSCROLL:
        if ((HWND)lParam == hSpeedTrackbar) {
            int speedLevel = (int)SendMessage(hSpeedTrackbar, TBM_GETPOS, 0, 0);
            // 修改速度计算公式，范围1-10对应速度100-10
            visualizationSpeed = 110 - (speedLevel * 10);
            RECT uiRect;
            uiRect.left = GRID_WIDTH * CELL_SIZE;
            uiRect.top = 0;
            uiRect.right = WINDOW_WIDTH;
            uiRect.bottom = WINDOW_HEIGHT;
            InvalidateRect(hWnd, &uiRect, TRUE);
        }
        break;

    case WM_LBUTTONDOWN:
        if (!isRunning) {
            int x = GET_X_LPARAM(lParam) / CELL_SIZE;
            int y = GET_Y_LPARAM(lParam) / CELL_SIZE;

            if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                isDragging = true;
                HandleMapClick(x, y, false);
            }
        }
        break;

    case WM_MOUSEMOVE:
        if (isDragging && !isRunning) {
            int x = GET_X_LPARAM(lParam) / CELL_SIZE;
            int y = GET_Y_LPARAM(lParam) / CELL_SIZE;

            if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                HandleMapClick(x, y, true);
            }
        }
        break;

    case WM_LBUTTONUP:
        isDragging = false;
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 100: case 101: case 102: case 103:
            currentTool = static_cast<ToolType>(LOWORD(wParam) - 100);
            CheckRadioButton(hWnd, 100, 103, LOWORD(wParam));
            break;

        case 104:
            if (!isRunning) {
                if (!hasStart || !hasEnd) {
                    MessageBox(hWnd, L"请先设置起点和终点！", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                isRunning = true;
                hAStarThread = CreateThread(NULL, 0, AStarSearch, NULL, 0, NULL);

                // 添加UI重绘
                RECT uiRect;
                uiRect.left = GRID_WIDTH * CELL_SIZE;
                uiRect.top = 0;
                uiRect.right = WINDOW_WIDTH;
                uiRect.bottom = WINDOW_HEIGHT;
                InvalidateRect(hWnd, &uiRect, TRUE);
            }
            break;

        case 105:
            StopAStar();
            // 添加UI重绘
            {
                RECT uiRect;
                uiRect.left = GRID_WIDTH * CELL_SIZE;
                uiRect.top = 0;
                uiRect.right = WINDOW_WIDTH;
                uiRect.bottom = WINDOW_HEIGHT;
                InvalidateRect(hWnd, &uiRect, TRUE);
            }
            break;

        case 106:
            if (isRunning) {
                isPaused = !isPaused;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case 107:
            if (!isRunning) {
                StopAStar();
                for (int y = 0; y < GRID_HEIGHT; y++) {
                    for (int x = 0; x < GRID_WIDTH; x++) {
                        grid[y][x] = CELL_EMPTY;
                    }
                }
                hasStart = false;
                hasEnd = false;
                startPos = { -1, -1 };
                endPos = { -1, -1 };
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case 108:
            if (!isRunning) {
                StopAStar();
                GenerateRandomMap();
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case 109:
            if (!isRunning) {
                SaveMap();
            }
            break;

        case 110:
            if (!isRunning) {
                LoadMap();
                // 添加UI重绘
                RECT uiRect;
                uiRect.left = GRID_WIDTH * CELL_SIZE;
                uiRect.top = 0;
                uiRect.right = WINDOW_WIDTH;
                uiRect.bottom = WINDOW_HEIGHT;
                InvalidateRect(hWnd, &uiRect, TRUE);
            }
            break;
        }
        break;

    case WM_KEYDOWN:
        switch (wParam) {
        case 'S': case 's':
            if (!isRunning) {
                if (!hasStart || !hasEnd) {
                    MessageBox(hWnd, L"请先设置起点和终点！", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                isRunning = true;
                hAStarThread = CreateThread(NULL, 0, AStarSearch, NULL, 0, NULL);

                // 添加UI重绘
                RECT uiRect;
                uiRect.left = GRID_WIDTH * CELL_SIZE;
                uiRect.top = 0;
                uiRect.right = WINDOW_WIDTH;
                uiRect.bottom = WINDOW_HEIGHT;
                InvalidateRect(hWnd, &uiRect, TRUE);
            }
            break;

        case 'P': case 'p':
            if (isRunning) {
                isPaused = !isPaused;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case 'T': case 't':
            StopAStar();
            // 添加UI重绘
            {
                RECT uiRect;
                uiRect.left = GRID_WIDTH * CELL_SIZE;
                uiRect.top = 0;
                uiRect.right = WINDOW_WIDTH;
                uiRect.bottom = WINDOW_HEIGHT;
                InvalidateRect(hWnd, &uiRect, TRUE);
            }
            break;
        }
        break;

    case WM_DESTROY:
        StopAStar();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 注册窗口类
BOOL RegisterWindowClass() {
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInst;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"AStarVisualizer";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    return RegisterClassEx(&wcex);
}

// 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;

    // 设置进程工作集大小，优化内存使用
    SetProcessWorkingSetSize(GetCurrentProcess(), 1024 * 1024, 4 * 1024 * 1024);

    if (!RegisterWindowClass()) {
        MessageBox(NULL, L"窗口类注册失败!可能的原因：\n1. 类名已被占用\n2. 系统资源不足\n3. 程序重复运行", L"错误", MB_ICONERROR);
        return 1;
    }

    hMainWnd = CreateWindow(
        L"AStarVisualizer",
        L"A*寻路算法可视化工具",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (!hMainWnd) {
        MessageBox(NULL, L"窗口创建失败!可能的原因：\n1. 系统资源不足\n2. 窗口类注册失败\n3. 内存不足", L"错误", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}