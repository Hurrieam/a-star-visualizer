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
HWND hStartButton, hStopButton, hPauseButton, hClearButton, hRandomButton, hSaveButton, hLoadButton, hExitButton, hAboutButton;
HWND hSpeedTrackbar, hSpeedLabel;
ToolType currentTool = TOOL_WALL;
HANDLE hAStarThread = NULL;

// 鼠标状态跟踪
bool isMouseDownOnControl = false;
bool ignoreNextMouseMove = false;
POINT lastMousePos = { -1, -1 };  // 记录上一次鼠标位置

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

// 更新UI状态显示
void UpdateUIStatus() {
    RECT uiRect;
    uiRect.left = GRID_WIDTH * CELL_SIZE;
    uiRect.top = 0;
    uiRect.right = WINDOW_WIDTH;
    uiRect.bottom = WINDOW_HEIGHT;
    InvalidateRect(hMainWnd, &uiRect, TRUE);
    UpdateWindow(hMainWnd);
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
        UpdateUIStatus(); // 更新UI状态
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
            UpdateUIStatus(); // 更新UI状态

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

            // 检查对角线移动是否被直角墙阻挡
            if (i >= 4) { // 对角线移动
                int dx1 = directions[i][0], dy1 = 0;
                int dx2 = 0, dy2 = directions[i][1];

                // 如果两个相邻的直角位置都是墙，则不能对角线移动
                if (grid[current->y + dy1][current->x + dx1] == CELL_WALL &&
                    grid[current->y + dy2][current->x + dx2] == CELL_WALL) {
                    continue;
                }
            }

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
    UpdateUIStatus(); // 更新UI状态

    return 0;
}

// 生成随机地图
void GenerateRandomMap(int wallProbability = 30) {
    // 使用高精度计数器作为随机数种子
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    srand(static_cast<unsigned int>(counter.LowPart));

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
            UpdateUIStatus(); // 更新UI状态
        }
    }
}

// 显示关于对话框
void ShowAboutDialog() {
    MessageBox(hMainWnd,
        L"A*寻路算法可视化工具\n\n"
        L"hurrieam (C) 2025\n\n"
        L"功能介绍:\n"
        L"- 可视化A*寻路算法过程\n"
        L"- 支持绘制墙壁、设置起点终点\n"
        L"- 支持保存和加载地图\n"
        L"- 可调节可视化速度\n"
        L"- 支持8方向移动\n"
        L"- 实时显示算法状态",
        L"关于",
        MB_OK | MB_ICONINFORMATION);
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

    // 只有当状态真正改变时才重绘UI区域
    static bool lastHasStart = false;
    static bool lastHasEnd = false;

    if (hasStart != lastHasStart || hasEnd != lastHasEnd) {
        UpdateUIStatus(); // 使用新的UI更新函数

        lastHasStart = hasStart;
        lastHasEnd = hasEnd;
    }
}

