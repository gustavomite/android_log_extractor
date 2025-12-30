#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <process.h>
#include <commctrl.h> // Para Estilos Visuais

// Linkar bibliotecas necessárias
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

#define ID_BTN_EXTRAIR     101
#define ID_BTN_PARAR       106
#define ID_BTN_BROWSE_ADB   104
#define ID_BTN_BROWSE_SAVE  105
#define ID_EDIT_ADB        102
#define ID_EDIT_SAVE       103
#define ID_EDIT_PACKAGE    107
#define ID_STATUS_BAR      108

HWND hEditAdb, hEditSave, hEditPackage, hBtnExtrair, hBtnParar, hStatus;
HFONT hFont; // Handle da fonte suavizada
volatile BOOL bRodando = FALSE;
HANDLE hProcessADB = NULL;

typedef struct {
    char pathAdb[MAX_PATH];
    char pathSave[MAX_PATH];
    char packageName[128]; // Novo campo para o pacote
} Config;

void SalvarConfiguracoes() {
    Config cfg;
    GetWindowText(hEditAdb, cfg.pathAdb, MAX_PATH);
    GetWindowText(hEditSave, cfg.pathSave, MAX_PATH);
    GetWindowText(hEditPackage, cfg.packageName, 128);

    FILE *f = fopen("config.dat", "wb");
    if (f) { fwrite(&cfg, sizeof(Config), 1, f); fclose(f); }
}

void CarregarConfiguracoes() {
    Config cfg;
    FILE *f = fopen("config.dat", "rb");
    if (f) {
        if (fread(&cfg, sizeof(Config), 1, f) == 1) {
            SetWindowText(hEditAdb, cfg.pathAdb);
            SetWindowText(hEditSave, cfg.pathSave);
            SetWindowText(hEditPackage, cfg.packageName);
        }
        fclose(f);
    }
}

void ExecuteAdbThread(void* args) {
    Config* data = (Config*)args;
    bRodando = TRUE;
    SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)"Status: Localizando PID do pacote...");

    char cmdLine[MAX_PATH * 3];
    BOOL usarFiltro = (strlen(data->packageName) > 0);

    // Se houver filtro, primeiro tentamos descobrir o PID
    if (usarFiltro) {
        char pidCmd[MAX_PATH * 2];
        char pidResult[256] = {0};
		sprintf(pidCmd, "%s shell \"ps | grep %s | tr -s ' ' | cut -d' ' -f2\"", data->pathAdb, data->packageName);
        
        // Execução rápida para pegar o PID
        FILE* pipe = _popen(pidCmd, "r");
        if (pipe) {
            fgets(pidResult, sizeof(pidResult), pipe);
            _pclose(pipe);
        }
		
        // Limpa espaços ou quebras de linha do PID
        strtok(pidResult, " \r\n");

        if (strlen(pidResult) > 0) {
            char statusMsg[128];
            sprintf(statusMsg, "Status: Coletando logs do PID: %s", pidResult);
            SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)statusMsg);
            // Comando final usando o PID descoberto
            sprintf(cmdLine, "%s logcat -v time --pid=%s", data->pathAdb, pidResult);
        } else {
            MessageBox(NULL, "App não encontrado ou não está rodando.\nCertifique-se que o app está aberto no celular.", "Aviso", MB_OK | MB_ICONWARNING);
            bRodando = FALSE;
            goto finalizar;
        }
    } else {
        sprintf(cmdLine, "\"%s\" logcat -v time", data->pathAdb);
    }

    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) { bRodando = FALSE; goto finalizar; }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe; si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

	if (CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        hProcessADB = pi.hProcess;
        EnableWindow(hBtnExtrair, FALSE);
        EnableWindow(hBtnParar, TRUE);

        FILE *fSave = fopen(data->pathSave, "ab"); 
        if (fSave) {
            char buffer[16384];
            DWORD bytesRead, bytesAvail;

            // LOOP CORRIGIDO
            while (bRodando) {
                // Verifica se há dados no Pipe sem ficar bloqueado
                if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
                    if (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                        for (DWORD i = 0; i < bytesRead; i++) {
                            if (buffer[i] == '\r') {
                                if (i + 1 < bytesRead && buffer[i+1] == '\r') continue;
                            }
                            fputc(buffer[i], fSave);
                        }
                        fflush(fSave);
                    }
                } else {
                    // Se não houver dados, descansa a CPU um pouco e volta a checar bRodando
                    Sleep(50); 
                }
            }
            fclose(fSave);
        }
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    CloseHandle(hReadPipe); CloseHandle(hWritePipe);

