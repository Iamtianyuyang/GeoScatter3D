#include "platform/NativeFileDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace gs3d::platform {

namespace {

#if defined(_WIN32)

std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size
    );
    return result;
}

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
        L"散点数据 (*.csv;*.dat;*.gs3d)\0*.csv;*.dat;*.gs3d\0"
        L"GS3D 文件 (*.gs3d)\0*.gs3d\0"
        L"CSV 文件 (*.csv)\0*.csv\0"
        L"DAT 文件 (*.dat)\0*.dat\0"
        L"所有文件 (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path_buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
    dialog.lpstrFilter = kFilter;
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = L"打开散点数据";
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

std::string shell_quote(const std::string& value)
{
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
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

#if !defined(__APPLE__)

namespace {

NativeFileDialogResult choose_save_file_blocking(
    const std::string& default_filename,
    const std::string& title,
    const std::string& /*filters*/
) {
#if defined(_WIN32)
    const std::wstring wide_default = utf8_to_wide(default_filename);
    const std::wstring wide_title = utf8_to_wide(title);

    wchar_t path_buffer[32768]{};
    if (wide_default.size() < std::size(path_buffer)) {
        std::wcscpy(path_buffer, wide_default.c_str());
    }
    static constexpr wchar_t kPngFilter[] =
        L"PNG Images (*.png)\0*.png\0"
        L"所有文件 (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path_buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
    dialog.lpstrFilter = kPngFilter;
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
#else
    std::error_code absolute_error;
    const auto absolute_path = std::filesystem::absolute(
        default_filename,
        absolute_error
    );
    const std::string default_path = absolute_error
        ? default_filename
        : absolute_path.string();
    if (command_available("zenity")) {
        const std::string command =
            "zenity --file-selection --save "
            "--confirm-overwrite "
            "--title=" + shell_quote(title) + " "
            "--filename=" + shell_quote(default_path) + " "
            "--file-filter='PNG Images | *.png' "
            "2>/dev/null";
        return run_dialog_command(command.c_str());
    }
    if (command_available("kdialog")) {
        const std::string command =
            "kdialog --getsavefilename " + shell_quote(default_path) + " "
            "'PNG Images (*.png)' "
            "--title " + shell_quote(title) + " 2>/dev/null";
        return run_dialog_command(command.c_str());
    }
    return {
        {},
        "未找到系统文件选择器（需要 Zenity 或 KDialog）"
    };
#endif
}

} // namespace

struct NativeSavePanel::Impl {
    std::future<NativeFileDialogResult> task;
    bool active = false;
};

NativeSavePanel::NativeSavePanel()
    : impl_(std::make_unique<Impl>()) {}

NativeSavePanel::~NativeSavePanel() {
    if (impl_->task.valid()) {
        impl_->task.wait();
    }
}

bool NativeSavePanel::begin(
    const std::string& default_filename,
    const std::string& title,
    const std::string& filters
) {
    if (impl_->active) {
        return false;
    }
    impl_->task = std::async(
        std::launch::async,
        [default_filename, title, filters]() {
            return choose_save_file_blocking(
                default_filename,
                title,
                filters
            );
        }
    );
    impl_->active = true;
    return true;
}

std::optional<NativeFileDialogResult> NativeSavePanel::poll() {
    if (!impl_->active ||
        !impl_->task.valid() ||
        impl_->task.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return std::nullopt;
    }
    impl_->active = false;
    return impl_->task.get();
}

bool NativeSavePanel::active() const noexcept {
    return impl_->active;
}

#endif

NativeFileDialogResult choose_raw_data_file()
{
#if defined(_WIN32)
    return choose_windows_raw_file();
#elif defined(__APPLE__)
    return run_dialog_command(
        "osascript -e 'POSIX path of (choose file with prompt "
        "\"打开 CSV / DAT / GS3D 数据\")' 2>/dev/null"
    );
#else
    if (command_available("zenity")) {
        return run_dialog_command(
            "zenity --file-selection --title='打开散点数据' "
            "--file-filter='散点数据 | *.csv *.CSV *.dat *.DAT *.gs3d *.GS3D' "
            "--file-filter='所有文件 | *' 2>/dev/null"
        );
    }
    if (command_available("kdialog")) {
        return run_dialog_command(
            "kdialog --getopenfilename . "
            "'散点数据 (*.csv *.CSV *.dat *.DAT *.gs3d *.GS3D)' "
            "--title '打开散点数据' 2>/dev/null"
        );
    }
    return {
        {},
        "未找到系统文件选择器（需要 Zenity 或 KDialog）"
    };
#endif
}

} // namespace gs3d::platform
