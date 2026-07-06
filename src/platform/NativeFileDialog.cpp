#include "platform/NativeFileDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace gs3d::platform {

namespace {

#if defined(_WIN32)

NativeFileDialogResult choose_windows_folder()
{
    const HRESULT init_result = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
    );
    IFileOpenDialog* dialog = nullptr;
    const HRESULT create_result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&dialog)
    );
    if (FAILED(create_result) || dialog == nullptr) {
        if (SUCCEEDED(init_result)) {
            CoUninitialize();
        }
        return {{}, "无法打开 Windows 文件夹选择器"};
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(
        options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST |
        FOS_FORCEFILESYSTEM
    );
    dialog->SetTitle(L"打开 GeoScatter3D 项目");

    NativeFileDialogResult result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(
                    SIGDN_FILESYSPATH,
                    &path
                )) && path != nullptr) {
                result.path = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    if (SUCCEEDED(init_result)) {
        CoUninitialize();
    }
    return result;
}

NativeFileDialogResult choose_windows_raw_file()
{
    wchar_t path_buffer[32768]{};
    static constexpr wchar_t kFilter[] =
        L"散点数据 (*.csv;*.dat)\0*.csv;*.dat\0"
        L"CSV 文件 (*.csv)\0*.csv\0"
        L"DAT 文件 (*.dat)\0*.dat\0"
        L"所有文件 (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path_buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
    dialog.lpstrFilter = kFilter;
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = L"加载原始数据";
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&dialog) != FALSE) {
        return {std::filesystem::path(path_buffer), {}};
    }
    return {};
}

#else

bool command_available(const char* command)
{
    const std::string probe =
        "command -v " + std::string(command) +
        " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

NativeFileDialogResult run_dialog_command(const char* command)
{
    FILE* process = popen(command, "r");
    if (process == nullptr) {
        return {{}, "无法启动系统文件选择器"};
    }

    std::string output;
    char buffer[4096]{};
    while (std::fgets(buffer, sizeof(buffer), process) != nullptr) {
        output += buffer;
    }
    const int status = pclose(process);
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (status != 0 || output.empty()) {
        return {};
    }
    return {std::filesystem::path(output), {}};
}

#endif

} // namespace

NativeFileDialogResult choose_project_directory()
{
#if defined(_WIN32)
    return choose_windows_folder();
#elif defined(__APPLE__)
    return run_dialog_command(
        "osascript -e 'POSIX path of (choose folder with prompt "
        "\"打开 GeoScatter3D 项目\")' 2>/dev/null"
    );
#else
    if (command_available("zenity")) {
        return run_dialog_command(
            "zenity --file-selection --directory "
            "--title='打开 GeoScatter3D 项目' 2>/dev/null"
        );
    }
    if (command_available("kdialog")) {
        return run_dialog_command(
            "kdialog --getexistingdirectory . "
            "--title '打开 GeoScatter3D 项目' 2>/dev/null"
        );
    }
    return {
        {},
        "未找到系统文件选择器（需要 Zenity 或 KDialog）"
    };
#endif
}

NativeFileDialogResult choose_save_file(
    const std::string& default_filename,
    const std::string& title,
    const std::string& /*filters*/
) {
#if defined(_WIN32)
    std::wstring wide_default(
        default_filename.begin(),
        default_filename.end()
    );
    std::wstring wide_title(title.begin(), title.end());

    wchar_t path_buffer[32768]{};
    if (wide_default.size() < std::size(path_buffer)) {
        std::wcscpy(path_buffer, wide_default.c_str());
    }
    const std::wstring wfilter =
        L"PNG Images (*.png)\0*.png\0"
        L"所有文件 (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path_buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
    dialog.lpstrFilter = wfilter.c_str();
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = wide_title.c_str();
    dialog.lpstrDefExt = L"png";
    dialog.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_NOCHANGEDIR |
        OFN_EXPLORER |
        OFN_PATHMUSTEXIST;

    if (GetSaveFileNameW(&dialog) != FALSE) {
        return {std::filesystem::path(path_buffer), {}};
    }
    return {};
#elif defined(__APPLE__)
    const std::string command =
        "osascript -e 'POSIX path of (choose file name "
        "with prompt \"" + title + "\" "
        "default name \"" + default_filename + "\")' 2>/dev/null";
    return run_dialog_command(command.c_str());
#else
    if (command_available("zenity")) {
        const std::string command =
            "zenity --file-selection --save "
            "--confirm-overwrite "
            "--title='" + title + "' "
            "--filename='" + default_filename + "' "
            "--file-filter='PNG Images | *.png' "
            "2>/dev/null";
        return run_dialog_command(command.c_str());
    }
    if (command_available("kdialog")) {
        const std::string command =
            "kdialog --getsavefilename . "
            "'PNG Images (*.png)' "
            "--title '" + title + "' 2>/dev/null";
        return run_dialog_command(command.c_str());
    }
    return {
        {},
        "未找到系统文件选择器（需要 Zenity 或 KDialog）"
    };
#endif
}

NativeFileDialogResult choose_raw_data_file()
{
#if defined(_WIN32)
    return choose_windows_raw_file();
#elif defined(__APPLE__)
    return run_dialog_command(
        "osascript -e 'POSIX path of (choose file with prompt "
        "\"加载 DAT / CSV 原始数据\")' 2>/dev/null"
    );
#else
    if (command_available("zenity")) {
        return run_dialog_command(
            "zenity --file-selection --title='加载原始数据' "
            "--file-filter='散点数据 | *.csv *.CSV *.dat *.DAT' "
            "--file-filter='所有文件 | *' 2>/dev/null"
        );
    }
    if (command_available("kdialog")) {
        return run_dialog_command(
            "kdialog --getopenfilename . "
            "'散点数据 (*.csv *.CSV *.dat *.DAT)' "
            "--title '加载原始数据' 2>/dev/null"
        );
    }
    return {
        {},
        "未找到系统文件选择器（需要 Zenity 或 KDialog）"
    };
#endif
}

} // namespace gs3d::platform