finalizar:
    hProcessADB = NULL; bRodando = FALSE;
    EnableWindow(hBtnExtrair, TRUE); EnableWindow(hBtnParar, FALSE);
    SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)"Status: Parado.");
    free(data);
    _endthread();
}

void SelecionarArquivo(HWND hEdit, BOOL salvar) {
    OPENFILENAME ofn;
    char szFile[260] = {0};
    GetWindowText(hEdit, szFile, 260);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hEdit;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = salvar ? "Arquivo de Log (*.txt)\0*.txt\0" : "Executáveis (*.exe)\0*.exe\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (salvar ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn)) {
        SetWindowText(hEdit, ofn.lpstrFile);
        SalvarConfiguracoes();
    }
}

// Função de Callback para aplicar a fonte em cada componente (Padrão C)
BOOL CALLBACK SetFontCallback(HWND child, LPARAM font) {
    SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

void AplicarFonteSuave(HWND hwnd) {
    // CLEARTYPE_QUALITY garante o anti-aliasing da fonte
    hFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                       DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    
    // Aplica a fonte na janela principal e em todos os seus filhos
    SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(hwnd, SetFontCallback, (LPARAM)hFont);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
            InitCommonControlsEx(&icex);

            // Interface
            CreateWindow("STATIC", "Caminho do ADB:", WS_VISIBLE | WS_CHILD, 20, 15, 200, 20, hwnd, NULL, NULL, NULL);
            hEditAdb = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 20, 35, 300, 25, hwnd, (HMENU)ID_EDIT_ADB, NULL, NULL);
            CreateWindow("BUTTON", "...", WS_VISIBLE | WS_CHILD, 325, 35, 35, 25, hwnd, (HMENU)ID_BTN_BROWSE_ADB, NULL, NULL);

            CreateWindow("STATIC", "Salvar log em:", WS_VISIBLE | WS_CHILD, 20, 75, 200, 20, hwnd, NULL, NULL, NULL);
            hEditSave = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 20, 95, 300, 25, hwnd, (HMENU)ID_EDIT_SAVE, NULL, NULL);
            CreateWindow("BUTTON", "...", WS_VISIBLE | WS_CHILD, 325, 95, 35, 25, hwnd, (HMENU)ID_BTN_BROWSE_SAVE, NULL, NULL);

            CreateWindow("STATIC", "Filtrar Pacote ou Termo (opcional):", WS_VISIBLE | WS_CHILD, 20, 135, 300, 20, hwnd, NULL, NULL, NULL);
            hEditPackage = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 20, 155, 340, 25, hwnd, (HMENU)ID_EDIT_PACKAGE, NULL, NULL);

            hBtnExtrair = CreateWindow("BUTTON", "INICIAR COLETA", WS_VISIBLE | WS_CHILD, 20, 205, 165, 45, hwnd, (HMENU)ID_BTN_EXTRAIR, NULL, NULL);
            hBtnParar = CreateWindow("BUTTON", "PARAR", WS_VISIBLE | WS_CHILD, 195, 205, 165, 45, hwnd, (HMENU)ID_BTN_PARAR, NULL, NULL);
            
            // Barra de Status
            hStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, "Status: Pronto", hwnd, ID_STATUS_BAR);

            AplicarFonteSuave(hwnd);
            CarregarConfiguracoes();
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_BROWSE_ADB) SelecionarArquivo(hEditAdb, FALSE);
            if (LOWORD(wParam) == ID_BTN_BROWSE_SAVE) SelecionarArquivo(hEditSave, TRUE);
            if (LOWORD(wParam) == ID_BTN_EXTRAIR) {
                Config* args = (Config*)malloc(sizeof(Config));
                GetWindowText(hEditAdb, args->pathAdb, MAX_PATH);
                GetWindowText(hEditSave, args->pathSave, MAX_PATH);
                GetWindowText(hEditPackage, args->packageName, 128);
                _beginthread(ExecuteAdbThread, 0, (void*)args);
            }
			if (LOWORD(wParam) == ID_BTN_PARAR) {
                bRodando = FALSE;
            }
            break;

        case WM_SIZE: // Reposiciona a barra de status quando a janela mudar de tamanho
            SendMessage(hStatus, WM_SIZE, 0, 0);
            break;

        case WM_CLOSE:
            bRodando = FALSE;
            if (hProcessADB) TerminateProcess(hProcessADB, 0);
            SalvarConfiguracoes();
            Sleep(150);
            if (hFont) DeleteObject(hFont);
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc; wc.hInstance = hInst; wc.lpszClassName = "AdbFinalGuiStatus";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);
    HWND hwnd = CreateWindow("AdbFinalGuiStatus", "Android Log Extractor", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
                             CW_USEDEFAULT, CW_USEDEFAULT, 400, 320, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}