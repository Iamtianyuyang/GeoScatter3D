#include "platform/NativeFileDialog.hpp"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace gs3d::platform {

namespace {

struct SavePanelState {
    std::mutex mutex;
    std::optional<NativeFileDialogResult> result;
    bool active = false;
};

NSString* make_ns_string(const std::string& value) {
    NSString* string = [[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding];
    return string != nil ? string : @"";
}

} // namespace

struct NativeSavePanel::Impl {
    std::shared_ptr<SavePanelState> state =
        std::make_shared<SavePanelState>();
    NSSavePanel* __strong panel = nil;
};

NativeSavePanel::NativeSavePanel()
    : impl_(std::make_unique<Impl>()) {}

NativeSavePanel::~NativeSavePanel() {
    NSSavePanel* panel = impl_->panel;
    impl_->state.reset();
    impl_->panel = nil;
    if (panel == nil) {
        return;
    }
    if ([NSThread isMainThread]) {
        [panel cancel:nil];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [panel cancel:nil];
        });
    }
}

bool NativeSavePanel::begin(
    const std::string& default_filename,
    const std::string& title,
    const std::string& /*filters*/
) {
    if (![NSThread isMainThread]) {
        return false;
    }

    {
        std::scoped_lock lock(impl_->state->mutex);
        if (impl_->state->active) {
            return false;
        }
        impl_->state->active = true;
        impl_->state->result.reset();
    }

    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = make_ns_string(title);
    panel.nameFieldStringValue = make_ns_string(default_filename);
    panel.canCreateDirectories = YES;
    panel.canSelectHiddenExtension = YES;
    panel.extensionHidden = NO;
    panel.allowsOtherFileTypes = NO;

    if (@available(macOS 11.0, *)) {
        panel.allowedContentTypes = @[UTTypePNG];
    }
    if ([panel respondsToSelector:@selector(setShowsContentTypes:)]) {
        panel.showsContentTypes = YES;
    }

    impl_->panel = panel;
    const std::weak_ptr<SavePanelState> weak_state = impl_->state;
    void (^completion_handler)(NSModalResponse) =
        ^(NSModalResponse response) {
            const auto state = weak_state.lock();
            if (!state) {
                return;
            }

            NativeFileDialogResult result;
            if (response == NSModalResponseOK) {
                NSURL* url = panel.URL;
                const char* file_system_path =
                    url.fileSystemRepresentation;
                if (file_system_path != nullptr) {
                    result.path =
                        std::filesystem::path(file_system_path);
                } else {
                    result.error =
                        "系统保存面板没有返回有效路径";
                }
            }

            std::scoped_lock lock(state->mutex);
            state->result = std::move(result);
            state->active = false;
        };

    NSWindow* owner = NSApp.keyWindow;
    if (owner == nil) {
        owner = NSApp.mainWindow;
    }
    if (owner != nil) {
        [panel beginSheetModalForWindow:owner
                     completionHandler:completion_handler];
    } else {
        [panel beginWithCompletionHandler:completion_handler];
    }
    return true;
}

std::optional<NativeFileDialogResult> NativeSavePanel::poll() {
    std::scoped_lock lock(impl_->state->mutex);
    if (!impl_->state->result.has_value()) {
        return std::nullopt;
    }
    auto result = std::move(impl_->state->result);
    impl_->state->result.reset();
    impl_->panel = nil;
    return result;
}

bool NativeSavePanel::active() const noexcept {
    std::scoped_lock lock(impl_->state->mutex);
    return impl_->state->active;
}

} // namespace gs3d::platform