// 在两点之间绘制直线（填充中间的单元格）
void DrawLineBetweenPoints(int x1, int y1, int x2, int y2) {
    // 使用Bresenham直线算法填充两点之间的所有单元格
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        // 处理当前单元格
        HandleMapClick(x1, y1, true);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
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
        hAboutButton = CreateWindow(L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 231, 180, 28, hWnd, (HMENU)113, hInst, NULL);
        hExitButton = CreateWindow(L"BUTTON", L"退出程序", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rightPanelX, buttonY + 264, 180, 28, hWnd, (HMENU)112, hInst, NULL);
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

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case 100: case 101: case 102: case 103:
            currentTool = static_cast<ToolType>(wmId - 100);
            CheckRadioButton(hWnd, 100, 103, wmId);
            break;

        case 104: // 开始寻路
            if (!hasStart || !hasEnd) {
                MessageBox(hWnd, L"请先设置起点和终点！", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (isRunning) {
                MessageBox(hWnd, L"寻路正在进行中！", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            isRunning = true;
            isPaused = false;
            hAStarThread = CreateThread(NULL, 0, AStarSearch, NULL, 0, NULL);
            UpdateUIStatus(); // 更新UI状态
            break;

        case 105: // 停止
            StopAStar();
            break;

        case 106: // 暂停/继续
            if (isRunning) {
                isPaused = !isPaused;
                UpdateUIStatus(); // 更新UI状态
            }
            break;

        case 107: // 清空地图
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
            UpdateUIStatus(); // 更新UI状态
            break;

        case 108: // 随机地图
            StopAStar();
            GenerateRandomMap();
            InvalidateRect(hWnd, NULL, TRUE);
            UpdateUIStatus(); // 更新UI状态
            break;

        case 109: // 保存地图
            SaveMap();
            break;

        case 110: // 加载地图
            LoadMap();
            break;

        case 112: // 退出程序
            PostQuitMessage(0);
            break;

        case 113: // 关于按钮
            ShowAboutDialog();
            break;
        }
    }
    break;

    case WM_HSCROLL:
        if ((HWND)lParam == hSpeedTrackbar) {
            int pos = (int)SendMessage(hSpeedTrackbar, TBM_GETPOS, 0, 0);
            // 修改速度映射公式
            visualizationSpeed = 110 - pos * 10;  // 1级=100ms, 10级=10ms
            UpdateUIStatus(); // 更新UI状态
        }
        break;

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // 检查是否点击在控件上
        HWND hwndChild = ChildWindowFromPoint(hWnd, { x, y });
        if (hwndChild != hWnd && hwndChild != NULL) {
            isMouseDownOnControl = true;
            break;
        }

        // 检查是否点击在网格区域内
        if (x < GRID_WIDTH * CELL_SIZE) {
            isDragging = true;
            int gridX = x / CELL_SIZE;
            int gridY = y / CELL_SIZE;
            lastMousePos = { gridX, gridY };
            HandleMapClick(gridX, gridY, false);
            SetCapture(hWnd);
        }
    }
    break;

    case WM_MOUSEMOVE:
    {
        if (isMouseDownOnControl) {
            break;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // 检查是否在网格区域内
        if (isDragging && x < GRID_WIDTH * CELL_SIZE) {
            int gridX = x / CELL_SIZE;
            int gridY = y / CELL_SIZE;

            // 如果鼠标位置没有变化，跳过
            if (gridX == lastMousePos.x && gridY == lastMousePos.y) {
                break;
            }

            // 使用直线绘制来填充鼠标移动路径中的所有单元格
            DrawLineBetweenPoints(lastMousePos.x, lastMousePos.y, gridX, gridY);

            // 更新最后鼠标位置
            lastMousePos = { gridX, gridY };
        }
    }
    break;

    case WM_LBUTTONUP:
        if (isDragging) {
            isDragging = false;
            ReleaseCapture();
        }
        isMouseDownOnControl = false;
        break;

    case WM_KEYDOWN:
        switch (wParam) {
        case 'S': case 's': // 开始寻路
            if (!isRunning && hasStart && hasEnd) {
                isRunning = true;
                isPaused = false;
                hAStarThread = CreateThread(NULL, 0, AStarSearch, NULL, 0, NULL);
                UpdateUIStatus(); // 更新UI状态
            }
            break;

        case 'P': case 'p': // 暂停/继续
            if (isRunning) {
                isPaused = !isPaused;
                UpdateUIStatus(); // 更新UI状态
            }
            break;

        case 'T': case 't': // 停止
            StopAStar();
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

// 应用程序入口点
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;

    // 注册窗口类
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"AStarVisualizer";
    wcex.hIconSm = LoadIcon(hInstance, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, L"窗口类注册失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 计算窗口位置居中显示
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowX = (screenWidth - WINDOW_WIDTH) / 2;
    int windowY = (screenHeight - WINDOW_HEIGHT) / 2;

    // 创建窗口
    hMainWnd = CreateWindow(
        L"AStarVisualizer",
        L"A*寻路算法可视化工具",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        windowX, windowY,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL);

    if (!hMainWnd) {
        MessageBox(NULL, L"窗口创建失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}